#include <arch/boot.h>
#include <arch/stage2.h>
#include <arch/virq.h>
#include <arch/vplic.h>
#include <hv/hv.h>
#include <hv/stage2.h>

extern char __riscv64_trap_vector[];
extern char __stack_top[];
extern char __guest_stack_top[];
extern char __image_start[], __image_end[];
extern void riscv64_guest_entry(void);

#define HSTATUS_SPV	(1UL << 7)
#define HSTATUS_SPVP	(1UL << 8)
#define SSTATUS_SPP	(1UL << 8)
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
 * Enter VS-mode at entry with a0, a1 = arg0, arg1; the guest may read the
 * cycle, time and instret counters, gets its exceptions and interrupts,
 * and sees memory through stage 2. While the guest runs, sscratch holds
 * h2's stack on this hart, for the trap vector.
 */
void riscv64_enter_guest(unsigned long entry, unsigned long stack,
			 unsigned long host_stack, unsigned long arg0,
			 unsigned long arg1)
{
	riscv64_virq_init();
	riscv64_stage2_enable();

	/* bound to a0/a1 only after the call, which may clobber them */
	register unsigned long a0 __asm__("a0") = arg0;
	register unsigned long a1 __asm__("a1") = arg1;

	__asm__ volatile(
		"csrw stvec, %[trap]\n"
		"csrw vsatp, zero\n"
		"csrw hcounteren, %[counters]\n"
		"csrw hedeleg, %[exceptions]\n"
		"csrw sepc, %[entry]\n"
		"csrw sscratch, %[host_stack]\n"
		"csrr t0, hstatus\n"
		"ori t0, t0, %[spv]\n"
		"ori t0, t0, %[spvp]\n"
		"csrw hstatus, t0\n"
		"csrr t0, sstatus\n"
		"ori t0, t0, %[spp]\n"
		"csrw sstatus, t0\n"
		"mv sp, %[stack]\n"
		"sret\n"
		:
		: [trap] "r"(__riscv64_trap_vector),
		  [entry] "r"(entry),
		  [stack] "r"(stack),
		  [host_stack] "r"(host_stack),
		  [spv] "i"(HSTATUS_SPV),
		  [spvp] "i"(HSTATUS_SPVP),
		  [spp] "i"(SSTATUS_SPP),
		  [counters] "r"(HCOUNTEREN_CY_TM_IR),
		  [exceptions] "r"(HEDELEG_GUEST),
		  "r"(a0), "r"(a1)
		: "t0", "memory");
	__builtin_unreachable();
}

void riscv64_boot(unsigned long hartid, unsigned long dtb)
{
	hv_addr_t entry;
	int loaded;

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
	if (loaded >= 0 && riscv64_stage2_init() < 0)
		loaded = -1;

	switch (loaded) {
	case 1:
		__asm__ volatile("fence.i" ::: "memory");
		riscv64_enter_guest(entry, 0, (unsigned long)__stack_top,
				    hartid, dtb);
	case 0:
		riscv64_enter_guest((unsigned long)riscv64_guest_entry,
				    (unsigned long)__guest_stack_top,
				    (unsigned long)__stack_top, hartid, dtb);
	default:
		for (;;)
			__asm__ volatile("wfi");
	}
}
