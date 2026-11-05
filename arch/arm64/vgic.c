#include <arch/barrier.h>
#include <arch/platform.h>
#include <arch/trap.h>
#include <arch/vgic.h>
#include <hv/cpu.h>
#include <hv/hv.h>
#include <hv/mmio.h>
#include <hv/stage2.h>
#include <hv/vcpu.h>

/*
 * Interrupts for the guest through the GIC's virtual CPU interface.
 * Physical IRQs come to h2 (HCR_EL2.IMO), which acknowledges each, drops
 * its priority (EOImode 1) and puts it in the running vCPU's list
 * registers: SPIs and PPIs linked to the physical interrupt (HW), so the
 * guest's own EOI deactivates it; SGIs as software ones, deactivated by
 * h2 at once. The guest acknowledges and ends them on the virtual CPU
 * interface: GICv3's ICV_* registers (its ICC_* accesses, now that IMO
 * is set), GICv2's GICV, shown to it where its GICC is. The distributor
 * and the redistributors are still the guest's, so it enables, routes and
 * prioritizes its interrupts there itself; h2 only adds its maintenance
 * interrupt.
 *
 * Interrupts that don't fit in the list registers wait in the vCPU's
 * queue, for the maintenance interrupt (underflow: list registers about
 * to run out) or for an end of interrupt h2 asked to hear of (EOI). A
 * vCPU that goes off its CPU takes its list registers along, and with
 * them the SPIs it was given, active until it ends them. The CPU's own
 * interrupts (PPIs: its timers) follow the vCPU that runs: see
 * arm64_vgic_save().
 */

#define MAINT_INTID		25	/* PPI 9, on both platforms */
#define KICK_INTID		15	/* h2's real SGI; guests' are virtual */
#define TICK_INTID		26	/* PPI 10: CNTHP, h2's own timer */
#define SPURIOUS(id)		((id) >= 1020 && (id) < 1024)
#define LPI_FIRST		8192

/* list registers, as GICv3 has them */
#define LR_VINTID(lr)		((lr) & 0xffffffffUL)
#define LR_PINTID_SHIFT		32
#define LR_PINTID(lr)		(((lr) >> LR_PINTID_SHIFT) & 0x1fff)
#define LR_PINTID_MASK		(0x1fffUL << LR_PINTID_SHIFT)
#define LR_EOI			(1UL << 41)	/* software ones: tell h2 */
#define LR_PRIO_SHIFT		48
#define LR_GROUP1		(1UL << 60)
#define LR_HW			(1UL << 61)
#define LR_PENDING		(1UL << 62)
#define LR_ACTIVE		(1UL << 63)
#define LR_STATE		(LR_PENDING | LR_ACTIVE)

#define HCR_EN			(1UL << 0)
#define HCR_UIE			(1UL << 1)

/* GICv3: system registers */
#define SYSREG_READ(name) ({ uint64_t __v; \
	__asm__ volatile("mrs %0, " name : "=r"(__v)); __v; })
#define SYSREG_WRITE(name, v) \
	__asm__ volatile("msr " name ", %0" :: "r"((uint64_t)(v)))

#define ICC_PMR_EL1		"S3_0_C4_C6_0"
#define ICC_DIR_EL1		"S3_0_C12_C11_1"
#define ICC_RPR_EL1		"S3_0_C12_C11_3"
#define ICC_SGI1R_EL1		"S3_0_C12_C11_5"
#define ICC_ASGI1R_EL1		"S3_0_C12_C11_6"
#define ICC_SGI0R_EL1		"S3_0_C12_C11_7"
#define ICC_IAR1_EL1		"S3_0_C12_C12_0"
#define ICC_EOIR1_EL1		"S3_0_C12_C12_1"
#define ICC_BPR1_EL1		"S3_0_C12_C12_3"
#define ICC_CTLR_EL1		"S3_0_C12_C12_4"
#define ICC_IGRPEN1_EL1		"S3_0_C12_C12_7"
#define ICH_HCR_EL2		"S3_4_C12_C11_0"
#define ICH_VTR_EL2		"S3_4_C12_C11_1"
#define ICH_VMCR_EL2		"S3_4_C12_C11_7"
#define ICC_CTLR_EOIMODE	(1UL << 1)
#define ICH_VTR_LRS(v)		(((v) & 0x1f) + 1)
#define ICH_VTR_PREBITS(v)	((((v) >> 26) & 7) + 1)

