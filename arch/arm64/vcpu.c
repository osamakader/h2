#include <arch/boot.h>
#include <hv/vcpu.h>

#define SPSR_EL1H		0x5UL
#define SPSR_DAIF		(0xfUL << 6)
#define SCTLR_EL1_RES1		((1UL << 29) | (1UL << 28) | (1UL << 23) | \
				 (1UL << 22) | (1UL << 20) | (1UL << 11))

#define SAVE(reg, field) \
	__asm__ volatile("mrs %0, " #reg : "=r"(vcpu->arch.field))
#define LOAD(reg, field) \
	__asm__ volatile("msr " #reg ", %0" :: "r"(vcpu->arch.field))

/*
 * EL1h, MMU off, interrupts masked, x0 = arg0, as firmware starts Linux.
 * Every register starts defined, but CNTKCTL_EL1 (EL0's access to the
 * counters), which a guest may leave as its firmware set it: this CPU's.
 */
void arch_vcpu_init(struct hv_vcpu *vcpu, hv_addr_t entry, hv_addr_t stack,
		    hv_addr_t arg0, hv_addr_t arg1)
{
	hv_addr_t mpidr;

	(void)arg1;
	vcpu->frame.pc = entry;
	vcpu->frame.status = SPSR_EL1H | SPSR_DAIF;
	vcpu->frame.regs[0] = arg0;
	vcpu->arch.sctlr_el1 = SCTLR_EL1_RES1;
	vcpu->arch.sp_el1 = stack;
	SAVE(CNTKCTL_EL1, cntkctl_el1);
	__asm__ volatile("mrs %0, MPIDR_EL1" : "=r"(mpidr));
	vcpu->arch.vmpidr_el2 = mpidr;
}

/* the running vCPU's state, into vcpu; its timers stop */
void arch_vcpu_save(struct hv_vcpu *vcpu)
{
	SAVE(SCTLR_EL1, sctlr_el1);
	SAVE(CPACR_EL1, cpacr_el1);
	SAVE(TTBR0_EL1, ttbr0_el1);
	SAVE(TTBR1_EL1, ttbr1_el1);
	SAVE(TCR_EL1, tcr_el1);
	SAVE(MAIR_EL1, mair_el1);
	SAVE(AMAIR_EL1, amair_el1);
	SAVE(VBAR_EL1, vbar_el1);
	SAVE(ESR_EL1, esr_el1);
	SAVE(AFSR0_EL1, afsr0_el1);
	SAVE(AFSR1_EL1, afsr1_el1);
	SAVE(FAR_EL1, far_el1);
	SAVE(PAR_EL1, par_el1);
	SAVE(ELR_EL1, elr_el1);
	SAVE(SPSR_EL1, spsr_el1);
	SAVE(SP_EL1, sp_el1);
	SAVE(SP_EL0, sp_el0);
	SAVE(TPIDR_EL1, tpidr_el1);
	SAVE(TPIDR_EL0, tpidr_el0);
	SAVE(TPIDRRO_EL0, tpidrro_el0);
	SAVE(CONTEXTIDR_EL1, contextidr_el1);
	SAVE(CSSELR_EL1, csselr_el1);
	SAVE(MDSCR_EL1, mdscr_el1);
	SAVE(CNTKCTL_EL1, cntkctl_el1);
	SAVE(CNTP_CTL_EL0, cntp_ctl_el0);
	SAVE(CNTP_CVAL_EL0, cntp_cval_el0);
	SAVE(CNTV_CTL_EL0, cntv_ctl_el0);
	SAVE(CNTV_CVAL_EL0, cntv_cval_el0);
	SAVE(VMPIDR_EL2, vmpidr_el2);
	arm64_fpsimd_save(&vcpu->arch.fpsimd);

	/* not to go off for the next vCPU */
	__asm__ volatile("msr CNTP_CTL_EL0, xzr; msr CNTV_CTL_EL0, xzr");
}

void arch_vcpu_load(struct hv_vcpu *vcpu)
{
	LOAD(SCTLR_EL1, sctlr_el1);
	LOAD(CPACR_EL1, cpacr_el1);
	LOAD(TTBR0_EL1, ttbr0_el1);
	LOAD(TTBR1_EL1, ttbr1_el1);
	LOAD(TCR_EL1, tcr_el1);
	LOAD(MAIR_EL1, mair_el1);
	LOAD(AMAIR_EL1, amair_el1);
	LOAD(VBAR_EL1, vbar_el1);
	LOAD(ESR_EL1, esr_el1);
	LOAD(AFSR0_EL1, afsr0_el1);
	LOAD(AFSR1_EL1, afsr1_el1);
	LOAD(FAR_EL1, far_el1);
	LOAD(PAR_EL1, par_el1);
	LOAD(ELR_EL1, elr_el1);
	LOAD(SPSR_EL1, spsr_el1);
	LOAD(SP_EL1, sp_el1);
	LOAD(SP_EL0, sp_el0);
	LOAD(TPIDR_EL1, tpidr_el1);
	LOAD(TPIDR_EL0, tpidr_el0);
	LOAD(TPIDRRO_EL0, tpidrro_el0);
	LOAD(CONTEXTIDR_EL1, contextidr_el1);
	LOAD(CSSELR_EL1, csselr_el1);
	LOAD(MDSCR_EL1, mdscr_el1);
	LOAD(CNTKCTL_EL1, cntkctl_el1);
	LOAD(CNTP_CVAL_EL0, cntp_cval_el0);
	LOAD(CNTP_CTL_EL0, cntp_ctl_el0);
	LOAD(CNTV_CVAL_EL0, cntv_cval_el0);
	LOAD(CNTV_CTL_EL0, cntv_ctl_el0);
	LOAD(VMPIDR_EL2, vmpidr_el2);
	arm64_fpsimd_load(&vcpu->arch.fpsimd);
	__asm__ volatile("isb" ::: "memory");
}
