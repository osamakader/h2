#include <arch/barrier.h>
#include <hv/cpu.h>
#include <hv/hv.h>
#include <hv/mmio.h>
#include <hv/stage2.h>
#include <hv/virtio.h>

/*
 * The guest's disk. h2 drives the real virtio-blk (a queue of its own)
 * and shows the guest an emulated one in its place: a virtio-mmio
 * register file and one queue in the guest's memory. When the guest
 * notifies, h2 takes its requests: copies each header (the guest can't
 * change it once checked), checks the sectors and that every buffer is
 * the guest's own memory, and hands the data buffers to the real device
 * as they are (stage 2 is the identity). When the real device is done
 * (its interrupt, or h2 looking at the next notify), h2 fills in the
 * guest's used ring and raises the guest's interrupt. Up to SLOTS
 * requests are on the real device at a time; the rest wait in the
 * guest's ring. virtio-mmio is only on qemu, where memory is coherent: no
 * cache maintenance.
 */

#define SLOTS		8		/* requests on the real device */
#define SLOT_DESCS	16		/* a header, data, a status byte */
#define MAX_SEGS	(SLOT_DESCS - 2)	/* data buffers per request */
#define REAL_QUEUE	(SLOTS * SLOT_DESCS)
#define GUEST_QUEUE_MAX	256
#define VENDOR_H2	0x00003268	/* "h2" */

/* a guest request on the real device */
struct slot {
	struct virtio_blk_req hdr __attribute__((aligned(16)));
	volatile uint8_t status;
	int busy;
	uint16_t head;			/* the guest's chain */
	hv_addr_t status_addr;		/* its status byte */
	uint32_t written;		/* into its buffers, when done */
	uint32_t epoch;			/* the guest's queue it came from */
};

/* the real device, h2's */
static struct {
	struct virtq_desc desc[REAL_QUEUE] __attribute__((aligned(16)));
	struct {
		uint16_t flags;
		uint16_t idx;
		uint16_t ring[REAL_QUEUE];
	} avail __attribute__((aligned(4)));
	struct {
		uint16_t flags;
		uint16_t idx;
		struct virtq_used_elem ring[REAL_QUEUE];
	} used __attribute__((aligned(4096)));
	struct slot slots[SLOTS];
	uint16_t used_idx;
	hv_addr_t base;
	uint64_t capacity;		/* sectors */
} real;

/* the guest's, emulated */
static struct {
	struct hv_mmio_dev dev;
	struct hv_lock lock;
	unsigned int irq;
	uint32_t status, isr;
	uint32_t dev_features_sel, drv_features_sel;
	uint64_t drv_features;
	uint32_t queue_sel, num, ready;
	uint64_t desc, avail, used;
	uint16_t last_avail, used_idx;
	uint32_t epoch;			/* resets so far */
} vblk;

static volatile uint32_t *real_reg(hv_addr_t off)
{
	return (volatile uint32_t *)(real.base + off);
}

/* guest memory, by guest physical address (= physical) */
#define G16(a)	(*(volatile uint16_t *)(hv_addr_t)(a))
#define G32(a)	(*(volatile uint32_t *)(hv_addr_t)(a))
#define G64(a)	(*(volatile uint64_t *)(hv_addr_t)(a))
#define G8(a)	(*(volatile uint8_t *)(hv_addr_t)(a))

