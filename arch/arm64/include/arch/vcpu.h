#ifndef ARCH_ARM64_VCPU_H
#define ARCH_ARM64_VCPU_H

#include <arch/vgic.h>
#include <hv/types.h>

/* FP/SIMD: q0-q31, then FPSR and FPCR (fpsimd.S) */
struct arm64_fpsimd {
	uint64_t q[64];
	uint64_t fpsr;
	uint64_t fpcr;
} __attribute__((aligned(16)));

/* EL1's (and EL0's) registers, as a vCPU leaves them */
struct arch_vcpu {
	uint64_t sctlr_el1;
	uint64_t cpacr_el1;
	uint64_t ttbr0_el1;
	uint64_t ttbr1_el1;
	uint64_t tcr_el1;
	uint64_t mair_el1;
	uint64_t amair_el1;
	uint64_t vbar_el1;
	uint64_t esr_el1;
	uint64_t afsr0_el1;
	uint64_t afsr1_el1;
	uint64_t far_el1;
	uint64_t par_el1;
	uint64_t elr_el1;
	uint64_t spsr_el1;
	uint64_t sp_el1;
	uint64_t sp_el0;
	uint64_t tpidr_el1;
	uint64_t tpidr_el0;
	uint64_t tpidrro_el0;
	uint64_t contextidr_el1;
	uint64_t csselr_el1;
	uint64_t mdscr_el1;
	uint64_t cntkctl_el1;
	uint64_t cntp_ctl_el0;
	uint64_t cntp_cval_el0;
	uint64_t cntv_ctl_el0;
	uint64_t cntv_cval_el0;
	uint64_t vmpidr_el2;		/* the MPIDR it sees */
	struct arm64_vgic vgic;
	struct arm64_fpsimd fpsimd;
};

/* a vCPU the guest starts gets arg as from PSCI CPU_ON: in x0 */
#define ARCH_VCPU_START_ARGS(hwid, arg)	(arg), 0

void arm64_fpsimd_save(struct arm64_fpsimd *fpsimd);
void arm64_fpsimd_load(const struct arm64_fpsimd *fpsimd);

#endif
