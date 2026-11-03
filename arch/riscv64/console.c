#include <hv/hv.h>

/* the 16550 h2 writes to, and reads the guest's input from */
#define RISCV_UART0_BASE	0x10000000UL
#define UART_RBR		0x00
#define UART_THR		0x00
#define UART_IER		0x01
#define UART_IER_RX		(1U << 0)
#define UART_FCR		0x02
#define UART_FCR_FIFO		(1U << 0)
#define UART_LSR		0x05
#define UART_LSR_DR		(1U << 0)
#define UART_LSR_THRE		(1U << 5)

static volatile uint8_t *const uart0 = (volatile uint8_t *)RISCV_UART0_BASE;

void hv_console_out(char ch)
{
	while ((uart0[UART_LSR] & UART_LSR_THRE) == 0)
		;
	uart0[UART_THR] = (uint8_t)ch;
}

void hv_console_putchar(char ch)
{
	if (ch == '\n')
		hv_console_out('\r');
	hv_console_out(ch);
}

/* a received character, or -1 */
int hv_console_getc(void)
{
	if (!(uart0[UART_LSR] & UART_LSR_DR))
		return -1;
	return uart0[UART_RBR];
}

/* its receive interrupt on (the firmware set the line up) */
void hv_console_rx_init(void)
{
	uart0[UART_FCR] = UART_FCR_FIFO;
	uart0[UART_IER] = UART_IER_RX;
}

/* reading it empty is what clears its interrupt */
void hv_console_rx_ack(void)
{
}