/* the real device's setup, as a driver's: 0, or -1 if it isn't one */
static int real_init(hv_addr_t base)
{
	uint32_t status = 0;
	hv_addr_t a;

	real.base = base;
	if (*real_reg(VIRTIO_MMIO_MAGIC) != VIRTIO_MAGIC ||
	    *real_reg(VIRTIO_MMIO_VERSION) != 2 ||
	    *real_reg(VIRTIO_MMIO_DEVICE_ID) != VIRTIO_DEV_BLOCK)
		return -1;

	*real_reg(VIRTIO_MMIO_STATUS) = 0;
	status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER;
	*real_reg(VIRTIO_MMIO_STATUS) = status;
	*real_reg(VIRTIO_MMIO_DEVICE_FEATURES_SEL) = 1;
	if (!(*real_reg(VIRTIO_MMIO_DEVICE_FEATURES) & 1U << (VIRTIO_F_VERSION_1 - 32)))
		return -1;
	*real_reg(VIRTIO_MMIO_DRIVER_FEATURES_SEL) = 0;
	*real_reg(VIRTIO_MMIO_DRIVER_FEATURES) = 0;
	*real_reg(VIRTIO_MMIO_DRIVER_FEATURES_SEL) = 1;
	*real_reg(VIRTIO_MMIO_DRIVER_FEATURES) = 1U << (VIRTIO_F_VERSION_1 - 32);
	status |= VIRTIO_STATUS_FEATURES_OK;
	*real_reg(VIRTIO_MMIO_STATUS) = status;
	if (!(*real_reg(VIRTIO_MMIO_STATUS) & VIRTIO_STATUS_FEATURES_OK))
		return -1;

	*real_reg(VIRTIO_MMIO_QUEUE_SEL) = 0;
	if (*real_reg(VIRTIO_MMIO_QUEUE_NUM_MAX) < REAL_QUEUE)
		return -1;
	*real_reg(VIRTIO_MMIO_QUEUE_NUM) = REAL_QUEUE;
	a = (hv_addr_t)real.desc;
	*real_reg(VIRTIO_MMIO_QUEUE_DESC_LOW) = (uint32_t)a;
	*real_reg(VIRTIO_MMIO_QUEUE_DESC_HIGH) = (uint32_t)(a >> 32);
	a = (hv_addr_t)&real.avail;
	*real_reg(VIRTIO_MMIO_QUEUE_DRIVER_LOW) = (uint32_t)a;
	*real_reg(VIRTIO_MMIO_QUEUE_DRIVER_HIGH) = (uint32_t)(a >> 32);
	a = (hv_addr_t)&real.used;
	*real_reg(VIRTIO_MMIO_QUEUE_DEVICE_LOW) = (uint32_t)a;
	*real_reg(VIRTIO_MMIO_QUEUE_DEVICE_HIGH) = (uint32_t)(a >> 32);
	*real_reg(VIRTIO_MMIO_QUEUE_READY) = 1;
	status |= VIRTIO_STATUS_DRIVER_OK;
	*real_reg(VIRTIO_MMIO_STATUS) = status;

	real.capacity = *real_reg(VIRTIO_MMIO_CONFIG) |
			(uint64_t)*real_reg(VIRTIO_MMIO_CONFIG + 4) << 32;
	return 0;
}

struct seg {
	uint64_t addr;
	uint32_t len;
	uint16_t flags;
};

/* a request from the guest's ring, checked and copied */
struct req {
	struct seg segs[MAX_SEGS + 2];
	int n;
	struct virtio_blk_req hdr;
};

static void update_irq(void)
{
	hv_virq_line(vblk.irq, vblk.isr != 0);
}

/* the guest's chain head done: into its used ring */
static void guest_used(uint16_t head, uint32_t len)
{
	hv_addr_t e = vblk.used + 4 + 8UL * (vblk.used_idx % vblk.num);

	G32(e) = head;
	G32(e + 4) = len;
	hv_mb();
	vblk.used_idx++;
	G16(vblk.used + 2) = vblk.used_idx;
}

static void guest_interrupt(void)
{
	hv_mb();
	if (!(G16(vblk.avail) & VIRTQ_AVAIL_F_NO_INTERRUPT)) {
		vblk.isr |= VIRTIO_INT_USED;
		update_irq();
	}
}

/*
 * The guest's request whose chain starts at head, into r: -1 if it is
 * malformed, else the status it gets at once, or -2 if it goes to the
 * real device.
 */
