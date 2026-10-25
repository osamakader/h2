#ifndef ARCH_RISCV64_VPLIC_H
#define ARCH_RISCV64_VPLIC_H

#include <hv/hv.h>

void riscv64_vplic_init(void);
void riscv64_external_sync(void);
int riscv64_vplic_access(struct hv_trap_frame *frame);

#endif
