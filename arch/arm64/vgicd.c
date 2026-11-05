#include <arch/barrier.h>
#include <arch/platform.h>
#include <arch/vgic.h>
#include <hv/cpu.h>
#include <hv/hv.h>
#include <hv/mmio.h>
#include <hv/vcpu.h>

/*
 * Each guest's virtual distributor (and on GICv3 its redistributors):
 * emulated devices, so no guest touches the real ones, which are h2's.
 * What a guest programs is kept here, for each guest and for each CPU
 * (SGIs and PPIs). Guest 0 owns the machine's SPIs, but for the ones of
 * devices h2 emulates, so what it says about those (enable, trigger,
 * routing, pending and active state) goes to the real distributor as
 * well. Priorities don't: h2 ends each real interrupt's priority at once,
 * and the list registers get the guest's priority from here. Real PPI
 * enables follow the vCPU that runs (arm64_vgic_ppi_wanted()). SGIs are
 * virtual: from a guest's write, into its target's mailbox here.
 */

#ifdef PLATFORM_GICR
#define PLATFORM_GICR_BASE	PLATFORM_GICR
#else
#define PLATFORM_GICR_BASE	0		/* a GICv2 machine */
#endif

#define NR_INTIDS	288		/* qemu virt's; the Pi 4's are fewer */
#define WORDS		(NR_INTIDS / 32)

#define GICD_CTLR	0x000
#define GICD_TYPER	0x004
#define GICD_IIDR	0x008
#define GICx_IGROUPR	0x080
#define GICx_ISENABLER	0x100
#define GICx_ICENABLER	0x180
#define GICx_ISPENDR	0x200
#define GICx_ICPENDR	0x280
#define GICx_ISACTIVER	0x300
#define GICx_ICACTIVER	0x380
#define GICx_IPRIORITYR	0x400
#define GICD_ITARGETSR	0x800
#define GICx_ICFGR	0xc00
#define GICD_SGIR	0xf00
#define GICD_IROUTER	0x6000
#define GICD_IROUTER_END 0x7fe0
#define GICD_ID_V2	0xfd0
#define GICD_ID_V3	0xffd0
#define GICD_SIZE_V2	0x1000
#define GICD_SIZE_V3	0x10000
#define GICD_CTLR_MASK_V2 0x3
#define GICD_CTLR_MASK_V3 0x37		/* ARE, DS's groups */

#define GICR_CTLR	0x000
#define GICR_IIDR	0x004
#define GICR_TYPER	0x008
#define GICR_WAKER	0x014
#define GICR_ID		0xffd0
#define GICR_FRAME	0x20000
#define GICR_SGI_BASE	0x10000
#define GICR_TYPER_LAST	(1UL << 4)
#define GICR_TYPER_LPIS	3UL		/* no LPIs, direct or virtual */
#define WAKER_ASLEEP	6U		/* ProcessorSleep, ChildrenAsleep */

/* an SGI's or PPI's: per guest, per CPU */
struct bank {
	uint32_t enabled, group, cfg1, waker;
	uint8_t prio[32];
	uint16_t sgi;			/* mailbox: pending SGIs */
	uint8_t sgi_src[16];		/* GICv2: from which CPU interfaces */
};

struct dist {
	uint32_t ctlr;
	uint32_t enabled[WORDS], group[WORDS], cfg[2 * WORDS];
	uint8_t prio[NR_INTIDS], targets[NR_INTIDS];
	uint64_t router[NR_INTIDS];
	struct bank bank[HV_MAX_CPUS];
};

static struct dist dist[HV_MAX_GUESTS];
static struct hv_lock lock;
static int gic3;
static uint32_t h2_spis[WORDS];		/* SPIs whose real ones are h2's */
static hv_addr_t gicr_size;

#define REAL_D(off)	((volatile uint32_t *)((hv_addr_t)PLATFORM_GICD + (off)))
#define REAL_D8(off)	((volatile uint8_t *)((hv_addr_t)PLATFORM_GICD + (off)))
#define REAL_D64(off)	((volatile uint64_t *)((hv_addr_t)PLATFORM_GICD + (off)))

static int guest_id(void)
{
	return hv_vcpu_current()->guest->id;
}

/* the bits of word n of SPIs that guest g owns for real */
static uint32_t owned(int g, unsigned int n)
{
	return g || n == 0 || n >= WORDS ? 0 : ~h2_spis[n];
}

