#include <hv/hv.h>
#include <hv/stage2.h>

#define PAGE_SHIFT	12
#define PT_ENTRIES	512
#define ROOT_LEVEL	2
#define LEVEL_SIZE(l)	(1UL << (PAGE_SHIFT + 9 * (l)))

#define MAX_HOLES	8
#define POOL_TABLES	10

static uint64_t pool[POOL_TABLES][PT_ENTRIES] __attribute__((aligned(4096)));
static int pool_used;

/* not identity mapped: unmapped (to == 0), or mapped to to */
static struct {
	hv_addr_t start;
	hv_addr_t end;
	hv_addr_t to;
} holes[MAX_HOLES];
static int nholes;

/*
 * 1 if [a, b) meets a hole, 2 if it lies inside an unmapped one, 3 if it
 * is a page inside a remapped one (*pa: where it goes)
 */
static int in_hole(hv_addr_t a, hv_addr_t b, hv_addr_t *pa)
{
	int i;

	for (i = 0; i < nholes; i++) {
		if (holes[i].start <= a && b <= holes[i].end) {
			if (!holes[i].to)
				return 2;
			if (b - a > LEVEL_SIZE(0))
				return 1;
			*pa = holes[i].to + (a - holes[i].start);
			return 3;
		}
		if (holes[i].start < b && a < holes[i].end)
			return 1;
	}
	return 0;
}

static int fill(uint64_t *table, int entries, int level, hv_addr_t base)
{
	hv_addr_t size = LEVEL_SIZE(level), a, pa;
	uint64_t *next;
	int i;

	for (i = 0; i < entries; i++) {
		a = base + i * size;
		switch (in_hole(a, a + size, &pa)) {
		case 0:
			table[i] = arch_stage2_leaf(a, level);
			break;
		case 3:
			table[i] = arch_stage2_leaf(pa, 0);
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
			table[i] = arch_stage2_table(next);
		}
	}
	return 0;
}

/* leave [start, end), page aligned, out of the guest's view */
void hv_stage2_hole(hv_addr_t start, hv_addr_t end)
{
	hv_stage2_remap(start, end, 0);
}

/* show the guest [to, to + end - start) at [start, end), page aligned */
void hv_stage2_remap(hv_addr_t start, hv_addr_t end, hv_addr_t to)
{
	if (nholes < MAX_HOLES) {
		holes[nholes].start = start;
		holes[nholes].end = end;
		holes[nholes].to = to;
		nholes++;
	} else {
		hv_console_write("h2: stage 2: too many holes\n");
	}
}

/*
 * Whether [start, start + size) is the guest's own memory as it sees it
 * (identity mapped, in no hole): for buffers it hands to an emulated
 * device. Not past what the stage 2 tables reach.
 */
static hv_addr_t reach;

int hv_stage2_guest_range(hv_addr_t start, hv_addr_t size)
{
	hv_addr_t pa;

	if (start + size < start || start + size > reach)
		return 0;
	return size == 0 || !in_hole(start, start + size, &pa);
}

#define WINDOW_TABLES	4

static uint64_t window_pool[WINDOW_TABLES][PT_ENTRIES] __attribute__((aligned(4096)));
static int window_used;

static int fill_window(uint64_t *table, int entries, int level, hv_addr_t base,
		       hv_addr_t ipa, hv_addr_t size, hv_addr_t pa)
{
	hv_addr_t step = LEVEL_SIZE(level), a;
	uint64_t *next;
	int i;

	for (i = 0; i < entries; i++) {
		a = base + i * step;
		if (a + step <= ipa || a >= ipa + size) {
			table[i] = 0;
		} else if (a >= ipa && a + step <= ipa + size &&
			   !((a - ipa + pa) & (step - 1))) {
			table[i] = arch_stage2_leaf(a - ipa + pa, level);
		} else {
			if (level == 0 || window_used == WINDOW_TABLES)
				return -1;
			next = window_pool[window_used++];
			if (fill_window(next, PT_ENTRIES, level - 1, a, ipa, size, pa) < 0)
				return -1;
			table[i] = arch_stage2_table(next);
		}
	}
	return 0;
}

/*
 * A second guest's tables: only [ipa, ipa + size) mapped, to pa. Once,
 * page aligned: 0, or -1.
 */
int hv_stage2_build_window(uint64_t *root, int entries, hv_addr_t ipa,
			   hv_addr_t size, hv_addr_t pa)
{
	return fill_window(root, entries, ROOT_LEVEL, 0, ipa, size, pa);
}

/* once, after the holes: 0, or -1 */
int hv_stage2_build(uint64_t *root, int entries)
{
	reach = entries * LEVEL_SIZE(ROOT_LEVEL);
	if (fill(root, entries, ROOT_LEVEL, 0) < 0) {
		hv_console_write("h2: stage 2: out of page tables\n");
		return -1;
	}
	return 0;
}
