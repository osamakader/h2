#ifndef HV_STAGE2_H
#define HV_STAGE2_H

#include <hv/types.h>

/*
 * Stage 2 page tables for the guest: guest physical = host physical, in
 * the largest pages (1GB, 2MB, 4KB: levels 2, 1, 0) that leave out the
 * holes. The root's entries map 1GB each.
 */
void hv_stage2_hole(hv_addr_t start, hv_addr_t end);
int hv_stage2_build(uint64_t *root, int entries);

/* the architecture's descriptors */
uint64_t arch_stage2_leaf(hv_addr_t pa, int level);
uint64_t arch_stage2_table(const uint64_t *table);

#endif
