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

uint64_t arch_stage2_leaf(hv_addr_t pa, int level)
{
	return pa | S2_LEAF_ATTRS | (level ? DESC_BLOCK : DESC_TABLE_OR_PAGE);
}

uint64_t arch_stage2_table(const uint64_t *table)
{
	return (hv_addr_t)table | DESC_TABLE_OR_PAGE;
}
