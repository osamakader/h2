#ifndef ARCH_RISCV64_BOOT_H
#define ARCH_RISCV64_BOOT_H

void riscv64_enter_guest(unsigned long entry, unsigned long stack,
			 unsigned long host_stack, unsigned long arg0,
			 unsigned long arg1);

#endif
