#include <stdint.h>

#define ARM64_UART0_BASE	0x09000000UL
#define PL011_FR		0x18
#define PL011_FR_TXFF		(1U << 5)

static volatile uint32_t *const uart0 = (volatile uint32_t *)ARM64_UART0_BASE;

void hv_console_putchar(char ch)
{
	if (ch == '\n')
		hv_console_putchar('\r');

	while (uart0[PL011_FR / sizeof(uint32_t)] & PL011_FR_TXFF)
		;

	uart0[0] = (uint32_t)ch;
}
