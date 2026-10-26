#ifndef ARCH_RISCV64_BARRIER_H
#define ARCH_RISCV64_BARRIER_H

#define hv_mb()	__asm__ volatile("fence rw, rw" ::: "memory")

#endif
