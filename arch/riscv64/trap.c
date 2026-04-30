#include <arch/trap.h>
#include <hv/hv.h>

void riscv64_handle_trap(struct hv_trap_frame *frame)
{
	enum hv_trap_reason reason = HV_TRAP_UNKNOWN;

	if (frame->cause == RISCV_CAUSE_ECALL_FROM_VS)
		reason = HV_TRAP_HYPERCALL;

	if (!hv_handle_trap(reason, frame).handled)
		for (;;)
			__asm__ volatile("wfi");

	frame->pc += 4;
}
