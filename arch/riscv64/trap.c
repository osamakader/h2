#include <arch/trap.h>
#include <hv/hv.h>

void riscv64_handle_trap(struct hv_trap_frame *frame)
{
	enum hv_trap_reason reason = HV_TRAP_UNKNOWN;

	if (frame->cause == RISCV_CAUSE_ECALL_FROM_VS)
		reason = HV_TRAP_HYPERCALL;

	if (!hv_handle_trap(reason, frame).handled) {
		hv_dump_trap(frame->cause & RISCV_CAUSE_INTERRUPT ?
			     "unhandled riscv64 interrupt" :
			     "unhandled riscv64 trap", frame);
		for (;;)
			__asm__ volatile("wfi");
	}

	/* sepc points at the ECALL itself */
	frame->pc += 4;
}
