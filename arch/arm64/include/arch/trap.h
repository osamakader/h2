#ifndef ARCH_ARM64_TRAP_H
#define ARCH_ARM64_TRAP_H

#define ARM64_ESR_EC(esr)	(((esr) >> 26) & 0x3f)
#define ARM64_ESR_EC_HVC64	0x16
#define ARM64_ESR_EC_IABT_LOW	0x20
#define ARM64_ESR_EC_DABT_LOW	0x24
#define ARM64_ESR_EC_SMC64	0x17

/* SMC Calling Convention: the function id in w0 */
#define SMCCC_FAST		(1U << 31)
#define SMCCC_64		(1U << 30)
#define SMCCC_OWNER(fid)	(((fid) >> 24) & 0x3f)
#define SMCCC_NUMBER(fid)	((fid) & 0xffff)
#define SMCCC_OWNER_STANDARD	4	/* PSCI */
#define SMCCC_OWNER_VENDOR_HYP	6	/* h2's own calls */
#define SMCCC_RET_NOT_SUPPORTED	(-1L)

/* an h2 call: SMC64 fast call in the vendor hypervisor range */
#define HV_SMCCC_FID(nr)	(SMCCC_FAST | SMCCC_64 | \
				 (SMCCC_OWNER_VENDOR_HYP << 24) | (nr))

/* regs[n] holds xn; hypercall arguments are x0, x1, ... */
#define HV_TRAP_ARG(n)		(n)

#endif