static int parse(uint16_t head, struct req *r)
{
	uint8_t *hdr = (uint8_t *)&r->hdr;
	uint64_t total = 0, d;
	uint16_t i = head;
	int k, out, ended = 0;

	/* the chain: no loops, no indirect tables, every buffer the guest's */
	r->n = 0;
	for (k = 0; k < (int)vblk.num; k++) {
		if (i >= vblk.num || r->n == MAX_SEGS + 2)
			return -1;
		d = vblk.desc + 16UL * i;
		r->segs[r->n].addr = G64(d);
		r->segs[r->n].len = G32(d + 8);
		r->segs[r->n].flags = G16(d + 12);
		if ((r->segs[r->n].flags & VIRTQ_DESC_F_INDIRECT) ||
		    !hv_stage2_guest_range(r->segs[r->n].addr, r->segs[r->n].len))
			return -1;
		if (!(r->segs[r->n++].flags & VIRTQ_DESC_F_NEXT)) {
			ended = 1;
			break;
		}
		i = G16(d + 14);
	}
	/* a header the device reads, a status byte it writes */
	if (!ended || r->n < 2 || r->segs[0].len < sizeof(r->hdr) ||
	    (r->segs[0].flags & VIRTQ_DESC_F_WRITE) ||
	    !(r->segs[r->n - 1].flags & VIRTQ_DESC_F_WRITE) ||
	    r->segs[r->n - 1].len < 1)
		return -1;
	for (k = 0; k < (int)sizeof(r->hdr); k++)
		hdr[k] = G8(r->segs[0].addr + k);

	switch (r->hdr.type) {
	case VIRTIO_BLK_T_IN:
	case VIRTIO_BLK_T_OUT:
		break;
	case VIRTIO_BLK_T_FLUSH:
		/* writes are through to the device already */
		return VIRTIO_BLK_S_OK;
	default:
		return VIRTIO_BLK_S_UNSUPP;
	}

	/* the data, all one way, whole sectors on the disk */
	out = r->hdr.type == VIRTIO_BLK_T_OUT;
	for (k = 1; k < r->n - 1; k++) {
		if (!!(r->segs[k].flags & VIRTQ_DESC_F_WRITE) == out)
			return VIRTIO_BLK_S_IOERR;
		total += r->segs[k].len;
	}
	if (total % VIRTIO_BLK_SECTOR || r->hdr.sector > real.capacity ||
	    total / VIRTIO_BLK_SECTOR > real.capacity - r->hdr.sector)
		return VIRTIO_BLK_S_IOERR;
	r->hdr.reserved = 0;
	return -2;
}

/* r onto the real device, in slot s (not notified yet) */
static void submit(int s, uint16_t head, const struct req *r)
{
	struct slot *slot = &real.slots[s];
	struct virtq_desc *desc = &real.desc[s * SLOT_DESCS];
	uint32_t total = 0;
	int k, n = r->n - 2;

	slot->busy = 1;
	slot->epoch = vblk.epoch;
	slot->head = head;
	slot->hdr = r->hdr;
	slot->status = 0xff;
	slot->status_addr = r->segs[r->n - 1].addr + r->segs[r->n - 1].len - 1;
	for (k = 1; k <= n; k++)
		total += r->segs[k].len;
	slot->written = r->hdr.type == VIRTIO_BLK_T_IN ? total : 0;

	desc[0] = (struct virtq_desc){ (hv_addr_t)&slot->hdr, sizeof(slot->hdr),
				       VIRTQ_DESC_F_NEXT,
				       (uint16_t)(s * SLOT_DESCS + 1) };
	for (k = 1; k <= n; k++)
		desc[k] = (struct virtq_desc){ r->segs[k].addr, r->segs[k].len,
			(uint16_t)((r->segs[k].flags & VIRTQ_DESC_F_WRITE) |
				   VIRTQ_DESC_F_NEXT),
			(uint16_t)(s * SLOT_DESCS + k + 1) };
	desc[n + 1] = (struct virtq_desc){ (hv_addr_t)&slot->status, 1,
					   VIRTQ_DESC_F_WRITE, 0 };

	real.avail.ring[real.avail.idx % REAL_QUEUE] = (uint16_t)(s * SLOT_DESCS);
	hv_mb();
	real.avail.idx++;
}

/*
 * The real device's finished requests, back to the guest: how many. One
 * from before the guest reset the device is only forgotten.
 */
static int reap(void)
{
	struct virtq_used_elem *e;
	struct slot *slot;
	int done = 0;
	uint8_t status;

	hv_mb();
	while (*(volatile uint16_t *)&real.used.idx != real.used_idx) {
		e = &real.used.ring[real.used_idx % REAL_QUEUE];
		slot = &real.slots[e->id / SLOT_DESCS];
		real.used_idx++;
		if (e->id % SLOT_DESCS || e->id >= REAL_QUEUE || !slot->busy)
			continue;
		slot->busy = 0;
		if (slot->epoch != vblk.epoch || !vblk.ready)
			continue;
		status = slot->status;
		G8(slot->status_addr) = status;
		guest_used(slot->head,
			   (status == VIRTIO_BLK_S_OK ? slot->written : 0) + 1);
		done++;
	}
	return done;
}

