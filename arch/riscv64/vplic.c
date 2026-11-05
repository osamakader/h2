#include <arch/barrier.h>
#include <arch/csr.h>
#include <arch/trap.h>
#include <arch/virq.h>
#include <arch/vplic.h>
#include <hv/cpu.h>
#include <hv/hv.h>
#include <hv/mmio.h>
#include <hv/vcpu.h>

/*
 * Each guest's PLIC: an emulated device, so no guest touches the real
 * one, which is h2's. What a guest programs (priorities, enables,
 * thresholds) and has pending or in service is kept here. Guest 0 owns the
 * machine's sources, but for the ones of devices h2 emulates, so its
 * enables go to the real PLIC as well, and when one of them interrupts,
 * h2 claims it at once and marks it pending here; the real completion
 * waits for the guest's. So a hart's supervisor external interrupt is
 * h2's alone, and the guest's (hvip.VSEIP) says only that its own PLIC
 * has something for this hart. An emulated device's source is its level
 * line (hv_virq_line): pending while it is up.
 */

#define QEMU_VIRT_PLIC		0x0c000000UL
#define PLIC_SIZE		0x400000UL
#define PLIC_PENDING		0x1000UL
#define PLIC_ENABLE		0x2000UL
#define PLIC_ENABLE_CTX		0x80UL
#define PLIC_CONTEXT		0x200000UL
#define PLIC_CONTEXT_SIZE	0x1000UL
#define PLIC_THRESHOLD		0
#define PLIC_CLAIM		4	/* claim on read, complete on write */

#define NR_SRC			96	/* qemu virt's */
#define WORDS			(NR_SRC / 32)
#define NR_HARTS		8

/* qemu virt: hart h has context 2h (M-mode) and 2h + 1 (S-mode) */
#define S_CONTEXT(hart)		(2 * (hart) + 1)
#define REAL(off)		((volatile uint32_t *)(QEMU_VIRT_PLIC + (off)))
#define REAL_ENABLE(ctx, w)	REAL(PLIC_ENABLE + PLIC_ENABLE_CTX * (ctx) + 4 * (w))
#define REAL_CLAIM(ctx)		REAL(PLIC_CONTEXT + PLIC_CONTEXT_SIZE * (ctx) + PLIC_CLAIM)
#define REAL_THRESHOLD(ctx)	REAL(PLIC_CONTEXT + PLIC_CONTEXT_SIZE * (ctx))

struct plic {
	uint32_t prio[NR_SRC];
	uint32_t enable[NR_HARTS][WORDS];
	uint32_t threshold[NR_HARTS];
	volatile uint32_t pending[WORDS];	/* real ones h2 has claimed */
	volatile uint32_t lines[WORDS];		/* emulated devices' */
	uint32_t in_service[WORDS];
};

static struct plic plics[HV_MAX_GUESTS];
static struct hv_lock lock;
static uint32_t h2_src[WORDS];			/* sources that are h2's */
static uint8_t real_ctx[NR_SRC];		/* claimed in that context */

static int this_hart(void)
{
	return (int)hv_cpus[hv_cpu_index()].hwid;
}

/*
 * The source guest g's PLIC would give hart now: pending, not in service,
 * enabled, over its threshold; the highest priority, then the lowest
 * number. 0 if there is none.
 */
static unsigned int ready(int g, int hart)
{
	struct plic *p = &plics[g];
	uint32_t top = p->threshold[hart], w;
	unsigned int best = 0, n, i;

	for (n = 0; n < WORDS; n++) {
		w = (p->pending[n] | p->lines[n]) & ~p->in_service[n] &
		    p->enable[hart][n];
		for (i = 0; w >> i && i < 32; i++) {
			if ((w >> i & 1) && p->prio[n * 32 + i] > top) {
				top = p->prio[n * 32 + i];
				best = n * 32 + i;
			}
		}
	}
	return best;
}

/*
 * hvip.VSEIP: the running vCPU's guest's PLIC has something for this
 * hart. Guest 0's while another runs: it had better be next.
 */
