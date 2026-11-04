#ifndef ARCH_RISCV64_VIRQ_H
#define ARCH_RISCV64_VIRQ_H

#include <hv/types.h>

void riscv64_virq_init(void);
int riscv64_sstc(void);
void riscv64_timer_interrupt(void);
void riscv64_timer_program(void);
void riscv64_guest_set_timer(hv_addr_t stime);
void riscv64_soft_interrupt(void);
void riscv64_guest_ipi(int slot);
void riscv64_kick(int slot);

#endif