/*
 * The guest's new requests, in order, as far as there are slots for
 * them: 1 if any went to the real device. The others wait in its ring.
 */
static int take(void)
{
	struct req r;
	uint16_t head;
	int s, status, any = 0, done = 0;

	while (G16(vblk.avail + 2) != vblk.last_avail) {
		head = G16(vblk.avail + 4 + 2UL * (vblk.last_avail % vblk.num));
		status = parse(head, &r);
		if (status == -2) {
			for (s = 0; s < SLOTS && real.slots[s].busy; s++)
				;
			if (s == SLOTS)
				break;
			submit(s, head, &r);
			any = 1;
		} else if (status == -1) {
			guest_used(head, 0);
			done = 1;
		} else {
			G8(r.segs[r.n - 1].addr + r.segs[r.n - 1].len - 1) =
				(uint8_t)status;
			guest_used(head, 1);
			done = 1;
		}
		vblk.last_avail++;
	}
	if (any) {
		hv_mb();
		*real_reg(VIRTIO_MMIO_QUEUE_NOTIFY) = 0;
	}
	if (done)
		guest_interrupt();
	return any;
}

/* done ones back to the guest, waiting ones on to the real device */
static void progress(void)
{
	int done = reap();

	if (!(vblk.status & VIRTIO_STATUS_DRIVER_OK) || !vblk.ready)
		return;
	if (done)
		guest_interrupt();
	take();
}

/* QueueNotify */
static void notify(void)
{
	hv_mb();
	progress();
}

/* the real device's interrupt: requests done */
static void real_irq(void *ctx)
{
	uint32_t isr;

	(void)ctx;
	hv_lock(&vblk.lock);
	isr = *real_reg(VIRTIO_MMIO_INTERRUPT_STATUS);
	*real_reg(VIRTIO_MMIO_INTERRUPT_ACK) = isr;
	progress();
	hv_unlock(&vblk.lock);
}

static void reset(void)
{
	vblk.epoch++;
	vblk.status = vblk.isr = 0;
	vblk.drv_features = 0;
	vblk.num = vblk.ready = 0;
	vblk.desc = vblk.avail = vblk.used = 0;
	vblk.last_avail = vblk.used_idx = 0;
	update_irq();
}

/* QueueReady: its rings in the guest's memory, aligned */
static void queue_ready(uint32_t ready)
{
	uint64_t n = vblk.num;

	/* requests from the queue it had are forgotten */
	vblk.epoch++;
	vblk.ready = 0;
	vblk.last_avail = vblk.used_idx = 0;
	if (!ready)
		return;
	if (!n || n > GUEST_QUEUE_MAX || (vblk.desc & 15) || (vblk.avail & 1) ||
	    (vblk.used & 3) ||
	    !hv_stage2_guest_range(vblk.desc, 16 * n) ||
	    !hv_stage2_guest_range(vblk.avail, 6 + 2 * n) ||
	    !hv_stage2_guest_range(vblk.used, 6 + 8 * n)) {
		vblk.status |= VIRTIO_STATUS_FAILED;
		return;
	}
	vblk.ready = 1;
}

static uint64_t vblk_read(void *ctx, hv_addr_t off, int size)
{
	uint64_t v = 0;

	(void)ctx;
	hv_lock(&vblk.lock);
	if (off >= VIRTIO_MMIO_CONFIG) {
		/* struct virtio_blk_config: capacity, the rest zero */
		off -= VIRTIO_MMIO_CONFIG;
		if (off < 8)
			v = real.capacity >> (8 * off);
		if (size < 8)
			v &= (1UL << (8 * size)) - 1;
		hv_unlock(&vblk.lock);
		return v;
	}

	switch (off) {
	case VIRTIO_MMIO_MAGIC:		v = VIRTIO_MAGIC; break;
	case VIRTIO_MMIO_VERSION:	v = 2; break;
	case VIRTIO_MMIO_DEVICE_ID:	v = VIRTIO_DEV_BLOCK; break;
	case VIRTIO_MMIO_VENDOR_ID:	v = VENDOR_H2; break;
	case VIRTIO_MMIO_DEVICE_FEATURES:
		/* VIRTIO_F_VERSION_1, nothing else */
		v = vblk.dev_features_sel == 1 ? 1U << (VIRTIO_F_VERSION_1 - 32) : 0;
		break;
	case VIRTIO_MMIO_QUEUE_NUM_MAX:
		v = vblk.queue_sel == 0 ? GUEST_QUEUE_MAX : 0;
		break;
	case VIRTIO_MMIO_QUEUE_READY:	v = vblk.queue_sel == 0 && vblk.ready; break;
	case VIRTIO_MMIO_INTERRUPT_STATUS: v = vblk.isr; break;
	case VIRTIO_MMIO_STATUS:	v = vblk.status; break;
	case VIRTIO_MMIO_CONFIG_GENERATION: v = 0; break;
	default:			break;
	}
	hv_unlock(&vblk.lock);
	return v;
}