static int spi_owned(int g, unsigned int intid)
{
	return intid >= 32 && intid < NR_INTIDS &&
	       (owned(g, intid / 32) >> (intid % 32) & 1);
}

/* word 0 of the per-interrupt registers: GICD (GICv2), SGI_base (GICv3) */
static uint32_t banked_read(struct bank *b, hv_addr_t off)
{
	switch (off) {
	case GICx_IGROUPR:	return b->group;
	case GICx_ISENABLER:
	case GICx_ICENABLER:	return b->enabled;
	case GICx_ICFGR:	return 0xaaaaaaaa;	/* SGIs: edge */
	case GICx_ICFGR + 4:	return b->cfg1;
	default:		return 0;
	}
}

static void banked_write(struct bank *b, hv_addr_t off, uint32_t v)
{
	/* GICv2's SGIs are always on */
	uint32_t can = gic3 ? ~0U : 0xffff0000U;

	switch (off) {
	case GICx_IGROUPR:	b->group = v; break;
	case GICx_ISENABLER:	b->enabled |= v & can; break;
	case GICx_ICENABLER:	b->enabled &= ~(v & can); break;
	case GICx_ICFGR + 4:	b->cfg1 = v; break;
	default:		break;
	}
}

/* n bytes of the priorities or targets of consecutive interrupts */
static uint64_t bytes_read(const uint8_t *a, int size)
{
	uint64_t v = 0;
	int i;

	for (i = 0; i < size; i++)
		v |= (uint64_t)a[i] << (8 * i);
	return v;
}

static void bytes_write(uint8_t *a, int size, uint64_t v)
{
	int i;

	for (i = 0; i < size; i++)
		a[i] = (uint8_t)(v >> (8 * i));
}

/* an SGI for guest g's vCPU on target: into its mailbox */
void arm64_vgicd_sgi(int g, int target, unsigned int intid, unsigned int src)
{
	struct bank *b = &dist[g].bank[target];

	hv_lock(&lock);
	b->sgi |= 1U << intid;
	b->sgi_src[intid] |= 1U << src;
	hv_unlock(&lock);
}

/*
 * One SGI out of guest g's mailbox on this CPU that is enabled: 1, its
 * number and on GICv2 its source. 0 if none.
 */
int arm64_vgicd_sgi_take(int g, int cpu, unsigned int *intid, unsigned int *src)
{
	struct bank *b = &dist[g].bank[cpu];
	uint32_t ready = b->sgi & b->enabled;
	unsigned int i, s;

	if (!ready)
		return 0;
	hv_lock(&lock);
	for (i = 0; !(ready >> i & 1); i++)
		;
	for (s = 0; !(b->sgi_src[i] >> s & 1); s++)
		;
	b->sgi_src[i] &= (uint8_t)~(1U << s);
	if (!b->sgi_src[i])
		b->sgi &= (uint16_t)~(1U << i);
	hv_unlock(&lock);
	*intid = i;
	*src = s;
	return 1;
}

/* whether guest g has an SGI for its vCPU on cpu */
int arm64_vgicd_sgi_waiting(int g, int cpu)
{
	return (dist[g].bank[cpu].sgi & dist[g].bank[cpu].enabled) != 0;
}

int arm64_vgicd_enabled(int g, int cpu, unsigned int intid)
{
	if (intid < 32)
		return dist[g].bank[cpu].enabled >> intid & 1;
	return intid < NR_INTIDS && (dist[g].enabled[intid / 32] >> (intid % 32) & 1);
}

unsigned int arm64_vgicd_prio(int g, int cpu, unsigned int intid)
{
	if (intid < 32)
		return dist[g].bank[cpu].prio[intid];
	return intid < NR_INTIDS ? dist[g].prio[intid] : 0;
}

/* the CPU guest g routes an SPI to: its slot, or -1 for any or unknown */
int arm64_vgicd_route(int g, unsigned int intid)
{
	hv_addr_t want;
	int slot, k;

	if (intid < 32 || intid >= NR_INTIDS)
		return -1;
	if (gic3) {
		want = dist[g].router[intid];
		if (want & (1UL << 31))
			return -1;
		want &= 0xff00ffffffUL;		/* as MPIDR has them */
	} else {
		for (k = 0; k < 8 && !(dist[g].targets[intid] >> k & 1); k++)
			;
		if (k == 8)
			return -1;
		want = (hv_addr_t)k;		/* CPU interface k: Aff0 k */
	}
	for (slot = 0; slot < HV_MAX_CPUS; slot++)
		if (hv_cpus[slot].used &&
		    (gic3 ? hv_cpus[slot].hwid : hv_cpus[slot].hwid & 0xff) == want)
			return slot;
	return -1;
}

