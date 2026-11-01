#ifndef ARCH_RISCV64_VCPU_H
#define ARCH_RISCV64_VCPU_H

#include <hv/types.h>

/* FP: f0-f31, then fcsr (fp.S) */
struct riscv64_fp {
	uint64_t f[32];
	uint64_t fcsr;
};

/* VS-mode's registers, as a vCPU leaves them */
struct arch_vcpu {
	uint64_t vsstatus;
	uint64_t vsie;
	uint64_t vstvec;
	uint64_t vsscratch;
	uint64_t vsepc;
	uint64_t vscause;
	uint64_t vstval;
	uint64_t vsatp;
	uint64_t hvip;			/* its own: software and timer */
	uint64_t stime;			/* its SBI set_timer, or ~0 */
	struct riscv64_fp fp;
};

/* a vCPU the guest starts gets arg as from SBI hart_start: a0 hartid, a1 */
#define ARCH_VCPU_START_ARGS(hwid, arg)	(hwid), (arg)

void riscv64_fp_save(struct riscv64_fp *fp);
void riscv64_fp_load(const struct riscv64_fp *fp);

#endif