void riscv64_external_sync(void)
{
	struct hv_vcpu *vcpu = hv_vcpu_current();
	int hart = this_hart(), g;

	if (!vcpu || hart >= NR_HARTS) {
		csr_clear(hvip, 1UL << IRQ_VS_EXT);
		return;
	}
	g = vcpu->guest->id;
	if (ready(g, hart))
		csr_set(hvip, 1UL << IRQ_VS_EXT);
	else
		csr_clear(hvip, 1UL << IRQ_VS_EXT);
	if (g && ready(0, hart))
		hv_vcpu_yield();
}

/*
 * The hart's external interrupt: h2 claims all there is. A device of h2's
 * is handled and completed; others are guest 0's to claim from its PLIC,
 * and complete, which completes the real one.
 */
void riscv64_external_interrupt(void)
{
	int hart = this_hart();
	unsigned int ctx = S_CONTEXT(hart);
	uint32_t id;

	while ((id = *REAL_CLAIM(ctx))) {
		if (hv_mmio_phys_irq(id)) {
			*REAL_CLAIM(ctx) = id;
		} else if (id < NR_SRC) {
			hv_lock(&lock);
			plics[0].pending[id / 32] |= 1U << (id % 32);
			real_ctx[id] = (uint8_t)ctx;
			hv_unlock(&lock);
		} else {
			*REAL_CLAIM(ctx) = id;
		}
	}
	riscv64_external_sync();
}

/* look again, here or on another hart (h2's IPI) */
static void look(int hart)
{
	int slot;

	if (hart == this_hart()) {
		riscv64_external_sync();
		return;
	}
	for (slot = 0; slot < HV_MAX_CPUS; slot++)
		if (hv_cpus[slot].used && (int)hv_cpus[slot].hwid == hart)
			riscv64_kick(slot);
}

/* an emulated device's line goes up or down: guest 0's, for now */
void hv_virq_line(unsigned int irq, int level)
{
	struct plic *p = &plics[0];
	uint32_t bit = 1U << (irq % 32), was;
	int hart;

	if (irq == 0 || irq >= NR_SRC)
		return;
	hv_lock(&lock);
	was = p->lines[irq / 32];
	if (level)
		p->lines[irq / 32] |= bit;
	else
		p->lines[irq / 32] &= ~bit;
	hv_unlock(&lock);
	if (was == p->lines[irq / 32])
		return;
	for (hart = 0; hart < NR_HARTS; hart++)
		if (p->enable[hart][irq / 32] & bit)
			look(hart);
}

static unsigned int claim(int g, int hart)
{
	struct plic *p = &plics[g];
	unsigned int id;

	hv_lock(&lock);
	id = ready(g, hart);
	if (id) {
		p->pending[id / 32] &= ~(1U << (id % 32));
		p->in_service[id / 32] |= 1U << (id % 32);
	}
	hv_unlock(&lock);
	return id;
}

static void complete(int g, uint32_t id)
{
	struct plic *p = &plics[g];
	unsigned int ctx = 0;

	if (id == 0 || id >= NR_SRC)
		return;
	hv_lock(&lock);
	p->in_service[id / 32] &= ~(1U << (id % 32));
	if (g == 0) {
		ctx = real_ctx[id];
		real_ctx[id] = 0;
	}
	hv_unlock(&lock);
	if (ctx)
		*REAL_CLAIM(ctx) = id;
}

