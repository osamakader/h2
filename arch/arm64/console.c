#include <arch/platform.h>
#include <hv/hv.h>

/* the PL011 h2 writes to, and reads the guest's input from */
#define PL011_DR		0x00
#define PL011_FR		0x18
#define PL011_FR_RXFE		(1U << 4)
#define PL011_FR_TXFF		(1U << 5)
#define PL011_CR		0x30
#define PL011_CR_RXE		(1U << 9)
#define PL011_IMSC		0x38
#define PL011_ICR		0x44
#define PL011_INT_RX		(1U << 4)
#define PL011_INT_RT		(1U << 6)

static volatile uint32_t *const uart0 = (volatile uint32_t *)PLATFORM_UART0;

#define REG(off)	uart0[(off) / sizeof(uint32_t)]

void hv_console_out(char ch)
{
	while (REG(PL011_FR) & PL011_FR_TXFF)
		;
	REG(PL011_DR) = (uint8_t)ch;
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
	if (REG(PL011_FR) & PL011_FR_RXFE)
		return -1;
	return (int)(REG(PL011_DR) & 0xff);
}

/* its receive interrupt on (as the firmware set it up, else) */
void hv_console_rx_init(void)
{
	REG(PL011_CR) |= PL011_CR_RXE;
	REG(PL011_ICR) = PL011_INT_RX | PL011_INT_RT;
	REG(PL011_IMSC) |= PL011_INT_RX | PL011_INT_RT;
}

/* before draining it: what comes after raises it again */
void hv_console_rx_ack(void)
{
	REG(PL011_ICR) = PL011_INT_RX | PL011_INT_RT;
}