/* the PPIs guest g wants on for its vCPU on cpu: real ones follow */
uint32_t arm64_vgic_ppi_wanted(int g, int cpu)
{
	return dist[g].bank[cpu].enabled & 0xffff0000U;
}

static uint64_t gicd_read(void *ctx, hv_addr_t off, int size)
{
	int g = guest_id(), me = hv_cpu_index();
	struct dist *d = &dist[g];
	unsigned int n = (unsigned int)(off & 0x7f) / 4, intid;
	hv_addr_t reg = off & ~0x7fUL;
	uint32_t v;

	(void)ctx;
	if (off >= (gic3 ? GICD_ID_V3 : GICD_ID_V2))
		return *REAL_D(off);

	if (off >= GICx_IPRIORITYR && off < GICD_ITARGETSR) {
		intid = (unsigned int)(off - GICx_IPRIORITYR);
		if (intid + (unsigned int)size > NR_INTIDS)
			return 0;
		if (intid < 32)
			return gic3 ? 0 : bytes_read(&d->bank[me].prio[intid], size);
		return bytes_read(&d->prio[intid], size);
	}
	if (off >= GICD_ITARGETSR && off < GICx_ICFGR) {
		intid = (unsigned int)(off - GICD_ITARGETSR);
		if (gic3 || intid + (unsigned int)size > NR_INTIDS)
			return 0;
		if (intid < 32)		/* an SGI's or PPI's: this CPU */
			return 0x01010101U << (hv_cpus[me].hwid & 7) &
			       (size < 4 ? (1U << (8 * size)) - 1 : ~0U);
		return bytes_read(&d->targets[intid], size);
	}
	if (off >= GICx_ICFGR && off < GICD_SGIR) {
		n = (unsigned int)(off - GICx_ICFGR) / 4;
		if (n < 2)
			return gic3 ? 0 : banked_read(&d->bank[me], off);
		return n < 2 * WORDS ? d->cfg[n] : 0;
	}
	if (gic3 && off >= GICD_IROUTER && off < GICD_IROUTER_END) {
		intid = (unsigned int)(off - GICD_IROUTER) / 8;
		if (intid >= NR_INTIDS || intid < 32)
			return 0;
		return size == 8 ? d->router[intid] :
		       (uint32_t)(d->router[intid] >> (8 * (off & 7)));
	}

	switch (off) {
	case GICD_CTLR:
		return d->ctlr;
	case GICD_TYPER:
		/* no more interrupts than kept here */
		v = *REAL_D(GICD_TYPER);
		if ((v & 0x1f) >= WORDS)
			v = (v & ~0x1fU) | (WORDS - 1);
		return v;
	case GICD_IIDR:
		return *REAL_D(GICD_IIDR);
	}

	if (reg >= GICx_IGROUPR && reg < GICx_IPRIORITYR) {
		if (n == 0)
			return gic3 ? 0 : banked_read(&d->bank[me], reg);
		if (n >= WORDS)
			return 0;
		switch (reg) {
		case GICx_IGROUPR:	return d->group[n];
		case GICx_ISENABLER:
		case GICx_ICENABLER:	return d->enabled[n];
		default:
			/* pending, active: the real ones of its own SPIs */
			return *REAL_D(off) & owned(g, n);
		}
	}
	return 0;
}