static uint64_t plic_read(void *ctx, hv_addr_t off, int size)
{
	int g = hv_vcpu_current()->guest->id;
	struct plic *p = &plics[g];
	hv_addr_t reg;
	unsigned int c, w;
	uint32_t v = 0;

	(void)ctx;
	if (size != 4)
		return 0;
	if (off < PLIC_PENDING) {
		if (off / 4 < NR_SRC)
			v = p->prio[off / 4];
	} else if (off < PLIC_ENABLE) {
		w = (unsigned int)(off - PLIC_PENDING) / 4;
		if (w < WORDS)
			v = p->pending[w] | p->lines[w];
	} else if (off < PLIC_CONTEXT) {
		c = (unsigned int)((off - PLIC_ENABLE) / PLIC_ENABLE_CTX);
		w = (unsigned int)((off - PLIC_ENABLE) % PLIC_ENABLE_CTX) / 4;
		if ((c & 1) && c / 2 < NR_HARTS && w < WORDS)
			v = p->enable[c / 2][w];
	} else {
		c = (unsigned int)((off - PLIC_CONTEXT) / PLIC_CONTEXT_SIZE);
		reg = (off - PLIC_CONTEXT) % PLIC_CONTEXT_SIZE;
		if (!(c & 1) || c / 2 >= NR_HARTS)
			return 0;
		if (reg == PLIC_THRESHOLD) {
			v = p->threshold[c / 2];
		} else if (reg == PLIC_CLAIM) {
			v = claim(g, (int)(c / 2));
			riscv64_external_sync();
		}
	}
	return v;
}

static void plic_write(void *ctx, hv_addr_t off, int size, uint64_t value)
{
	int g = hv_vcpu_current()->guest->id;
	struct plic *p = &plics[g];
	uint32_t v = (uint32_t)value, mine;
	hv_addr_t reg;
	unsigned int c, w;

	(void)ctx;
	if (size != 4)
		return;
	if (off < PLIC_PENDING) {
		if (off / 4 < NR_SRC && off >= 4) {
			p->prio[off / 4] = v;
			look(this_hart());
		}
	} else if (off >= PLIC_ENABLE && off < PLIC_CONTEXT) {
		c = (unsigned int)((off - PLIC_ENABLE) / PLIC_ENABLE_CTX);
		w = (unsigned int)((off - PLIC_ENABLE) % PLIC_ENABLE_CTX) / 4;
		if (!(c & 1) || c / 2 >= NR_HARTS || w >= WORDS)
			return;
		p->enable[c / 2][w] = v;
		if (g == 0) {
			mine = ~h2_src[w];
			hv_lock(&lock);
			*REAL_ENABLE(c, w) = (*REAL_ENABLE(c, w) & ~mine) | (v & mine);
			hv_unlock(&lock);
		}
		look((int)(c / 2));
	} else if (off >= PLIC_CONTEXT) {
		c = (unsigned int)((off - PLIC_CONTEXT) / PLIC_CONTEXT_SIZE);
		reg = (off - PLIC_CONTEXT) % PLIC_CONTEXT_SIZE;
		if (!(c & 1) || c / 2 >= NR_HARTS)
			return;
		if (reg == PLIC_THRESHOLD) {
			p->threshold[c / 2] = v;
			look((int)(c / 2));
		} else if (reg == PLIC_CLAIM) {
			complete(g, v);
			riscv64_external_sync();
		}
	}
}

static const struct hv_mmio_dev device = {
	.base = QEMU_VIRT_PLIC,
	.size = PLIC_SIZE,
	.read = plic_read,
	.write = plic_write,
	.all_guests = 1,
};

/*
 * A device h2 emulates owns the real source irq: on, on this hart; a
 * guest's enable of its number is only for the virtual one.
 */
void arch_irq_own(unsigned int irq)
{
	unsigned int ctx = S_CONTEXT(this_hart());

	if (irq == 0 || irq >= NR_SRC)
		return;
	h2_src[irq / 32] |= 1U << (irq % 32);
	*REAL_ENABLE(ctx, irq / 32) |= 1U << (irq % 32);
}

/*
 * Once, on the boot hart: the real PLIC is h2's, every source alike, none
 * on until a guest says so, every S context taking them all.
 */
void riscv64_vplic_init(void)
{
	unsigned int i, h, w;

	for (i = 1; i < NR_SRC; i++)
		*REAL(4UL * i) = 1;
	for (h = 0; h < NR_HARTS; h++) {
		for (w = 0; w < WORDS; w++)
			*REAL_ENABLE(S_CONTEXT(h), w) = 0;
		*REAL_THRESHOLD(S_CONTEXT(h)) = 0;
	}
	hv_mmio_register(&device);
}
