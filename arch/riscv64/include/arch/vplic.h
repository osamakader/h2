#ifndef ARCH_RISCV64_VPLIC_H
#define ARCH_RISCV64_VPLIC_H

#include <hv/hv.h>

void riscv64_vplic_init(void);
void riscv64_external_sync(void);
void riscv64_external_interrupt(void);

#endif