/* GICv3 redistributors: an RD_base and an SGI_base frame per CPU */
#define GICR_TYPER		0x8
#define GICR_TYPER_LAST		(1UL << 4)
#define GICR_FRAME		0x20000
#define GICR_SGI_BASE		0x10000
#define GICx_IGROUPR0		0x080	/* in SGI_base, or GICD (banked) */
#define GICx_ISENABLER0		0x100
#define GICx_ICENABLER0		0x180
#define GICx_IPRIORITYR0	0x400
#define GICR_WAKER		0x14
#define H2_PPIS			(1U << MAINT_INTID | 1U << TICK_INTID)
#define GICx_ISACTIVER0		0x300

/* GICv2: memory mapped */
#define GICC_CTLR		0x000
#define GICC_PMR		0x004
#define GICC_IAR		0x00c
#define GICC_EOIR		0x010
#define GICC_RPR		0x014
#define GICC_DIR		0x1000
#define GICC_CTLR_ENABLE	(1U << 0)
#define GICC_CTLR_EOIMODE	(1U << 9)	/* EOImodeS, or NS's own */
#define GICH_HCR		0x000
#define GICH_VTR		0x004
#define GICH_VMCR		0x008
#define GICH_APR		0x0f0
#define GICH_LR(n)		(0x100 + 4 * (n))
#define GICH_VTR_LRS(v)		(((v) & 0x3f) + 1)

/* GICv2 list registers */
#define V2_VID(lr)		((lr) & 0x3ff)
#define V2_PID_SHIFT		10		/* HW: physical id, else */
#define V2_CPUID(lr)		(((lr) >> 10) & 7)	/* the SGI's source */
#define V2_EOI			(1U << 19)
#define V2_PRIO_SHIFT		23		/* 5 bits */
#define V2_STATE_SHIFT		28
#define V2_GROUP1		(1U << 30)
#define V2_HW			(1U << 31)

/* a trapped system register access (EC 0x18): ISS without Rt and read */
#define ISS_SYSREG(op0, op1, crn, crm, op2) \
	((op0) << 20 | (op2) << 17 | (op1) << 14 | (crn) << 10 | (crm) << 1)
#define ISS_SYSREG_MASK		ISS_SYSREG(3, 7, 15, 15, 7)
#define ISS_RT(iss)		(((iss) >> 5) & 0x1f)
#define ISS_READ		1

static int enabled;		/* a GIC with virtualization: from setup */
static int gic3;		/* its system registers, else GICv2 */
static int nr_lrs, nr_aprs;

/* each CPU's PPI registers: its redistributor's SGI_base, or GICD */
static hv_addr_t ppi_regs[HV_MAX_CPUS];

static volatile uint32_t *mmio(hv_addr_t addr)
{
	return (volatile uint32_t *)addr;
}

