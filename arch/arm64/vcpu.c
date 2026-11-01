#include <hv/vcpu.h>

#define SPSR_EL1H		0x5UL
#define SPSR_DAIF		(0xfUL << 6)
#define SCTLR_EL1_RES1		((1UL << 29) | (1UL << 28) | (1UL << 23) | \
				 (1UL << 22) | (1UL << 20) | (1UL << 11))

/* EL1h, MMU off, interrupts masked, x0 = arg0, as firmware starts Linux */
void arch_vcpu_init(struct hv_vcpu *vcpu, hv_addr_t entry, hv_addr_t stack,
		    hv_addr_t arg0, hv_addr_t arg1)
{
	(void)arg1;
	vcpu->frame.pc = entry;
	vcpu->frame.status = SPSR_EL1H | SPSR_DAIF;
	vcpu->frame.regs[0] = arg0;
	vcpu->arch.sctlr_el1 = SCTLR_EL1_RES1;
	vcpu->arch.sp_el1 = stack;
}

#define LOAD(reg, field) \
	__asm__ volatile("msr " #reg ", %0" :: "r"(vcpu->arch.field))

void arch_vcpu_load(struct hv_vcpu *vcpu)
{
	LOAD(SCTLR_EL1, sctlr_el1);
	LOAD(CPACR_EL1, cpacr_el1);
	LOAD(TTBR0_EL1, ttbr0_el1);
	LOAD(TTBR1_EL1, ttbr1_el1);
	LOAD(TCR_EL1, tcr_el1);
	LOAD(MAIR_EL1, mair_el1);
	LOAD(SP_EL1, sp_el1);
	__asm__ volatile("isb" ::: "memory");
}
