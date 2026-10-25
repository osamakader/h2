#ifndef ARCH_RISCV64_STAGE2_H
#define ARCH_RISCV64_STAGE2_H

#include <hv/types.h>

int riscv64_stage2_supported(void);
void riscv64_stage2_hole(hv_addr_t start, hv_addr_t end);
int riscv64_stage2_init(void);
void riscv64_stage2_enable(void);

#endif
