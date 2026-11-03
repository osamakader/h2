#include <arch/boot.h>
#include <arch/csr.h>
#include <arch/stage2.h>
#include <arch/virq.h>
#include <arch/vplic.h>
#include <hv/cpu.h>
#include <hv/hv.h>
#include <hv/stage2.h>
#include <hv/vcpu.h>
#include <hv/vuart.h>

extern char __riscv64_trap_vector[];
extern char __guest_stack_top[];
extern char __image_start[], __image_end[];
extern void riscv64_guest_entry(void);

#define QEMU_VIRT_UART0		0x10000000UL
#define QEMU_VIRT_UART0_IRQ	10		/* its PLIC source */
#define HCOUNTEREN_CY_TM_IR	0x7UL	/* cycle, time, instret */

/*
 * The guest's own exceptions, to the guest: misaligned and access faults,
 * illegal instructions, breakpoints, user ecalls, page faults. Not its
 * ecalls to the SBI, nor guest page faults or virtual instructions.
 */
#define HEDELEG_GUEST	((1UL << 0) | (1UL << 1) | (1UL << 2) | (1UL << 3) | \
			 (1UL << 4) | (1UL << 5) | (1UL << 6) | (1UL << 7) | \
			 (1UL << 8) | (1UL << 12) | (1UL << 13) | (1UL << 15))

/*
 * This hart, before it runs vCPUs: they may read the cycle, time and
 * instret counters, get their exceptions and interrupts, and see memory
 * through stage 2; h2 takes the traps.
 */
void riscv64_hart_init(void)
{
	riscv64_virq_init();
	riscv64_stage2_enable();
	csr_write(stvec, (hv_addr_t)__riscv64_trap_vector);
	csr_write(hcounteren, HCOUNTEREN_CY_TM_IR);
	csr_write(hedeleg, HEDELEG_GUEST);
}

/* a hart the guest started, on h2 slot slot: on to the guest, for good */
void riscv64_run_guest(int slot, hv_addr_t entry, hv_addr_t arg0,
		       hv_addr_t arg1)
{
	struct hv_vcpu *vcpu;

	riscv64_hart_init();
	vcpu = hv_vcpu_new(slot, entry, 0, arg0, arg1);
	if (vcpu)
		hv_vcpu_run(vcpu);
	hv_console_write("h2: no room for another vCPU\n");
	for (;;)
		__asm__ volatile("wfi");
}

void riscv64_boot(unsigned long hartid, unsigned long dtb)
{
	struct hv_vcpu *vcpu;
	hv_addr_t entry;
	int loaded;

	hv_cpu_boot(hartid);
	dtb = hv_main(dtb);
	if (!riscv64_stage2_supported()) {
		hv_console_write("h2: no Sv39x4 for stage 2\n");
		loaded = -1;
	} else {
		loaded = hv_load_guest(dtb, &entry);
	}

	/* h2's pages are no business of a guest of its own (not built in) */
	if (loaded == 1)
		hv_stage2_hole((hv_addr_t)__image_start,
				    ((hv_addr_t)__image_end + 0xfff) & ~0xfffUL);
	riscv64_vplic_init();
	/* the console UART is h2's now; the guest gets a virtual one */
	if (loaded >= 0)
		hv_vuart_16550_init(QEMU_VIRT_UART0, QEMU_VIRT_UART0_IRQ);
	if (loaded >= 0 && riscv64_stage2_init() < 0)
		loaded = -1;

	if (loaded == 1) {
		__asm__ volatile("fence.i" ::: "memory");
		riscv64_run_guest(0, entry, hartid, dtb);
	} else if (loaded == 0) {
		riscv64_hart_init();
		vcpu = hv_vcpu_new(0, (hv_addr_t)riscv64_guest_entry,
				   (hv_addr_t)__guest_stack_top, hartid, dtb);
		if (vcpu)
			hv_vcpu_run(vcpu);
	}
	for (;;)
		__asm__ volatile("wfi");
}
