#ifndef ARCH_RISCV64_VCPU_H
#define ARCH_RISCV64_VCPU_H

#include <hv/types.h>

/* VS-mode's registers, as a vCPU leaves them */
struct arch_vcpu {
	uint64_t vsatp;
};

#endif
