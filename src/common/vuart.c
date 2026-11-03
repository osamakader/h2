#include <hv/cpu.h>
#include <hv/hv.h>
#include <hv/mmio.h>
#include <hv/vuart.h>

/*
 * Virtual UARTs: an ARM PL011 and a 16550, as much of each as a kernel's
 * console driver uses. Sending is immediate (the transmitter is always
 * empty); received characters wait in a FIFO, filled from the real
 * UART's input when it interrupts and whenever the guest looks.
 */

#define RX_FIFO		64

struct vuart {
	struct hv_mmio_dev dev;
	struct hv_lock lock;
	unsigned int irq;
	uint8_t rx[RX_FIFO];
	unsigned int rx_head, rx_count;
	void (*received)(struct vuart *u);	/* the model's reaction */
	void (*update)(struct vuart *u);	/* its interrupt line */
};

/* the real UART's input, into the FIFO (dropped once it is full) */
static void pull(struct vuart *u)
{
	int c, got = 0;

	while ((c = hv_console_getc()) >= 0) {
		if (u->rx_count < RX_FIFO) {
			u->rx[(u->rx_head + u->rx_count++) % RX_FIFO] = (uint8_t)c;
			got = 1;
		}
	}
	if (got)
		u->received(u);
}

static int pop(struct vuart *u)
{
	int c;

	if (!u->rx_count)
		return -1;
	c = u->rx[u->rx_head];
	u->rx_head = (u->rx_head + 1) % RX_FIFO;
	u->rx_count--;
	return c;
}

/* the real UART's receive interrupt */
static void rx_irq(void *ctx)
{
	struct vuart *u = ctx;

	hv_lock(&u->lock);
	hv_console_rx_ack();
	pull(u);
	u->update(u);
	hv_unlock(&u->lock);
}

/* ARM PL011 */

#define PL011_DR	0x000
#define PL011_RSR	0x004
#define PL011_FR	0x018
#define PL011_IBRD	0x024
#define PL011_FBRD	0x028
#define PL011_LCR_H	0x02c
#define PL011_CR	0x030
#define PL011_IFLS	0x034
#define PL011_IMSC	0x038
#define PL011_RIS	0x03c
#define PL011_MIS	0x040
#define PL011_ICR	0x044
#define PL011_DMACR	0x048
#define PL011_ID	0xfe0
#define FR_RXFE		(1U << 4)
#define FR_RXFF		(1U << 6)
#define FR_TXFE		(1U << 7)
#define INT_RX		(1U << 4)
#define INT_TX		(1U << 5)
#define INT_RT		(1U << 6)

static struct pl011 {
	struct vuart u;
	uint32_t ibrd, fbrd, lcr_h, cr, ifls, imsc, ris, dmacr;
} pl011;

static const uint8_t pl011_id[8] = { 0x11, 0x10, 0x14, 0x00, 0x0d, 0xf0, 0x05, 0xb1 };

static void pl011_received(struct vuart *u)
{
	(void)u;
	pl011.ris |= INT_RX | INT_RT;
}

static void pl011_update(struct vuart *u)
{
	hv_virq_line(u->irq, (pl011.ris & pl011.imsc) != 0);
}

static uint64_t pl011_read(void *ctx, hv_addr_t off, int size)
{
	struct vuart *u = ctx;
	uint32_t v = 0;
	int c;

	(void)size;
	hv_lock(&u->lock);
	switch (off) {
	case PL011_DR:
		pull(u);
		c = pop(u);
		v = c < 0 ? 0 : (uint32_t)c;
		if (!u->rx_count)
			pl011.ris &= ~(INT_RX | INT_RT);
		break;
	case PL011_FR:
		pull(u);
		v = FR_TXFE | (u->rx_count ? 0 : FR_RXFE) |
		    (u->rx_count == RX_FIFO ? FR_RXFF : 0);
		break;
	case PL011_IBRD:	v = pl011.ibrd; break;
	case PL011_FBRD:	v = pl011.fbrd; break;
	case PL011_LCR_H:	v = pl011.lcr_h; break;
	case PL011_CR:		v = pl011.cr; break;
	case PL011_IFLS:	v = pl011.ifls; break;
	case PL011_IMSC:	v = pl011.imsc; break;
	case PL011_RIS:		v = pl011.ris; break;
	case PL011_MIS:		v = pl011.ris & pl011.imsc; break;
	case PL011_DMACR:	v = pl011.dmacr; break;
	default:
		if (off >= PL011_ID && off < PL011_ID + 0x20 && !(off & 3))
			v = pl011_id[(off - PL011_ID) / 4];
		break;
	}
	u->update(u);
	hv_unlock(&u->lock);
	return v;
}

static void pl011_write(void *ctx, hv_addr_t off, int size, uint64_t value)
{
	struct vuart *u = ctx;
	uint32_t v = (uint32_t)value;

	(void)size;
	hv_lock(&u->lock);
	switch (off) {
	case PL011_DR:
		hv_console_out((char)v);
		pl011.ris |= INT_TX;
		break;
	case PL011_IBRD:	pl011.ibrd = v & 0xffff; break;
	case PL011_FBRD:	pl011.fbrd = v & 0x3f; break;
	case PL011_LCR_H:
		/* turning the FIFOs off flushes them */
		if ((pl011.lcr_h & ~v) & (1U << 4))
			u->rx_count = 0;
		pl011.lcr_h = v & 0xff;
		break;
	case PL011_CR:		pl011.cr = v & 0xffff; break;
	case PL011_IFLS:	pl011.ifls = v & 0x3f; break;
	case PL011_IMSC:	pl011.imsc = v & 0x7ff; break;
	case PL011_ICR:		pl011.ris &= ~v; break;
	case PL011_DMACR:	pl011.dmacr = v & 7; break;
	default:
		break;
	}
	u->update(u);
	hv_unlock(&u->lock);
}

