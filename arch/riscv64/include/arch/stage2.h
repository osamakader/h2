#ifndef ARCH_RISCV64_STAGE2_H
#define ARCH_RISCV64_STAGE2_H

#include <hv/types.h>

int riscv64_stage2_supported(void);
int riscv64_stage2_init(void);
void riscv64_stage2_enable(void);

#endif
