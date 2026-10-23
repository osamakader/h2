#ifndef ARCH_RISCV64_TRAP_H
#define ARCH_RISCV64_TRAP_H

#define RISCV_CAUSE_INTERRUPT		(1UL << 63)
#define RISCV_CAUSE_ECALL_FROM_VS	10UL

/* SBI: extension id in a7, function id in a6; error in a0, value in a1 */
#define SBI_ERR_NOT_SUPPORTED		(-2L)
/* h2's own calls, in the firmware-specific extension space ("H2") */
#define HV_SBI_EXT			0x0A004832UL

/* regs[n] holds x(n+1); hypercall arguments are a0 (x10), a1, ... */
#define HV_TRAP_ARG(n)			(9 + (n))

#endif
