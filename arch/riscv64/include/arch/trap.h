#ifndef ARCH_RISCV64_TRAP_H
#define ARCH_RISCV64_TRAP_H

#define RISCV_CAUSE_ECALL_FROM_VS	10UL

/* regs[n] holds x(n+1); hypercall arguments are a0 (x10), a1, ... */
#define HV_TRAP_ARG(n)			(9 + (n))

#endif
