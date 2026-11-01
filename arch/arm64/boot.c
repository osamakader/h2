#include <arch/boot.h>
#include <arch/platform.h>
#include <arch/stage2.h>
#include <hv/cpu.h>
#include <hv/hv.h>
#include <hv/stage2.h>
#include <hv/vcpu.h>

extern char __arm64_vectors[];
extern char __guest_stack_top[];
extern char __image_start[], __image_end[];
extern void arm64_guest_entry(void);

#define HCR_EL2_RW		(1UL << 31)
#define HCR_EL2_VM		(1UL << 0)	/* stage 2 */
#define HCR_EL2_TSC		(1UL << 19)	/* guest SMCs trap to h2 */
#define CNTHCTL_EL1PCTEN	(1UL << 0)
#define CNTHCTL_EL1PCEN		(1UL << 1)
#define CPTR_EL2_RES1		0x32ffUL	/* RES1 bits; TFP, TTA, TCPAC clear */
#define ICC_SRE_EL2_VALUE	0xfUL		/* SRE, DFB, DIB, Enable */
#define ID_AA64PFR0_GIC(v)	(((v) >> 24) & 0xf)


static inline unsigned long read_current_el(void)
{
	unsigned long value;
	__asm__ volatile("mrs %0, CurrentEL" : "=r"(value));
	return (value >> 2) & 0x3;
}

/*
 * Per CPU: EL1 sees memory through stage 2, gets the physical timer and
 * counter, FP/SIMD and (with GICv3) the GIC system registers without
 * trapping, and sees this CPU's MIDR and MPIDR.
 */
void arm64_el2_init(void)
{
	unsigned long pfr0, value;

	__asm__ volatile("msr VBAR_EL2, %0" :: "r"(__arm64_vectors));
	arm64_stage2_enable();
	__asm__ volatile("msr HCR_EL2, %0" ::
			 "r"(HCR_EL2_RW | HCR_EL2_TSC | HCR_EL2_VM));

	__asm__ volatile("mrs %0, CNTHCTL_EL2" : "=r"(value));
	value |= CNTHCTL_EL1PCTEN | CNTHCTL_EL1PCEN;
	__asm__ volatile("msr CNTHCTL_EL2, %0" :: "r"(value));
	__asm__ volatile("msr CNTVOFF_EL2, xzr");
	__asm__ volatile("msr CPTR_EL2, %0" :: "r"(CPTR_EL2_RES1));
	__asm__ volatile("msr HSTR_EL2, xzr");

	/* ICC_SRE_EL2 is UNDEFINED without a GICv3 CPU interface */
	__asm__ volatile("mrs %0, ID_AA64PFR0_EL1" : "=r"(pfr0));
	if (ID_AA64PFR0_GIC(pfr0))
		__asm__ volatile("msr S3_4_C12_C9_5, %0" :: "r"(ICC_SRE_EL2_VALUE));

	__asm__ volatile("mrs %0, MIDR_EL1" : "=r"(value));
	__asm__ volatile("msr VPIDR_EL2, %0" :: "r"(value));
	__asm__ volatile("mrs %0, MPIDR_EL1" : "=r"(value));
	__asm__ volatile("msr VMPIDR_EL2, %0" :: "r"(value));
	__asm__ volatile("isb" ::: "memory");
}

void arm64_boot(unsigned long dtb)
{
	hv_addr_t entry, mpidr;
	struct hv_vcpu *vcpu = 0;
	int loaded;

	if (read_current_el() != 2) {
		hv_console_write("h2: started in EL");
		hv_console_putchar('0' + (char)read_current_el());
		hv_console_write(", not EL2 (qemu: -machine virtualization=on)\n");
		for (;;)
			__asm__ volatile("wfe");
	}

	__asm__ volatile("mrs %0, MPIDR_EL1" : "=r"(mpidr));
	hv_cpu_boot(mpidr & MPIDR_AFFINITY);

	/* report faults in h2 from here on */
	__asm__ volatile("msr VBAR_EL2, %0; isb" :: "r"(__arm64_vectors));

	/* qemu virt: ELF kernels get no x0, but the DTB at the start of RAM */
	if (!dtb)
		dtb = PLATFORM_RAM_BASE;
	dtb = hv_main(dtb);
	loaded = hv_load_guest(dtb, &entry);

	/* h2's pages are no business of a guest of its own (not built in) */
	if (loaded == 1)
		hv_stage2_hole((hv_addr_t)__image_start,
			       ((hv_addr_t)__image_end + 0xfff) & ~0xfffUL);
	if (loaded >= 0 && arm64_stage2_init() < 0)
		loaded = -1;
	if (loaded >= 0)
		arm64_el2_init();
#ifdef PLATFORM_SPIN_TABLE
	if (loaded >= 0)
		arm64_spin_take(mpidr);
#endif

	if (loaded == 1) {
		/* the guest's code went out through the data side */
		__asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
		vcpu = hv_vcpu_new(0, entry, 0, dtb, 0);
	} else if (loaded == 0) {
		vcpu = hv_vcpu_new(0, (hv_addr_t)arm64_guest_entry,
				   (hv_addr_t)__guest_stack_top, dtb, 0);
	}
	if (vcpu)
		hv_vcpu_run(vcpu);
	for (;;)
		__asm__ volatile("wfe");
}

/* a CPU the guest started, on h2 slot slot: on to the guest, for good */
void arm64_run_guest(int slot, unsigned long entry, unsigned long arg)
{
	struct hv_vcpu *vcpu = hv_vcpu_new(slot, entry, 0, arg, 0);

	if (vcpu)
		hv_vcpu_run(vcpu);
	hv_console_write("h2: no room for another vCPU\n");
	for (;;)
		__asm__ volatile("wfe");
}
