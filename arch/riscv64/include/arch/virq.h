#ifndef ARCH_RISCV64_VIRQ_H
#define ARCH_RISCV64_VIRQ_H

void riscv64_virq_init(void);
void riscv64_timer_interrupt(void);
#include <hv/types.h>

void riscv64_guest_set_timer(hv_addr_t stime);
void riscv64_soft_interrupt(void);

#endif
