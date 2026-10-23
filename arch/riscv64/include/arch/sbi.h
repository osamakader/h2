#ifndef ARCH_RISCV64_SBI_H
#define ARCH_RISCV64_SBI_H

#include <hv/hv.h>

void riscv64_sbi_call(struct hv_trap_frame *frame);

#endif
