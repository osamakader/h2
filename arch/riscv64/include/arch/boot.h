#ifndef ARCH_RISCV64_BOOT_H
#define ARCH_RISCV64_BOOT_H

#include <hv/types.h>

void riscv64_hart_init(void);
void riscv64_run_guest(int slot, hv_addr_t entry, hv_addr_t arg0,
		       hv_addr_t arg1);

#endif