static void gicd_write(void *ctx, hv_addr_t off, int size, uint64_t value)
{
	int g = guest_id(), me = hv_cpu_index(), i;
	struct dist *d = &dist[g];
	unsigned int n = (unsigned int)(off & 0x7f) / 4, intid;
	hv_addr_t reg = off & ~0x7fUL;
	uint32_t v = (uint32_t)value, m;

	(void)ctx;
	hv_lock(&lock);
	if (off >= GICx_IPRIORITYR && off < GICD_ITARGETSR) {
		intid = (unsigned int)(off - GICx_IPRIORITYR);
		if (intid + (unsigned int)size <= NR_INTIDS) {
			if (intid >= 32)
				bytes_write(&d->prio[intid], size, value);
			else if (!gic3)
				bytes_write(&d->bank[me].prio[intid], size, value);
		}
	} else if (off >= GICD_ITARGETSR && off < GICx_ICFGR) {
		intid = (unsigned int)(off - GICD_ITARGETSR);
		if (!gic3 && intid >= 32 && intid + (unsigned int)size <= NR_INTIDS) {
			bytes_write(&d->targets[intid], size, value);
			for (i = 0; i < size; i++)
				if (spi_owned(g, intid + (unsigned int)i))
					*REAL_D8(GICD_ITARGETSR + intid + i) =
						d->targets[intid + (unsigned int)i];
		}
	} else if (off >= GICx_ICFGR && off < GICD_SGIR) {
		n = (unsigned int)(off - GICx_ICFGR) / 4;
		if (n < 2) {
			if (!gic3)
				banked_write(&d->bank[me], off, v);
		} else if (n < 2 * WORDS) {
			d->cfg[n] = v;
			/* the fields of its own: two bits for each of 16 */
			for (m = 0, i = 0; i < 16; i++)
				if (spi_owned(g, n * 16 + (unsigned int)i))
					m |= 3U << (2 * i);
			if (m)
				*REAL_D(off) = (*REAL_D(off) & ~m) | (v & m);
		}
	} else if (off == GICD_SGIR) {
		hv_unlock(&lock);
		arm64_vgic_sgi_v2(v);
		return;
	} else if (gic3 && off >= GICD_IROUTER && off < GICD_IROUTER_END) {
		intid = (unsigned int)(off - GICD_IROUTER) / 8;
		if (intid >= 32 && intid < NR_INTIDS) {
			if (size == 8)
				d->router[intid] = value;
			else if (off & 4)
				d->router[intid] = (d->router[intid] & 0xffffffffUL) |
						   value << 32;
			else
				d->router[intid] = (d->router[intid] & ~0xffffffffUL) |
						   (value & 0xffffffffUL);
			if (spi_owned(g, intid))
				*REAL_D64(GICD_IROUTER + 8UL * intid) = d->router[intid];
		}
	} else if (off == GICD_CTLR) {
		d->ctlr = v & (gic3 ? GICD_CTLR_MASK_V3 : GICD_CTLR_MASK_V2);
	} else if (reg >= GICx_IGROUPR && reg < GICx_IPRIORITYR) {
		if (n == 0) {
			if (!gic3)
				banked_write(&d->bank[me], reg, v);
		} else if (n < WORDS) {
			switch (reg) {
			case GICx_IGROUPR:
				d->group[n] = v;
				break;
			case GICx_ISENABLER:
				d->enabled[n] |= v;
				*REAL_D(off) = v & owned(g, n);
				break;
			case GICx_ICENABLER:
				d->enabled[n] &= ~v;
				*REAL_D(off) = v & owned(g, n);
				break;
			default:
				*REAL_D(off) = v & owned(g, n);
				break;
			}
		}
	}
	hv_unlock(&lock);
	/* enables, routes and priorities can matter to any CPU */
	arm64_vgic_changed(g);
}

/* GICv3's redistributors: frame i's CPU, or -1 if it hasn't started */
static int frame_slot(hv_addr_t frame)
{
	uint64_t typer = *(volatile uint64_t *)((hv_addr_t)PLATFORM_GICR_BASE +
						frame * GICR_FRAME + GICR_TYPER);
	hv_addr_t aff = typer >> 32;
	int slot;

	/* Aff3 above Aff2 in GICR_TYPER, above a gap in MPIDR */
	aff = (aff & 0xffffff) | (aff >> 24 & 0xff) << 32;
	for (slot = 0; slot < HV_MAX_CPUS; slot++)
		if (hv_cpus[slot].used && hv_cpus[slot].hwid == aff)
			return slot;
	return -1;
}

static uint64_t gicr_read(void *ctx, hv_addr_t off, int size)
{
	int g = guest_id(), slot = frame_slot(off / GICR_FRAME);
	hv_addr_t rel = off % GICR_FRAME;
	hv_addr_t real = PLATFORM_GICR_BASE + off;
	struct bank *b;
	uint64_t v;

	(void)ctx;
	if (rel == GICR_TYPER || rel == GICR_TYPER + 4) {
		v = *(volatile uint64_t *)(real - (rel - GICR_TYPER));
		v &= ~GICR_TYPER_LPIS;
		return size == 8 ? v : (uint32_t)(v >> (8 * (rel - GICR_TYPER)));
	}
	if ((rel >= GICR_ID && rel < GICR_SGI_BASE) || rel == GICR_IIDR)
		return *(volatile uint32_t *)real;
	if (slot < 0)
		return 0;
	b = &dist[g].bank[slot];
	if (rel == GICR_WAKER)
		return b->waker;
	if (rel < GICR_SGI_BASE)
		return 0;
	rel -= GICR_SGI_BASE;
	if (rel >= GICx_IPRIORITYR && rel + (hv_addr_t)size <= GICx_IPRIORITYR + 32)
		return bytes_read(&b->prio[rel - GICx_IPRIORITYR], size);
	return banked_read(b, rel);
}

