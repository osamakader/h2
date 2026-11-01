#include <arch/csr.h>
#include <arch/trap.h>
#include <hv/vcpu.h>

#define HSTATUS_SPV	(1UL << 7)
#define HSTATUS_SPVP	(1UL << 8)
#define SSTATUS_SPP	(1UL << 8)

/* VS-mode, MMU off, a0, a1 = arg0, arg1, as SBI firmware starts a kernel */
void arch_vcpu_init(struct hv_vcpu *vcpu, hv_addr_t entry, hv_addr_t stack,
		    hv_addr_t arg0, hv_addr_t arg1)
{
	vcpu->frame.pc = entry;
	vcpu->frame.status = csr_read(sstatus) | SSTATUS_SPP;
	vcpu->frame.regs[1] = stack;		/* x2, sp */
	vcpu->frame.regs[HV_TRAP_ARG(0)] = arg0;
	vcpu->frame.regs[HV_TRAP_ARG(1)] = arg1;
}

void arch_vcpu_load(struct hv_vcpu *vcpu)
{
	csr_write(vsatp, vcpu->arch.vsatp);
	/* sret goes to VS-mode */
	csr_set(hstatus, HSTATUS_SPV | HSTATUS_SPVP);
}
