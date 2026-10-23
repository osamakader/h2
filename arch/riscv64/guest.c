#include <arch/trap.h>
#include <hv/hv.h>

static void guest_putc(char ch)
{
	register unsigned long a0 __asm__("a0") = (unsigned long)ch;
	register unsigned long a1 __asm__("a1");
	register unsigned long a6 __asm__("a6") = HV_HCALL_PUTC;
	register unsigned long a7 __asm__("a7") = HV_SBI_EXT;

	__asm__ volatile("ecall" : "+r"(a0), "=r"(a1)
			 : "r"(a6), "r"(a7) : "memory");
}

void riscv64_guest_entry(void)
{
	const char *message = "guest: hello from VS via ECALL\n";

	while (*message)
		guest_putc(*message++);

	for (;;)
		__asm__ volatile("wfi");
}
