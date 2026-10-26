#include <arch/stage2.h>
#include <hv/hv.h>
#include <hv/stage2.h>

/*
 * The guest's stage 2 translation: VMSAv8-64, 4KB granule, 39-bit IPA
 * from level 1, built by hv_stage2_build() from these descriptors.
 */

#define DESC_BLOCK		0x1UL		/* levels 1, 2 */
#define DESC_TABLE_OR_PAGE	0x3UL
#define S2_MEMATTR_NORMAL_WB	(0xfUL << 2)	/* the guest's stage 1 may be stricter */
#define S2_AP_RW		(0x3UL << 6)
#define S2_SH_INNER		(0x3UL << 8)
#define S2_AF			(1UL << 10)
#define S2_LEAF_ATTRS		(S2_MEMATTR_NORMAL_WB | S2_AP_RW | S2_SH_INNER | S2_AF)

#define ROOT_ENTRIES		512		/* level 1: 512 x 1GB */

/*
 * 39-bit IPA, 4KB granule, starting at level 1, 40-bit PA; table walks
 * non-cacheable (IRGN0 = ORGN0 = 0): h2 writes the tables with its MMU,
 * and so its caches, off.
 */
#define VTCR_T0SZ_39BIT		(64UL - 39)
#define VTCR_SL0_LEVEL1		(1UL << 6)
#define VTCR_PS_40BIT		(2UL << 16)
#define VTCR_RES1		(1UL << 31)
#define VTCR_VALUE		(VTCR_T0SZ_39BIT | VTCR_SL0_LEVEL1 | \
				 VTCR_PS_40BIT | VTCR_RES1)
#define ID_AA64MMFR0_PARANGE(v)	((v) & 0xf)
#define PARANGE_40BIT		2

static uint64_t root[ROOT_ENTRIES] __attribute__((aligned(4096)));

uint64_t arch_stage2_leaf(hv_addr_t pa, int level)
{
	return pa | S2_LEAF_ATTRS | (level ? DESC_BLOCK : DESC_TABLE_OR_PAGE);
}

uint64_t arch_stage2_table(const uint64_t *table)
{
	return (hv_addr_t)table | DESC_TABLE_OR_PAGE;
}

/* once, on the boot CPU, after the holes: 0, or -1 */
int arm64_stage2_init(void)
{
	unsigned long mmfr0;

	__asm__ volatile("mrs %0, ID_AA64MMFR0_EL1" : "=r"(mmfr0));
	if (ID_AA64MMFR0_PARANGE(mmfr0) < PARANGE_40BIT) {
		hv_console_write("h2: stage 2: physical addresses under 40 bits\n");
		return -1;
	}
	return hv_stage2_build(root, ROOT_ENTRIES);
}

/* per CPU, before HCR_EL2.VM turns it on */
void arm64_stage2_enable(void)
{
	__asm__ volatile("msr VTCR_EL2, %0" :: "r"(VTCR_VALUE));
	__asm__ volatile("msr VTTBR_EL2, %0" :: "r"(root));
	__asm__ volatile("isb; tlbi vmalls12e1; dsb nsh; isb" ::: "memory");
}