static void gicr_write(void *ctx, hv_addr_t off, int size, uint64_t value)
{
	int g = guest_id(), slot = frame_slot(off / GICR_FRAME);
	hv_addr_t rel = off % GICR_FRAME;
	struct bank *b;

	(void)ctx;
	if (slot < 0)
		return;
	b = &dist[g].bank[slot];
	hv_lock(&lock);
	if (rel == GICR_WAKER) {
		/* wakes at once; both bits say the same */
		b->waker = (value & 2) ? WAKER_ASLEEP : 0;
	} else if (rel >= GICR_SGI_BASE) {
		rel -= GICR_SGI_BASE;
		if (rel >= GICx_IPRIORITYR &&
		    rel + (hv_addr_t)size <= GICx_IPRIORITYR + 32)
			bytes_write(&b->prio[rel - GICx_IPRIORITYR], size, value);
		else
			banked_write(b, rel, (uint32_t)value);
	}
	hv_unlock(&lock);
	arm64_vgic_changed(g);
}

/*
 * Once, on the boot CPU, gic3 being what the CPU has: the real distributor
 * is h2's, on, its SPIs off and all alike until a guest says otherwise.
 * Then the emulated ones, for all guests.
 */
void arm64_vgicd_setup(int v3)
{
	static struct hv_mmio_dev d, r;
	hv_addr_t f;
	unsigned int n, i;
	int g, c;

	gic3 = v3;
	for (g = 0; g < HV_MAX_GUESTS; g++) {
		for (c = 0; c < HV_MAX_CPUS; c++) {
			dist[g].bank[c].waker = WAKER_ASLEEP;
			dist[g].bank[c].enabled = gic3 ? 0 : 0xffff;
		}
	}

	*REAL_D(GICD_CTLR) = gic3 ? (1U << 4) | 3 : 1;	/* ARE, the groups */
	for (n = 1; n < WORDS; n++) {
		*REAL_D(GICx_ICENABLER + 4 * n) = ~0U;
		*REAL_D(GICx_ICPENDR + 4 * n) = ~0U;
		*REAL_D(GICx_ICACTIVER + 4 * n) = ~0U;
		if (gic3)
			*REAL_D(GICx_IGROUPR + 4 * n) = ~0U;
	}
	for (i = 32; i < NR_INTIDS; i++)
		*REAL_D8(GICx_IPRIORITYR + i) = 0x80;

	d = (struct hv_mmio_dev){
		.base = PLATFORM_GICD,
		.size = gic3 ? GICD_SIZE_V3 : GICD_SIZE_V2,
		.read = gicd_read, .write = gicd_write, .all_guests = 1,
	};
	hv_mmio_register(&d);
	if (gic3) {
		for (f = 0;; f++) {
			if (*(volatile uint64_t *)((hv_addr_t)PLATFORM_GICR_BASE +
						   f * GICR_FRAME + GICR_TYPER) &
			    GICR_TYPER_LAST)
				break;
		}
		gicr_size = (f + 1) * GICR_FRAME;
		r = (struct hv_mmio_dev){
			.base = PLATFORM_GICR_BASE, .size = gicr_size,
			.read = gicr_read, .write = gicr_write, .all_guests = 1,
		};
		hv_mmio_register(&r);
	}
}

/*
 * A device h2 emulates owns the real SPI irq: on, always, on this CPU; a
 * guest's enable and routing of its number are only for the virtual one.
 */
void arch_irq_own(unsigned int irq)
{
	hv_addr_t me = hv_cpus[hv_cpu_index()].hwid;

	if (irq < 32 || irq >= NR_INTIDS)
		return;
	h2_spis[irq / 32] |= 1U << (irq % 32);
	if (gic3)
		*REAL_D64(GICD_IROUTER + 8UL * irq) = me;
	else
		*REAL_D8(GICD_ITARGETSR + irq) = (uint8_t)(1U << (me & 7));
	*REAL_D(GICx_ISENABLER + 4 * (irq / 32)) = 1U << (irq % 32);
}
