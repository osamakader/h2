#ifndef ARCH_RISCV64_MMIO_H
#define ARCH_RISCV64_MMIO_H

#include <hv/hv.h>

/* a guest load / store page fault: 1 if at an emulated device, now done */
int riscv64_mmio_access(struct hv_trap_frame *frame);

#endif
