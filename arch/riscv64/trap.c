#include <arch/trap.h>
#include <hv/hv.h>

#define A(n)	HV_TRAP_ARG(n)

static void sbi_call(struct hv_trap_frame *frame)
{
	struct hv_hcall_ret ret;

	if (frame->regs[A(7)] != HV_SBI_EXT) {
		frame->regs[A(0)] = (hv_addr_t)SBI_ERR_NOT_SUPPORTED;
		return;
	}

	ret = hv_hypercall(frame->regs[A(6)], frame->regs[A(0)]);
	frame->regs[A(0)] = ret.error ? (hv_addr_t)SBI_ERR_NOT_SUPPORTED : 0;
	frame->regs[A(1)] = ret.value;
}

void riscv64_handle_trap(struct hv_trap_frame *frame)
{
	switch (frame->cause) {
	case RISCV_CAUSE_ECALL_FROM_VS:
		sbi_call(frame);
		/* sepc points at the ECALL itself */
		frame->pc += 4;
		break;
	default:
		hv_dump_trap(frame->cause & RISCV_CAUSE_INTERRUPT ?
			     "unhandled riscv64 interrupt" :
			     "unhandled riscv64 trap", frame);
		for (;;)
			__asm__ volatile("wfi");
	}
}