#define LR_CASE(c, n, i) \
	case i: \
		if (write) \
			SYSREG_WRITE("S3_4_C12_C" #c "_" #n, v); \
		else \
			v = SYSREG_READ("S3_4_C12_C" #c "_" #n); \
		break;

static uint64_t ich_lr(int i, int write, uint64_t v)
{
	switch (i) {
	LR_CASE(12, 0, 0) LR_CASE(12, 1, 1) LR_CASE(12, 2, 2) LR_CASE(12, 3, 3)
	LR_CASE(12, 4, 4) LR_CASE(12, 5, 5) LR_CASE(12, 6, 6) LR_CASE(12, 7, 7)
	LR_CASE(13, 0, 8) LR_CASE(13, 1, 9) LR_CASE(13, 2, 10) LR_CASE(13, 3, 11)
	LR_CASE(13, 4, 12) LR_CASE(13, 5, 13) LR_CASE(13, 6, 14) LR_CASE(13, 7, 15)
	}
	return v;
}

#define APR_CASE(c, n) \
	case n: \
		if (write) \
			SYSREG_WRITE("S3_4_C12_C" #c "_" #n, v); \
		else \
			v = SYSREG_READ("S3_4_C12_C" #c "_" #n); \
		break;

/* ICH_AP0R<n>_EL2 (group 0) and ICH_AP1R<n>_EL2 */
static uint32_t ich_apr(int group, int n, int write, uint32_t v)
{
	if (group) {
		switch (n) {
		APR_CASE(9, 0) APR_CASE(9, 1) APR_CASE(9, 2) APR_CASE(9, 3)
		}
	} else {
		switch (n) {
		APR_CASE(8, 0) APR_CASE(8, 1) APR_CASE(8, 2) APR_CASE(8, 3)
		}
	}
	return v;
}

static uint64_t lr_get(int i)
{
	uint32_t v2;
	uint64_t lr;

	if (gic3)
		return ich_lr(i, 0, 0);

	v2 = *mmio(PLATFORM_GICH + GICH_LR(i));
	lr = V2_VID(v2) | (uint64_t)((v2 >> V2_PRIO_SHIFT) & 0x1f) << 3 << LR_PRIO_SHIFT |
	     (uint64_t)((v2 >> V2_STATE_SHIFT) & 3) << 62;
	if (v2 & V2_GROUP1)
		lr |= LR_GROUP1;
	if (v2 & V2_HW)
		lr |= LR_HW | (uint64_t)((v2 >> V2_PID_SHIFT) & 0x3ff) << LR_PINTID_SHIFT;
	else
		lr |= (uint64_t)V2_CPUID(v2) << LR_PINTID_SHIFT |
		      ((v2 & V2_EOI) ? LR_EOI : 0);
	return lr;
}

static void lr_put(int i, uint64_t lr)
{
	uint32_t v2;

	if (gic3) {
		ich_lr(i, 1, lr);
		return;
	}

	v2 = (uint32_t)(LR_VINTID(lr) & 0x3ff) |
	     (uint32_t)((lr >> LR_PRIO_SHIFT >> 3) & 0x1f) << V2_PRIO_SHIFT |
	     (uint32_t)(lr >> 62) << V2_STATE_SHIFT;
	if (lr & LR_GROUP1)
		v2 |= V2_GROUP1;
	if (lr & LR_HW)
		v2 |= V2_HW | (uint32_t)(LR_PINTID(lr) & 0x3ff) << V2_PID_SHIFT;
	else
		v2 |= (uint32_t)(LR_PINTID(lr) & 7) << V2_PID_SHIFT |
		      ((lr & LR_EOI) ? V2_EOI : 0);
	*mmio(PLATFORM_GICH + GICH_LR(i)) = v2;
}

static void hcr_put(uint64_t hcr)
{
	if (gic3)
		SYSREG_WRITE(ICH_HCR_EL2, hcr);
	else
		*mmio(PLATFORM_GICH + GICH_HCR) = (uint32_t)hcr;
}

/* the physical CPU interface, h2's: acknowledge, its priority, end */
static uint64_t ack(void)
{
	return gic3 ? SYSREG_READ(ICC_IAR1_EL1) : *mmio(PLATFORM_GICC + GICC_IAR);
}

static void priority_drop(uint64_t iar)
{
	if (gic3)
		SYSREG_WRITE(ICC_EOIR1_EL1, iar);
	else
		*mmio(PLATFORM_GICC + GICC_EOIR) = (uint32_t)iar;
}

static void deactivate(uint64_t iar)
{
	if (gic3)
		SYSREG_WRITE(ICC_DIR_EL1, iar);
	else
		*mmio(PLATFORM_GICC + GICC_DIR) = (uint32_t)iar;
}

/*
 * Put lr (pending) in a list register: 1 if done (or merged into the
 * one pending already), -1 if none is free, 0 if the guest has the same
 * interrupt as a software one: once it deactivates that, h2 hears of it.
 */
static int place(uint64_t lr)
{
	int i, free = -1;
	uint64_t cur;

	for (i = 0; i < nr_lrs; i++) {
		cur = lr_get(i);
		if (!(cur & LR_STATE)) {
			if (free < 0)
				free = i;
			continue;
		}
		/* GICv2: the same SGI from another CPU is another one */
		if (LR_VINTID(cur) != LR_VINTID(lr) ||
		    (!(lr & LR_HW) && !(cur & LR_HW) &&
		     LR_PINTID(cur & ~LR_EOI) != LR_PINTID(lr)))
			continue;
		if (!(lr & LR_HW) && !(cur & LR_HW)) {
			lr_put(i, cur | LR_PENDING);
			return 1;
		}
		if (!(cur & LR_HW))
			lr_put(i, cur | LR_EOI);
		return 0;
	}
	if (free < 0)
		return -1;
	lr_put(free, lr);
	return 1;
}

/*
 * Emulated devices' interrupts: level-sensitive, with no physical
 * interrupt behind them, so software ones that ask for maintenance when
 * the guest ends them (EOI), when h2 looks at the line again.
 */
#define VLINES			4
#define GICD_SGIR		0xf00

static struct {
	unsigned int intid;
	volatile int level;
	volatile int cpu;		/* where the guest routes it, or -1 */
} vlines[VLINES];
static int nvlines;

/* have another CPU look at the lines and SGIs (h2's own SGI) */
static void kick(int slot)
{
	hv_addr_t aff = hv_cpus[slot].hwid;

	if (gic3) {
		SYSREG_WRITE(ICC_SGI1R_EL1, (uint64_t)KICK_INTID << 24 |
			     (aff >> 32 & 0xff) << 48 | (aff >> 16 & 0xff) << 32 |
			     (aff >> 8 & 0xff) << 16 | 1UL << (aff & 15));
		__asm__ volatile("isb" ::: "memory");
	} else {
		*mmio(PLATFORM_GICD + GICD_SGIR) = 1U << (16 + (aff & 7)) | KICK_INTID;
	}
}

/* intid pending or active for the running vCPU, or waiting in its queue */
static int vline_present(struct arm64_vgic *vgic, unsigned int intid)
{
	uint64_t lr;
	int i;

	for (i = 0; i < nr_lrs; i++) {
		lr = lr_get(i);
		if ((lr & LR_STATE) && !(lr & LR_HW) && LR_VINTID(lr) == intid)
			return 1;
	}
	for (i = 0; i < vgic->queued; i++)
		if (LR_VINTID(vgic->queue[i]) == intid)
			return 1;
	return 0;
}

/* the line went down: whatever the guest hasn't taken yet goes */
static void vline_retract(struct arm64_vgic *vgic, unsigned int intid)
{
	uint64_t lr;
	int i, n = 0;

	for (i = 0; i < nr_lrs; i++) {
		lr = lr_get(i);
		if (!(lr & LR_STATE) || (lr & LR_HW) || LR_VINTID(lr) != intid)
			continue;
		lr_put(i, (lr & LR_ACTIVE) ? lr & ~LR_PENDING : 0);
	}
	for (i = 0; i < vgic->queued; i++)
		if (LR_VINTID(vgic->queue[i]) != intid)
			vgic->queue[n++] = vgic->queue[i];
	vgic->queued = n;
}

/*
 * The lines, for the vCPU that runs here: one that is up and routed here
 * goes in its queue unless it has it already; one that isn't, out of it
 * unless it has taken it.
 */
static void vline_resample(struct arm64_vgic *vgic)
{
	unsigned int intid;
	int i, here = hv_cpu_index();

	/* the lines are guest 0's */
	if (hv_vcpu_current()->guest->id)
		return;
	for (i = 0; i < nvlines; i++) {
		intid = vlines[i].intid;
		if (!vlines[i].level || !arm64_vgicd_enabled(0, here, intid) ||
		    (vlines[i].cpu >= 0 && vlines[i].cpu != here)) {
			vline_retract(vgic, intid);
			continue;
		}
		if (vline_present(vgic, intid) || vgic->queued == ARM64_VGIC_QUEUE)
			continue;
		vgic->queue[vgic->queued++] = intid | LR_PENDING | LR_EOI |
			(uint64_t)arm64_vgicd_prio(0, here, intid) << LR_PRIO_SHIFT |
			(gic3 ? LR_GROUP1 : 0);
	}
}

/*
 * The queue, into the list registers, as far as it goes: if they run
 * out, h2 wants the underflow maintenance interrupt.
 */
/* the PPIs on for real are the ones the running vCPU's guest wants */
static uint32_t ppis_on[HV_MAX_CPUS];

static void ppi_sync(void)
{
	struct hv_vcpu *vcpu = hv_vcpu_current();
	int me = hv_cpu_index();
	uint32_t want = arm64_vgic_ppi_wanted(vcpu->guest->id, me) & ~H2_PPIS;

	if (want == ppis_on[me])
		return;
	*mmio(ppi_regs[me] + GICx_ICENABLER0) = ~want & 0xffff0000U & ~H2_PPIS;
	*mmio(ppi_regs[me] + GICx_ISENABLER0) = want;
	ppis_on[me] = want;
}

/* the running vCPU's guest's SGIs, from its mailbox into its queue */
static void sgi_drain(struct hv_vcpu *vcpu)
{
	struct arm64_vgic *vgic = &vcpu->arch.vgic;
	int g = vcpu->guest->id, me = hv_cpu_index();
	unsigned int intid, src;

	while (vgic->queued < ARM64_VGIC_QUEUE &&
	       arm64_vgicd_sgi_take(g, me, &intid, &src)) {
		vgic->queue[vgic->queued++] = intid | LR_PENDING |
			(uint64_t)arm64_vgicd_prio(g, me, intid) << LR_PRIO_SHIFT |
			(gic3 ? LR_GROUP1 :
			 (uint64_t)src << LR_PINTID_SHIFT);
	}
}

static void refill(struct arm64_vgic *vgic)
{
	int i, n = 0, full = 0, r;
	uint64_t lr;

	ppi_sync();
	sgi_drain(hv_vcpu_current());

	/* done with: the ones that asked for maintenance need clearing */
	for (i = 0; i < nr_lrs; i++) {
		lr = lr_get(i);
		if (lr && !(lr & LR_STATE))
			lr_put(i, 0);
	}
	vline_resample(vgic);

	for (i = 0; i < vgic->queued; i++) {
		r = full ? -1 : place(vgic->queue[i]);
		if (r < 0)
			full = 1;
		if (r != 1)
			vgic->queue[n++] = vgic->queue[i];
	}
	vgic->queued = n;
	hcr_put(HCR_EN | (full ? HCR_UIE : 0));
}

/*
 * To vcpu on this CPU, the running one or not: one that isn't finds it
 * in its queue when it loads, and runs next.
 */
static void inject(struct hv_vcpu *vcpu, uint64_t lr)
{
	struct arm64_vgic *vgic = &vcpu->arch.vgic;

	if (vgic->queued == ARM64_VGIC_QUEUE) {
		/* a level interrupt comes back; an SGI is lost */
		if (lr & LR_HW)
			deactivate(LR_PINTID(lr));
		hv_console_write("h2: vgic: queue full, dropped ");
		hv_console_hex(LR_VINTID(lr));
		hv_console_putchar('\n');
		return;
	}
	vgic->queue[vgic->queued++] = lr;
	if (vcpu == hv_vcpu_current())
		refill(vgic);
	else
		hv_vcpu_yield();
}

/*
 * An emulated device's interrupt line (an SPI) goes up or down: for the
 * vCPU that runs on the CPU the guest routes it to, as GICv2 / GICv3
 * would for a level-sensitive one. Another CPU hears of it from h2's SGI.
 */
void hv_virq_line(unsigned int irq, int level)
{
	struct arm64_vgic *vgic;
	int i, cpu;

	for (i = 0; i < nvlines && vlines[i].intid != irq; i++)
		;
	if (i == nvlines) {
		if (nvlines == VLINES)
			return;
		vlines[nvlines++].intid = irq;
	}
	if (!enabled || !hv_vcpu_current()) {
		vlines[i].level = level;
		return;
	}
	cpu = arm64_vgicd_route(0, irq);
	if (vlines[i].level == level && vlines[i].cpu == cpu)
		return;
	vlines[i].cpu = cpu;
	hv_mb();
	vlines[i].level = level;
	hv_mb();

	if (cpu >= 0 && cpu != hv_cpu_index()) {
		kick(cpu);
	} else if (hv_vcpu_current()->guest->id) {
		/* guest 0's, and another guest runs: it'd better be next */
		if (level)
			hv_vcpu_yield();
	} else {
		vgic = &hv_vcpu_current()->arch.vgic;
		refill(vgic);
	}
}

/*
 * A guest changed its distributor: lines may need another look here and
 * on the CPUs routed to, and PPIs and SGIs on the CPUs it runs on.
 */
void arm64_vgic_changed(int g)
{
	struct hv_vcpu *vcpu = hv_vcpu_current();
	int i, slot, me = hv_cpu_index();

	if (g == 0)
		for (i = 0; i < nvlines; i++)
			vlines[i].cpu = arm64_vgicd_route(0, vlines[i].intid);
	hv_mb();
	if (vcpu->guest->id == g)
		refill(&vcpu->arch.vgic);
	for (slot = 0; slot < HV_MAX_CPUS; slot++)
		if (slot != me && hv_cpus[slot].used)
			kick(slot);
}

/* an SGI for guest g's vCPU on target */
static void sgi_send(int g, int target, unsigned int intid, unsigned int src)
{
	struct hv_vcpu *vcpu = hv_vcpu_current();

	arm64_vgicd_sgi(g, target, intid, src);
	if (target != hv_cpu_index())
		kick(target);
	else if (vcpu->guest->id == g)
		refill(&vcpu->arch.vgic);
}

/* GICv2's GICD_SGIR, written by the running guest */
void arm64_vgic_sgi_v2(uint32_t sgir)
{
	int g = hv_vcpu_current()->guest->id, me = hv_cpu_index(), slot;
	unsigned int filter = sgir >> 24 & 3, list = sgir >> 16 & 0xff;

	for (slot = 0; slot < HV_MAX_CPUS; slot++) {
		if (!hv_cpus[slot].used)
			continue;
		if (filter == 0 ? list >> (hv_cpus[slot].hwid & 7) & 1 :
		    filter == 1 ? slot != me : slot == me)
			sgi_send(g, slot, sgir & 0xf,
				 (unsigned int)(hv_cpus[me].hwid & 7));
	}
}

/* a physical IRQ, taken from the guest: on to the running vCPU */
static uint64_t lr_for(const struct hv_vcpu *vcpu, uint64_t intid)
{
	return intid | LR_PENDING | (gic3 ? LR_GROUP1 : 0) |
	       (uint64_t)arm64_vgicd_prio(vcpu->guest->id, vcpu->cpu,
					  (unsigned int)intid) << LR_PRIO_SHIFT;
}

void arm64_handle_irq(struct hv_trap_frame *frame)
{
	struct hv_vcpu *vcpu = hv_vcpu_current(), *owner;
	struct arm64_vgic *vgic = &vcpu->arch.vgic;
	uint64_t iar = ack(), intid;
	int g, me = hv_cpu_index();

	intid = gic3 ? iar & 0xffffff : iar & 0x3ff;
	if (!SPURIOUS(intid)) {
		priority_drop(iar);

		if (intid == MAINT_INTID || intid == KICK_INTID) {
			deactivate(iar);
			refill(vgic);
			/* an SGI for a guest that isn't running here */
			for (g = 0; intid == KICK_INTID && g < HV_MAX_GUESTS; g++)
				if (g != vcpu->guest->id &&
				    arm64_vgicd_sgi_waiting(g, me))
					hv_vcpu_yield();
		} else if (intid == TICK_INTID) {
			/* the next one first: that takes the line down */
			arch_tick_start();
			deactivate(iar);
			hv_vcpu_yield();
		} else if (hv_mmio_phys_irq((unsigned int)intid)) {
			/* an emulated device's real one: h2's */
			deactivate(iar);
		} else if (intid < 16 || intid >= LPI_FIRST) {
			/* no others of these are made: guests' SGIs are virtual */
			deactivate(iar);
		} else if (intid < 32) {
			/* the CPU's own: the running vCPU's timers */
			inject(vcpu, lr_for(vcpu, intid) | LR_HW |
				     intid << LR_PINTID_SHIFT);
		} else {
			/* a device's: guest 0's, if it has a vCPU here at all */
			owner = hv_vcpu_owner();
			if (owner)
				inject(owner, lr_for(owner, intid) | LR_HW |
					      intid << LR_PINTID_SHIFT);
			else
				deactivate(iar);
		}
	}

	hv_vcpu_trap_done(frame);
}

/*
 * The guest makes an SGI (GICv3: ICC_SGI*R_EL1 trap to EL2 while IMO is
 * set): virtual, to its vCPUs on the CPUs it names.
 */
int arm64_vgic_sysreg(struct hv_trap_frame *frame)
{
	unsigned long iss = frame->cause & 0x1ffffff, rt = ISS_RT(iss);
	uint64_t v = rt == 31 ? 0 : frame->regs[rt];
	int g = hv_vcpu_current()->guest->id, me = hv_cpu_index(), slot;
	unsigned int list = v & 0xffff, intid = v >> 24 & 0xf, rs = v >> 44 & 0xf;
	hv_addr_t hw;

	if (!gic3 || (iss & ISS_READ))
		return 0;

	switch (iss & ISS_SYSREG_MASK) {
	case ISS_SYSREG(3, 0, 12, 11, 5):
	case ISS_SYSREG(3, 0, 12, 11, 6):
	case ISS_SYSREG(3, 0, 12, 11, 7):
		break;
	default:
		return 0;
	}

	for (slot = 0; slot < HV_MAX_CPUS; slot++) {
		if (!hv_cpus[slot].used)
			continue;
		hw = hv_cpus[slot].hwid;
		if (v >> 40 & 1) {			/* IRM: all but itself */
			if (slot == me)
				continue;
		} else if ((hw >> 32 & 0xff) != (v >> 48 & 0xff) ||
			   (hw >> 16 & 0xff) != (v >> 32 & 0xff) ||
			   (hw >> 8 & 0xff) != (v >> 16 & 0xff) ||
			   (hw >> 4 & 0xf) != rs ||
			   !(list >> (hw & 0xf) & 1)) {
			continue;
		}
		sgi_send(g, slot, intid, 0);
	}
	frame->pc += 4;
	return 1;
}

static int ppi(uint64_t lr)
{
	return (lr & LR_HW) && LR_PINTID(lr) >= 16 && LR_PINTID(lr) < 32;
}

/*
 * The running vCPU's interface, into vgic. The CPU's PPIs (its timers,
 * stopped by now) are the next vCPU's: deactivated, and dropped if the
 * guest hadn't taken them yet (a level one still due comes back when its
 * timer runs again); one it is handling is made active again when it
 * comes back.
 */
void arm64_vgic_save(struct arm64_vgic *vgic)
{
	uint64_t lr;
	int i;

	if (!enabled)
		return;

	for (i = 0; i < nr_lrs; i++) {
		lr = lr_get(i);
		if ((lr & LR_STATE) && ppi(lr)) {
			deactivate(LR_PINTID(lr));
			lr = (lr & LR_ACTIVE) ? lr & ~LR_PENDING : 0;
		}
		vgic->lr[i] = lr;
		lr_put(i, 0);
	}

	if (gic3) {
		vgic->vmcr = SYSREG_READ(ICH_VMCR_EL2);
		for (i = 0; i < nr_aprs && i < 4; i++) {
			vgic->ap0r[i] = ich_apr(0, i, 0, 0);
			vgic->ap1r[i] = ich_apr(1, i, 0, 0);
		}
	} else {
		vgic->vmcr = *mmio(PLATFORM_GICH + GICH_VMCR);
		vgic->ap1r[0] = *mmio(PLATFORM_GICH + GICH_APR);
	}
	hcr_put(0);
}

void arm64_vgic_load(struct arm64_vgic *vgic)
{
	int i;

	if (!enabled)
		return;

	for (i = 0; i < nr_lrs; i++) {
		if ((vgic->lr[i] & LR_ACTIVE) && ppi(vgic->lr[i]))
			*mmio(ppi_regs[hv_cpu_index()] + GICx_ISACTIVER0) =
				1U << LR_PINTID(vgic->lr[i]);
		lr_put(i, vgic->lr[i]);
	}
	if (gic3) {
		SYSREG_WRITE(ICH_VMCR_EL2, vgic->vmcr);
		for (i = 0; i < nr_aprs && i < 4; i++) {
			ich_apr(0, i, 1, vgic->ap0r[i]);
			ich_apr(1, i, 1, vgic->ap1r[i]);
		}
	} else {
		*mmio(PLATFORM_GICH + GICH_VMCR) = (uint32_t)vgic->vmcr;
		*mmio(PLATFORM_GICH + GICH_APR) = vgic->ap1r[0];
	}
	refill(vgic);
}

/* GICv3: this CPU's redistributor, by its affinity; 0 if none */
static hv_addr_t redistributor(void)
{
#ifdef PLATFORM_GICR
	hv_addr_t rd = PLATFORM_GICR;
	uint64_t mpidr, aff, typer;
	int i;

	__asm__ volatile("mrs %0, MPIDR_EL1" : "=r"(mpidr));
	aff = (mpidr & 0xffffff) | (mpidr >> 32 & 0xff) << 24;
	for (i = 0; i < 64 * HV_MAX_CPUS; i++, rd += GICR_FRAME) {
		typer = *(volatile uint64_t *)(rd + GICR_TYPER);
		if (typer >> 32 == aff)
			return rd;
		if (typer & GICR_TYPER_LAST)
			break;
	}
#endif
	return 0;
}

/*
 * Once, on the boot CPU, before stage 2 is built: which GIC, and for
 * GICv2 the guest's GICC is GICV, and GICH none of its business.
 */
void arm64_vgic_setup(void)
{
	unsigned long pfr0, vtr;

	__asm__ volatile("mrs %0, ID_AA64PFR0_EL1" : "=r"(pfr0));
	gic3 = ((pfr0 >> 24) & 0xf) != 0;
	if (gic3) {
		vtr = SYSREG_READ(ICH_VTR_EL2);
		nr_lrs = ICH_VTR_LRS(vtr);
		nr_aprs = 1 << (ICH_VTR_PREBITS(vtr) - 5);
	} else {
		vtr = *mmio(PLATFORM_GICH + GICH_VTR);
		nr_lrs = GICH_VTR_LRS(vtr);
		nr_aprs = 1;
		hv_stage2_remap(PLATFORM_GICC, PLATFORM_GICC + PLATFORM_GICC_SIZE,
				PLATFORM_GICV);
		hv_stage2_hole(PLATFORM_GICH, PLATFORM_GICH + PLATFORM_GICH_SIZE);
	}
	if (nr_lrs > ARM64_VGIC_LRS)
		nr_lrs = ARM64_VGIC_LRS;
	if (nr_aprs > 4)
		nr_aprs = 4;
	enabled = 1;
	arm64_vgicd_setup(gic3);

	hv_console_write(gic3 ? "h2: vgic: GICv3, list registers: " :
				"h2: vgic: GICv2, list registers: ");
	hv_console_hex((hv_addr_t)nr_lrs);
	hv_console_putchar('\n');
}

/*
 * Per CPU (after ICC_SRE_EL2): h2's physical CPU interface takes every
 * priority, ends interrupts in two steps (EOImode 1), and gets its
 * maintenance interrupt; the virtual one starts off.
 */
void arm64_vgic_cpu_init(void)
{
	hv_addr_t rd;
	int i;

	if (!enabled)
		return;

	if (gic3) {
		SYSREG_WRITE(ICC_PMR_EL1, 0xff);
		SYSREG_WRITE(ICC_BPR1_EL1, 0);
		SYSREG_WRITE(ICC_CTLR_EL1, SYSREG_READ(ICC_CTLR_EL1) | ICC_CTLR_EOIMODE);
		SYSREG_WRITE(ICC_IGRPEN1_EL1, 1);
		rd = redistributor();
		if (!rd) {
			hv_console_write("h2: vgic: no redistributor for this CPU\n");
			return;
		}
		ppi_regs[hv_cpu_index()] = rd + GICR_SGI_BASE;
		*mmio(rd + GICR_SGI_BASE + GICx_IGROUPR0) = ~0U;
		*mmio(rd + GICR_WAKER) &= ~2U;
		while (*mmio(rd + GICR_WAKER) & 4)
			;
	} else {
		*mmio(PLATFORM_GICC + GICC_PMR) = 0xff;
		*mmio(PLATFORM_GICC + GICC_CTLR) = GICC_CTLR_ENABLE | GICC_CTLR_EOIMODE;
		ppi_regs[hv_cpu_index()] = PLATFORM_GICD;
	}
	/* nothing of the guests' yet; priorities mean nothing for real */
	for (i = 0; i < 8; i++)
		*mmio(ppi_regs[hv_cpu_index()] + GICx_IPRIORITYR0 + 4 * i) = 0x80808080;
	*mmio(ppi_regs[hv_cpu_index()] + GICx_ICENABLER0) =
		~(H2_PPIS | 1U << KICK_INTID);
	*mmio(ppi_regs[hv_cpu_index()] + GICx_ISENABLER0) =
		H2_PPIS | 1U << KICK_INTID;
	ppis_on[hv_cpu_index()] = 0;
	for (i = 0; i < nr_lrs; i++)
		lr_put(i, 0);
	hcr_put(0);
	__asm__ volatile("isb" ::: "memory");
}

int arm64_vgic_gic3(void)
{
	return gic3;
}

/* whether physical IRQs come to h2 */
int arm64_vgic_enabled(void)
{
	return enabled;
}