void hv_vuart_pl011_init(hv_addr_t base, hv_addr_t size, unsigned int irq)
{
	pl011.cr = (1U << 9) | (1U << 8);	/* RXE, TXE */
	pl011.ifls = 0x12;
	pl011.u.irq = irq;
	pl011.u.received = pl011_received;
	pl011.u.update = pl011_update;
	pl011.u.dev = (struct hv_mmio_dev){
		.base = base, .size = size, .ctx = &pl011.u,
		.read = pl011_read, .write = pl011_write,
		.phys_irq = irq, .irq = rx_irq,
	};
	hv_mmio_register(&pl011.u.dev);
	hv_console_rx_init();
}

/* 16550 (byte registers) */

#define U_RBR		0	/* THR when written, DLL with DLAB */
#define U_IER		1	/* DLM with DLAB */
#define U_IIR		2	/* FCR when written */
#define U_LCR		3
#define U_MCR		4
#define U_LSR		5
#define U_MSR		6
#define U_SCR		7
#define IER_RX		(1U << 0)
#define IER_THRE	(1U << 1)
#define IIR_NONE	0x01
#define IIR_THRE	0x02
#define IIR_RX		0x04
#define IIR_FIFO	0xc0
#define FCR_FIFO	(1U << 0)
#define FCR_RX_CLEAR	(1U << 1)
#define LCR_DLAB	(1U << 7)
#define LSR_DR		(1U << 0)
#define LSR_THRE	(1U << 5)
#define LSR_TEMT	(1U << 6)
#define MSR_CTS_DSR_DCD	0xb0

static struct ns16550 {
	struct vuart u;
	uint8_t ier, lcr, mcr, scr, dll, dlm, fifo;
	int thre;		/* the THR-empty interrupt, until IIR says so */
} ns;

static void ns_received(struct vuart *u)
{
	(void)u;
}

static void ns_update(struct vuart *u)
{
	hv_virq_line(u->irq, ((ns.ier & IER_RX) && u->rx_count) ||
			     ((ns.ier & IER_THRE) && ns.thre));
}

static uint64_t ns_read(void *ctx, hv_addr_t off, int size)
{
	struct vuart *u = ctx;
	uint8_t v = 0;
	int c;

	(void)size;
	hv_lock(&u->lock);
	switch (off) {
	case U_RBR:
		if (ns.lcr & LCR_DLAB) {
			v = ns.dll;
			break;
		}
		pull(u);
		c = pop(u);
		v = c < 0 ? 0 : (uint8_t)c;
		break;
	case U_IER:
		v = (ns.lcr & LCR_DLAB) ? ns.dlm : ns.ier;
		break;
	case U_IIR:
		if ((ns.ier & IER_RX) && u->rx_count) {
			v = IIR_RX;
		} else if ((ns.ier & IER_THRE) && ns.thre) {
			v = IIR_THRE;
			ns.thre = 0;
		} else {
			v = IIR_NONE;
		}
		v |= ns.fifo ? IIR_FIFO : 0;
		break;
	case U_LCR:	v = ns.lcr; break;
	case U_MCR:	v = ns.mcr; break;
	case U_LSR:
		pull(u);
		v = LSR_THRE | LSR_TEMT | (u->rx_count ? LSR_DR : 0);
		break;
	case U_MSR:	v = MSR_CTS_DSR_DCD; break;
	case U_SCR:	v = ns.scr; break;
	default:
		break;
	}
	u->update(u);
	hv_unlock(&u->lock);
	return v;
}

static void ns_write(void *ctx, hv_addr_t off, int size, uint64_t value)
{
	struct vuart *u = ctx;
	uint8_t v = (uint8_t)value;

	(void)size;
	hv_lock(&u->lock);
	switch (off) {
	case U_RBR:
		if (ns.lcr & LCR_DLAB) {
			ns.dll = v;
		} else {
			hv_console_out((char)v);
			ns.thre = 1;
		}
		break;
	case U_IER:
		if (ns.lcr & LCR_DLAB) {
			ns.dlm = v;
			break;
		}
		/* THRE enabled with the transmitter empty: interrupt */
		if (!(ns.ier & IER_THRE) && (v & IER_THRE))
			ns.thre = 1;
		ns.ier = v & 0x0f;
		break;
	case U_IIR:
		ns.fifo = v & FCR_FIFO;
		if (v & FCR_RX_CLEAR)
			u->rx_count = 0;
		break;
	case U_LCR:	ns.lcr = v; break;
	case U_MCR:	ns.mcr = v & 0x1f; break;
	case U_SCR:	ns.scr = v; break;
	default:
		break;
	}
	u->update(u);
	hv_unlock(&u->lock);
}

void hv_vuart_16550_init(hv_addr_t base, unsigned int irq)
{
	ns.u.irq = irq;
	ns.u.received = ns_received;
	ns.u.update = ns_update;
	ns.u.dev = (struct hv_mmio_dev){
		.base = base, .size = 8, .ctx = &ns.u,
		.read = ns_read, .write = ns_write,
		.phys_irq = irq, .irq = rx_irq,
	};
	hv_mmio_register(&ns.u.dev);
	hv_console_rx_init();
}
