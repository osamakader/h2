#include <arch/boot.h>
#include <hv/hv.h>

extern char __riscv64_trap_vector[];
extern char __stack_top[];
extern char __guest_stack_top[];
extern void riscv64_guest_entry(void);

#define HSTATUS_SPV	(1UL << 7)
#define HSTATUS_SPVP	(1UL << 8)
#define SSTATUS_SPP	(1UL << 8)

/*
 * Enter VS-mode at entry with a0, a1 = arg0, arg1; while the guest runs,
 * sscratch holds h2's stack on this hart, for the trap vector.
 */
void riscv64_enter_guest(unsigned long entry, unsigned long stack,
			 unsigned long host_stack, unsigned long arg0,
			 unsigned long arg1)
{
	register unsigned long a0 __asm__("a0") = arg0;
	register unsigned long a1 __asm__("a1") = arg1;

	__asm__ volatile(
		"csrw stvec, %[trap]\n"
		"csrw hgatp, zero\n"
		"csrw vsatp, zero\n"
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
		  "r"(a0), "r"(a1)
		: "t0", "memory");
	__builtin_unreachable();
}

void riscv64_boot(unsigned long hartid, unsigned long dtb)
{
	hv_addr_t entry;

	dtb = hv_main(dtb);

	switch (hv_load_guest(dtb, &entry)) {
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
