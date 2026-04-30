#include <hv/hv.h>

static void guest_putc(char ch)
{
	register unsigned long a0 __asm__("a0") = HV_HCALL_PUTC;
	register unsigned long a1 __asm__("a1") = (unsigned long)ch;

	__asm__ volatile("ecall" : "+r"(a0) : "r"(a1) : "memory");
}

void riscv64_guest_entry(void)
{
	const char *message = "guest: hello from VS via ECALL\n";

	while (*message)
		guest_putc(*message++);

	for (;;)
		__asm__ volatile("wfi");
}
