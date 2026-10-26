#ifndef ARCH_ARM64_BARRIER_H
#define ARCH_ARM64_BARRIER_H

#define hv_mb()	__asm__ volatile("dmb sy" ::: "memory")

#endif
