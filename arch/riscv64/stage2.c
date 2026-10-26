#include <arch/csr.h>
#include <arch/stage2.h>
#include <hv/hv.h>
#include <hv/stage2.h>

/*
 * The guest's G-stage (stage 2) translation: Sv39x4, built by
 * hv_stage2_build() from these descriptors.
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

#define ROOT_ENTRIES	2048		/* Sv39x4: 41-bit guest physical */

#define HGATP_MODE_SV39X4	(8UL << 60)
#define HGATP_MODE_MASK		(0xfUL << 60)

static uint64_t root[ROOT_ENTRIES] __attribute__((aligned(16384)));
static unsigned long hgatp;

uint64_t arch_stage2_leaf(hv_addr_t pa, int level)
{
	(void)level;
	return PTE(pa, PTE_LEAF);
}

uint64_t arch_stage2_table(const uint64_t *table)
{
	return PTE((hv_addr_t)table, PTE_V);
}

/* once, on the boot hart, after the holes: 0, or -1 */
int riscv64_stage2_init(void)
{
	if (hv_stage2_build(root, ROOT_ENTRIES) < 0)
		return -1;
	hgatp = HGATP_MODE_SV39X4 | ((hv_addr_t)root >> 12);
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
