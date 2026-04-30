#include <hv/hv.h>

static void guest_putc(char ch)
{
	register unsigned long x0 __asm__("x0") = HV_HCALL_PUTC;
	register unsigned long x1 __asm__("x1") = (unsigned long)ch;

	__asm__ volatile("hvc #0" : "+r"(x0) : "r"(x1) : "memory");
}

void arm64_guest_entry(void)
{
	const char *message = "guest: hello from EL1 via HVC\n";

	while (*message)
		guest_putc(*message++);

	for (;;)
		__asm__ volatile("wfe");
}
