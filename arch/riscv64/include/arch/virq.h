#ifndef ARCH_RISCV64_VIRQ_H
#define ARCH_RISCV64_VIRQ_H

void riscv64_virq_init(void);
void riscv64_timer_interrupt(void);
void riscv64_guest_set_timer(void);

#endif
