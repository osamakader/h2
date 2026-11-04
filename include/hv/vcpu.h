#ifndef HV_VCPU_H
#define HV_VCPU_H

#include <arch/vcpu.h>
#include <hv/hv.h>

/*
 * A guest CPU (vCPU). While it runs, its state is in the physical CPU;
 * while it doesn't, it is here: the trap frame (general registers, pc,
 * status) and the architecture's part (system registers, timers, FP).
 * Each physical CPU has a ring of vCPUs and runs one of them at a time.
 */
struct hv_vcpu {
	struct hv_trap_frame frame;
	struct arch_vcpu arch;
	int used;
	int cpu;			/* the physical CPU's slot */
	struct hv_vcpu *next;		/* in that CPU's ring */
};

struct hv_vcpu *hv_vcpu_new(int cpu, hv_addr_t entry, hv_addr_t stack,
			    hv_addr_t arg0, hv_addr_t arg1);
__attribute__((noreturn)) void hv_vcpu_run(struct hv_vcpu *vcpu);
struct hv_vcpu *hv_vcpu_current(void);
struct hv_vcpu *hv_vcpu_first(void);
void hv_vcpu_yield(void);
void hv_vcpu_trap_done(struct hv_trap_frame *frame);

/* the architecture's part */
void arch_vcpu_init(struct hv_vcpu *vcpu, hv_addr_t entry, hv_addr_t stack,
		    hv_addr_t arg0, hv_addr_t arg1);
void arch_vcpu_load(struct hv_vcpu *vcpu);
void arch_vcpu_save(struct hv_vcpu *vcpu);
/* this CPU's next tick, which makes the running vCPU give way */
void arch_tick_start(void);
/* this CPU's TLB entries for the guest's own translations (stage 1) */
void arch_vcpu_flush_tlb(void);
/* copy frame to the top of this CPU's h2 stack and return from a trap */
__attribute__((noreturn))
void arch_vcpu_enter(const struct hv_trap_frame *frame, hv_addr_t stack_top);

#endif
