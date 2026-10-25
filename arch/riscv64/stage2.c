#include <arch/csr.h>
#include <arch/stage2.h>
#include <hv/hv.h>

/*
 * The guest's G-stage (stage 2) translation: Sv39x4, guest physical =
 * host physical, in the largest pages that leave out the holes.
 */

#define PTE_V		(1UL << 0)
#define PTE_R		(1UL << 1)
#define PTE_W		(1UL << 2)
#define PTE_X		(1UL << 3)
#define PTE_U		(1UL << 4)	/* required in G-stage leaves */
#define PTE_A		(1UL << 6)
#define PTE_D		(1UL << 7)
#define PTE_LEAF	(PTE_V | PTE_R | PTE_W | PTE_X | PTE_U | PTE_A | PTE_D)
#define PTE(pa, flags)	((((pa) >> 12) << 10) | (flags))

#define PAGE_SHIFT	12
#define PT_ENTRIES	512
#define ROOT_ENTRIES	2048		/* Sv39x4: 41-bit guest physical */
#define ROOT_LEVEL	2
#define LEVEL_SIZE(l)	(1UL << (PAGE_SHIFT + 9 * (l)))

#define HGATP_MODE_SV39X4	(8UL << 60)
#define HGATP_MODE_MASK		(0xfUL << 60)

#define MAX_HOLES	4
#define POOL_TABLES	8

static uint64_t root[ROOT_ENTRIES] __attribute__((aligned(16384)));
static uint64_t pool[POOL_TABLES][PT_ENTRIES] __attribute__((aligned(4096)));
static int pool_used;

static struct {
	hv_addr_t start;
	hv_addr_t end;
} holes[MAX_HOLES];
static int nholes;

static unsigned long hgatp;

/* 1 if [a, b) meets a hole, 2 if it lies inside one */
static int in_hole(hv_addr_t a, hv_addr_t b)
{
	int i;

	for (i = 0; i < nholes; i++) {
		if (holes[i].start <= a && b <= holes[i].end)
			return 2;
		if (holes[i].start < b && a < holes[i].end)
			return 1;
	}
	return 0;
}

static int fill(uint64_t *table, int entries, int level, hv_addr_t base)
{
	hv_addr_t size = LEVEL_SIZE(level), a;
	uint64_t *next;
	int i;

	for (i = 0; i < entries; i++) {
		a = base + i * size;
		switch (in_hole(a, a + size)) {
		case 0:
			table[i] = PTE(a, PTE_LEAF);
			break;
		case 2:
			table[i] = 0;
			break;
		default:
			if (level == 0 || pool_used == POOL_TABLES)
				return -1;
			next = pool[pool_used++];
			if (fill(next, PT_ENTRIES, level - 1, a) < 0)
				return -1;
			table[i] = PTE((hv_addr_t)next, PTE_V);
		}
	}
	return 0;
}

/* leave [start, end), page aligned, out of the guest's view */
void riscv64_stage2_hole(hv_addr_t start, hv_addr_t end)
{
	if (nholes < MAX_HOLES) {
		holes[nholes].start = start;
		holes[nholes].end = end;
		nholes++;
	}
}

/* once, on the boot hart, after the holes: 0, or -1 */
int riscv64_stage2_init(void)
{
	if (fill(root, ROOT_ENTRIES, ROOT_LEVEL, 0) < 0) {
		hv_console_write("h2: stage 2: out of page tables\n");
		return -1;
	}
	hgatp = HGATP_MODE_SV39X4 | ((hv_addr_t)root >> PAGE_SHIFT);
	return 0;
}

/* per hart, before entering the guest */
void riscv64_stage2_enable(void)
{
	csr_write(hgatp, hgatp);
	__asm__ volatile("hfence.gvma zero, zero" ::: "memory");
}

/* whether the hart can do Sv39x4 at all */
int riscv64_stage2_supported(void)
{
	unsigned long mode;

	csr_write(hgatp, HGATP_MODE_SV39X4);
	mode = csr_read(hgatp) & HGATP_MODE_MASK;
	csr_write(hgatp, 0);
	return mode == HGATP_MODE_SV39X4;
}
