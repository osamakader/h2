#include <stdint.h>

#define RISCV_UART0_BASE	0x10000000UL
#define UART_THR		0x00
#define UART_LSR		0x05
#define UART_LSR_THRE		(1U << 5)

static volatile uint8_t *const uart0 = (volatile uint8_t *)RISCV_UART0_BASE;

void hv_console_putchar(char ch)
{
	if (ch == '\n')
		hv_console_putchar('\r');

	while ((uart0[UART_LSR] & UART_LSR_THRE) == 0)
		;

	uart0[UART_THR] = (uint8_t)ch;
}
