#include <hv/hv.h>

extern char __riscv64_trap_vector[];
extern char __stack_top[];
extern char __guest_stack_top[];
extern void riscv64_guest_entry(void);

#define HSTATUS_SPV	(1UL << 7)
#define HSTATUS_SPVP	(1UL << 8)
#define SSTATUS_SPP	(1UL << 8)

static inline void enter_vs_guest(unsigned long entry, unsigned long stack)
{
	(void)stack;

	__asm__ volatile(
		"csrw stvec, %[trap]\n"
		"csrw hgatp, zero\n"
		"csrw vsatp, zero\n"
		"csrw vsepc, %[entry]\n"
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
		  [host_stack] "r"(__stack_top),
		  [spv] "i"(HSTATUS_SPV),
		  [spvp] "i"(HSTATUS_SPVP),
		  [spp] "i"(SSTATUS_SPP)
		: "t0", "memory");
}

void riscv64_boot(void)
{
	hv_main();
	enter_vs_guest((unsigned long)riscv64_guest_entry,
		       (unsigned long)__guest_stack_top);
}
