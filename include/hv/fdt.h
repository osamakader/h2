#ifndef HV_FDT_H
#define HV_FDT_H

#include <hv/types.h>

/* A flattened device tree (DTB) in memory, edited where it is. */
int fdt_valid(const void *fdt);
uint32_t fdt_size(const void *fdt);
uint32_t fdt_used(const void *fdt);
int fdt_add_memreserve(void *fdt, uint64_t addr, uint64_t size);
void fdt_move(const void *fdt, void *to, uint32_t size);
int fdt_memory(const void *fdt, uint64_t *base, uint64_t *size, uint32_t *reg);
void fdt_memory_set_size(void *fdt, uint32_t reg, uint64_t size);

#endif
