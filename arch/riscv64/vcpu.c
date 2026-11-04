#include <arch/csr.h>
#include <arch/sbi.h>
#include <arch/trap.h>
#include <arch/virq.h>
#include <hv/vcpu.h>

#define HSTATUS_SPV	(1UL << 7)
#define HSTATUS_SPVP	(1UL << 8)
#define SSTATUS_SIE	(1UL << 1)
#define SSTATUS_SPIE	(1UL << 5)
#define SSTATUS_SPP	(1UL << 8)
#define SSTATUS_FS	(3UL << 13)
#define VCPU_HVIP	((1UL << IRQ_VS_SOFT) | (1UL << IRQ_VS_TIMER))

/*
 * VS-mode, MMU off, a0, a1 = arg0, arg1, as SBI firmware starts a kernel;
 * vsstatus as the hart has it (its XLEN fields), but no interrupts.
 */
void arch_vcpu_init(struct hv_vcpu *vcpu, hv_addr_t entry, hv_addr_t stack,
		    hv_addr_t arg0, hv_addr_t arg1)
{
	vcpu->frame.pc = entry;
	vcpu->frame.status = csr_read(sstatus) | SSTATUS_SPP;
	vcpu->frame.regs[1] = stack;		/* x2, sp */
	vcpu->frame.regs[HV_TRAP_ARG(0)] = arg0;
	vcpu->frame.regs[HV_TRAP_ARG(1)] = arg1;
	vcpu->arch.vsstatus = csr_read(vsstatus) &
			       ~(SSTATUS_SIE | SSTATUS_SPIE | SSTATUS_SPP);
	vcpu->arch.stime = ~0UL;
	vcpu->arch.vstimecmp = ~0UL;
}

/* the running vCPU's state, into vcpu */
void arch_vcpu_save(struct hv_vcpu *vcpu)
{
	vcpu->arch.vsstatus = csr_read(vsstatus);
	vcpu->arch.vsie = csr_read(vsie);
	vcpu->arch.vstvec = csr_read(vstvec);
	vcpu->arch.vsscratch = csr_read(vsscratch);
	vcpu->arch.vsepc = csr_read(vsepc);
	vcpu->arch.vscause = csr_read(vscause);
	vcpu->arch.vstval = csr_read(vstval);
	vcpu->arch.vsatp = csr_read(vsatp);
	vcpu->arch.hvip = csr_read(hvip) & VCPU_HVIP;
	csr_clear(hvip, VCPU_HVIP);
	if (riscv64_sstc())
		vcpu->arch.vstimecmp = csr_read(CSR_VSTIMECMP);
	csr_set(sstatus, SSTATUS_FS);
	riscv64_fp_save(&vcpu->arch.fp);
}

void arch_vcpu_load(struct hv_vcpu *vcpu)
{
	csr_write(vsstatus, vcpu->arch.vsstatus);
	csr_write(vsie, vcpu->arch.vsie);
	csr_write(vstvec, vcpu->arch.vstvec);
	csr_write(vsscratch, vcpu->arch.vsscratch);
	csr_write(vsepc, vcpu->arch.vsepc);
	csr_write(vscause, vcpu->arch.vscause);
	csr_write(vstval, vcpu->arch.vstval);
	csr_write(vsatp, vcpu->arch.vsatp);
	csr_set(hvip, vcpu->arch.hvip);
	/* a deadline already past goes off at once: VSTIP */
	riscv64_timer_program();
	if (riscv64_sstc())
		csr_write(CSR_VSTIMECMP, vcpu->arch.vstimecmp);
	csr_set(sstatus, SSTATUS_FS);
	riscv64_fp_load(&vcpu->arch.fp);
	/* sret goes to VS-mode */
	csr_set(hstatus, HSTATUS_SPV | HSTATUS_SPVP);
}

void arch_vcpu_flush_tlb(void)
{
	__asm__ volatile("hfence.vvma zero, zero" ::: "memory");
}
