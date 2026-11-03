#include <arch/csr.h>
#include <arch/trap.h>
#include <arch/virq.h>
#include <hv/cpu.h>
#include <hv/hv.h>
#include <hv/mmio.h>

/*
 * External interrupts for the guest. The hart's supervisor external
 * interrupt (sip.SEIP) comes from its PLIC context and stays up until the
 * guest claims the interrupt, so h2 needs to see the claims: the PLIC's
 * per-context registers (threshold, claim/complete) are an emulated
 * device: h2 does the guest's accesses on the real PLIC (its S contexts;
 * the M ones read as zero). After each claim or completion, the guest's
 * external interrupt (hvip.VSEIP) follows the hart's.
 */

#define QEMU_VIRT_PLIC		0x0c000000UL
#define PLIC_CONTEXT_BASE	(QEMU_VIRT_PLIC + 0x200000)
#define PLIC_CONTEXT_END	(QEMU_VIRT_PLIC + 0x400000)
#define PLIC_CONTEXT_SIZE	0x1000UL
#define PLIC_CLAIM		4	/* claim on read, complete on write */

/* qemu virt: hart h has context 2h (M-mode) and 2h + 1 (S-mode) */
#define PLIC_CONTEXT_S(ctx)	((ctx) & 1)
#define PLIC_PRIORITY(src)	((volatile uint32_t *)(QEMU_VIRT_PLIC + 4 * (src)))
#define PLIC_ENABLE0(ctx)	((volatile uint32_t *)(QEMU_VIRT_PLIC + 0x2000 + 0x80 * (ctx)))
#define PLIC_THRESHOLD(ctx)	((volatile uint32_t *)(PLIC_CONTEXT_BASE + PLIC_CONTEXT_SIZE * (ctx)))

/*
 * Emulated devices' interrupts: virtual sources (1-31), with the numbers
 * their real devices have. The guest enables and prioritizes them on
 * the real PLIC, which h2 reads; a real interrupt of such a source is
 * h2's, which claims and completes it itself. The guest claims and
 * completes the virtual one: its line up, not claimed yet, enabled in the
 * hart's context, over its threshold.
 */
static volatile uint32_t vsrc_level, vsrc_claimed;
static struct hv_lock vsrc_lock;

static unsigned int this_context(void)
{
	return 2 * (unsigned int)hv_cpus[hv_cpu_index()].hwid + 1;
}

/* the virtual source this context would get now, or 0 */
static unsigned int vsrc_ready(unsigned int ctx)
{
	uint32_t ready = vsrc_level & ~vsrc_claimed & *PLIC_ENABLE0(ctx);
	uint32_t threshold = *PLIC_THRESHOLD(ctx), best = 0, prio, top = 0;
	unsigned int src;

	for (src = 1; src < 32 && ready >> src; src++) {
		if (!(ready >> src & 1))
			continue;
		prio = *PLIC_PRIORITY(src);
		if (prio > threshold && prio > top) {
			top = prio;
			best = src;
		}
	}
	return best;
}

/*
 * hvip.VSEIP: the hart's sip.SEIP, or a virtual source ready; while SEIP
 * is up, h2 needn't hear about it again
 */
void riscv64_external_sync(void)
{
	int seip = (csr_read(sip) & (1UL << IRQ_S_EXT)) != 0;

	if (seip || (vsrc_level && vsrc_ready(this_context())))
		csr_set(hvip, 1UL << IRQ_VS_EXT);
	else
		csr_clear(hvip, 1UL << IRQ_VS_EXT);
	if (seip)
		csr_clear(sie, 1UL << IRQ_S_EXT);
	else
		csr_set(sie, 1UL << IRQ_S_EXT);
}

/*
 * An emulated device's interrupt line goes up or down: for the harts
 * whose contexts enable it, this one or others (h2's IPI).
 */
void hv_virq_line(unsigned int irq, int level)
{
	uint32_t was;
	int slot, me = hv_cpu_index();

	if (irq == 0 || irq > 31)
		return;
	hv_lock(&vsrc_lock);
	was = vsrc_level;
	if (level)
		vsrc_level |= 1U << irq;
	else
		vsrc_level &= ~(1U << irq);
	hv_unlock(&vsrc_lock);
	if (was == vsrc_level)
		return;

	for (slot = 0; slot < HV_MAX_CPUS; slot++) {
		if (!hv_cpus[slot].used ||
		    !(*PLIC_ENABLE0(2 * hv_cpus[slot].hwid + 1) >> irq & 1))
			continue;
		if (slot == me)
			riscv64_external_sync();
		else
			riscv64_kick(slot);
	}
}

/* a claim: h2's own sources handled first, then the guest's virtual ones */
static uint32_t claim(volatile uint32_t *reg, unsigned int ctx)
{
	uint32_t id = *reg, n;

	for (n = 0; id && n < 32 && hv_mmio_phys_irq(id); n++) {
		*reg = id;
		id = *reg;
	}
	if (!id) {
		hv_lock(&vsrc_lock);
		id = vsrc_ready(ctx);
		vsrc_claimed |= id ? 1U << id : 0;
		hv_unlock(&vsrc_lock);
	}
	return id;
}

static void complete(volatile uint32_t *reg, uint32_t id)
{
	if (id && id < 32 && (vsrc_claimed & 1U << id)) {
		hv_lock(&vsrc_lock);
		vsrc_claimed &= ~(1U << id);
		hv_unlock(&vsrc_lock);
	} else {
		*reg = id;
	}
}

/* a context page, as the guest sees it: its S contexts are the real ones */
static uint64_t context_read(void *ctx, hv_addr_t offset, int size)
{
	volatile uint32_t *reg = (volatile uint32_t *)(PLIC_CONTEXT_BASE + offset);
	uint32_t value;

	(void)ctx;
	if (size != 4 || !PLIC_CONTEXT_S(offset / PLIC_CONTEXT_SIZE))
		return 0;
	if ((offset & (PLIC_CONTEXT_SIZE - 1)) != PLIC_CLAIM)
		return *reg;
	value = claim(reg, (unsigned int)(offset / PLIC_CONTEXT_SIZE));
	riscv64_external_sync();
	return value;
}

static void context_write(void *ctx, hv_addr_t offset, int size, uint64_t value)
{
	volatile uint32_t *reg = (volatile uint32_t *)(PLIC_CONTEXT_BASE + offset);

	(void)ctx;
	if (size != 4 || !PLIC_CONTEXT_S(offset / PLIC_CONTEXT_SIZE))
		return;
	if ((offset & (PLIC_CONTEXT_SIZE - 1)) != PLIC_CLAIM) {
		*reg = (uint32_t)value;
		return;
	}
	complete(reg, (uint32_t)value);
	riscv64_external_sync();
}

static const struct hv_mmio_dev contexts = {
	.base = PLIC_CONTEXT_BASE,
	.size = PLIC_CONTEXT_END - PLIC_CONTEXT_BASE,
	.read = context_read,
	.write = context_write,
};

void riscv64_vplic_init(void)
{
	hv_mmio_register(&contexts);
}
