#ifndef ARCH_ARM64_VCPU_H
#define ARCH_ARM64_VCPU_H

#include <hv/types.h>

/* EL1's registers, as a vCPU leaves them */
struct arch_vcpu {
	uint64_t sctlr_el1;
	uint64_t cpacr_el1;
	uint64_t ttbr0_el1;
	uint64_t ttbr1_el1;
	uint64_t tcr_el1;
	uint64_t mair_el1;
	uint64_t sp_el1;
};

#endif