/* the low or high half of a 64-bit address register */
static void set_half(uint64_t *reg, int high, uint32_t v)
{
	if (high)
		*reg = (*reg & 0xffffffffUL) | (uint64_t)v << 32;
	else
		*reg = (*reg & ~0xffffffffUL) | v;
}

static void vblk_write(void *ctx, hv_addr_t off, int size, uint64_t value)
{
	uint32_t v = (uint32_t)value;
	int q0;

	(void)ctx;
	(void)size;
	hv_lock(&vblk.lock);
	q0 = vblk.queue_sel == 0 && !vblk.ready;
	switch (off) {
	case VIRTIO_MMIO_DEVICE_FEATURES_SEL:	vblk.dev_features_sel = v; break;
	case VIRTIO_MMIO_DRIVER_FEATURES_SEL:	vblk.drv_features_sel = v; break;
	case VIRTIO_MMIO_DRIVER_FEATURES:
		if (vblk.drv_features_sel < 2)
			set_half(&vblk.drv_features, vblk.drv_features_sel, v);
		break;
	case VIRTIO_MMIO_QUEUE_SEL:	vblk.queue_sel = v; break;
	case VIRTIO_MMIO_QUEUE_NUM:
		if (q0)
			vblk.num = v;
		break;
	case VIRTIO_MMIO_QUEUE_READY:
		if (vblk.queue_sel == 0)
			queue_ready(v & 1);
		break;
	case VIRTIO_MMIO_QUEUE_NOTIFY:
		if (v == 0)
			notify();
		break;
	case VIRTIO_MMIO_INTERRUPT_ACK:
		vblk.isr &= ~v;
		update_irq();
		break;
	case VIRTIO_MMIO_STATUS:
		if (v == 0) {
			reset();
			break;
		}
		/* only VIRTIO_F_VERSION_1, and that one needed */
		if ((v & VIRTIO_STATUS_FEATURES_OK) &&
		    vblk.drv_features != 1UL << VIRTIO_F_VERSION_1)
			v &= ~VIRTIO_STATUS_FEATURES_OK;
		vblk.status = v;
		break;
	case VIRTIO_MMIO_QUEUE_DESC_LOW:
	case VIRTIO_MMIO_QUEUE_DESC_HIGH:
		if (q0)
			set_half(&vblk.desc, off == VIRTIO_MMIO_QUEUE_DESC_HIGH, v);
		break;
	case VIRTIO_MMIO_QUEUE_DRIVER_LOW:
	case VIRTIO_MMIO_QUEUE_DRIVER_HIGH:
		if (q0)
			set_half(&vblk.avail, off == VIRTIO_MMIO_QUEUE_DRIVER_HIGH, v);
		break;
	case VIRTIO_MMIO_QUEUE_DEVICE_LOW:
	case VIRTIO_MMIO_QUEUE_DEVICE_HIGH:
		if (q0)
			set_half(&vblk.used, off == VIRTIO_MMIO_QUEUE_DEVICE_HIGH, v);
		break;
	default:
		break;
	}
	hv_unlock(&vblk.lock);
}

int hv_vblk_init(hv_addr_t base, unsigned int irq)
{
	if (real_init(base) < 0)
		return -1;

	vblk.irq = irq;
	vblk.dev = (struct hv_mmio_dev){
		.base = base, .size = VIRTIO_MMIO_SIZE,
		.read = vblk_read, .write = vblk_write,
		.phys_irq = irq, .irq = real_irq,
	};
	if (hv_mmio_register(&vblk.dev) < 0)
		return -1;

	hv_console_write("h2: virtio-blk for the guest, sectors: ");
	hv_console_hex(real.capacity);
	hv_console_putchar('\n');
	return 0;
}
