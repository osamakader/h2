#include <arch/platform.h>
#include <arch/trap.h>
#include <arch/vgic.h>
#include <hv/cpu.h>
#include <hv/hv.h>
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

static uint64_t running_priority(void)
{
	return (gic3 ? SYSREG_READ(ICC_RPR_EL1) :
		       *mmio(PLATFORM_GICC + GICC_RPR)) & 0xff;
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
 * The queue, into the list registers, as far as it goes: if they run
 * out, h2 wants the underflow maintenance interrupt.
 */
static void refill(struct arm64_vgic *vgic)
{
	int i, n = 0, full = 0, r;
	uint64_t lr;

	/* done with: the ones that asked for maintenance need clearing */
	for (i = 0; i < nr_lrs; i++) {
		lr = lr_get(i);
		if (lr && !(lr & LR_STATE))
			lr_put(i, 0);
	}

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

static void inject(struct arm64_vgic *vgic, uint64_t lr)
{
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
	refill(vgic);
}

/* a physical IRQ, taken from the guest: on to the running vCPU */
void arm64_handle_irq(struct hv_trap_frame *frame)
{
	struct arm64_vgic *vgic = &hv_vcpu_current()->arch.vgic;
	uint64_t iar = ack(), intid, lr;

	intid = gic3 ? iar & 0xffffff : iar & 0x3ff;
	if (!SPURIOUS(intid)) {
		lr = intid | running_priority() << LR_PRIO_SHIFT | LR_PENDING |
		     (gic3 ? LR_GROUP1 : 0);
		priority_drop(iar);

		if (intid == MAINT_INTID) {
			deactivate(iar);
			refill(vgic);
		} else if (intid < 16 || intid >= LPI_FIRST) {
			/* edge: h2's part is done; GICv2 says who sent it */
			deactivate(iar);
			if (!gic3)
				lr |= (iar >> 10 & 7) << LR_PINTID_SHIFT;
			inject(vgic, lr);
		} else {
			inject(vgic, lr | LR_HW | intid << LR_PINTID_SHIFT);
		}
	}

	hv_vcpu_trap_done(frame);
}

/*
 * The guest makes an SGI (GICv3: ICC_SGI*R_EL1 trap to EL2 while IMO is
 * set). Its vCPUs have their CPUs' affinities: make it as it is.
 */
int arm64_vgic_sysreg(struct hv_trap_frame *frame)
{
	unsigned long iss = frame->cause & 0x1ffffff, rt = ISS_RT(iss);
	uint64_t v = rt == 31 ? 0 : frame->regs[rt];

	if (!gic3 || (iss & ISS_READ))
		return 0;

	switch (iss & ISS_SYSREG_MASK) {
	case ISS_SYSREG(3, 0, 12, 11, 5):
		SYSREG_WRITE(ICC_SGI1R_EL1, v);
		break;
	case ISS_SYSREG(3, 0, 12, 11, 6):
		SYSREG_WRITE(ICC_ASGI1R_EL1, v);
		break;
	case ISS_SYSREG(3, 0, 12, 11, 7):
		SYSREG_WRITE(ICC_SGI0R_EL1, v);
		break;
	default:
		return 0;
	}
	__asm__ volatile("isb" ::: "memory");
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
		*mmio(rd + GICR_SGI_BASE + GICx_IGROUPR0) |= 1U << MAINT_INTID;
	} else {
		*mmio(PLATFORM_GICC + GICC_PMR) = 0xff;
		*mmio(PLATFORM_GICC + GICC_CTLR) = GICC_CTLR_ENABLE | GICC_CTLR_EOIMODE;
		ppi_regs[hv_cpu_index()] = PLATFORM_GICD;
	}
	*mmio(ppi_regs[hv_cpu_index()] + GICx_ISENABLER0) = 1U << MAINT_INTID;
	for (i = 0; i < nr_lrs; i++)
		lr_put(i, 0);
	hcr_put(0);
	__asm__ volatile("isb" ::: "memory");
}

/* whether physical IRQs come to h2 */
int arm64_vgic_enabled(void)
{
	return enabled;
}
