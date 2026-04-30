#include <hv/hv.h>

extern char __arm64_vectors[];
extern char __guest_stack_top[];
extern void arm64_guest_entry(void);

static inline unsigned long read_current_el(void)
{
	unsigned long value;
	__asm__ volatile("mrs %0, CurrentEL" : "=r"(value));
	return (value >> 2) & 0x3;
}

static inline void write_vbar_el2(unsigned long value)
{
	__asm__ volatile("msr VBAR_EL2, %0; isb" :: "r"(value) : "memory");
}

static inline void enter_el1_guest(unsigned long entry, unsigned long stack)
{
	const unsigned long hcr_rw = 1UL << 31;
	const unsigned long spsr_el1h = 0x5;
	const unsigned long daif_mask = 0xfUL << 6;
	const unsigned long sctlr_el1_res1 = (1UL << 29) | (1UL << 28) |
					     (1UL << 23) | (1UL << 22) |
					     (1UL << 20) | (1UL << 11);

	__asm__ volatile(
		"msr HCR_EL2, %[hcr]\n"
		"msr SCTLR_EL1, %[sctlr]\n"
		"msr CPACR_EL1, xzr\n"
		"msr TTBR0_EL1, xzr\n"
		"msr TTBR1_EL1, xzr\n"
		"msr TCR_EL1, xzr\n"
		"msr MAIR_EL1, xzr\n"
		"msr SP_EL1, %[stack]\n"
		"msr ELR_EL2, %[entry]\n"
		"msr SPSR_EL2, %[spsr]\n"
		"isb\n"
		"eret\n"
		:
		: [hcr] "r"(hcr_rw),
		  [sctlr] "r"(sctlr_el1_res1),
		  [stack] "r"(stack),
		  [entry] "r"(entry),
		  [spsr] "r"(spsr_el1h | daif_mask)
		: "memory");
}

void arm64_boot(void)
{
	if (read_current_el() != 2)
		for (;;)
			__asm__ volatile("wfe");

	hv_main();
	write_vbar_el2((unsigned long)__arm64_vectors);
	enter_el1_guest((unsigned long)arm64_guest_entry,
			(unsigned long)__guest_stack_top);
}
