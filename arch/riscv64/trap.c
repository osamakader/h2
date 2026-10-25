#include <arch/csr.h>
#include <arch/sbi.h>
#include <arch/trap.h>
#include <arch/virq.h>
#include <hv/hv.h>

void riscv64_handle_trap(struct hv_trap_frame *frame)
{
	switch (frame->cause) {
	case RISCV_CAUSE_ECALL_FROM_VS:
		riscv64_sbi_call(frame);
		/* sepc points at the ECALL itself */
		frame->pc += 4;
		break;
	case RISCV_CAUSE_INTERRUPT | IRQ_S_SOFT:
		riscv64_soft_interrupt();
		break;
	case RISCV_CAUSE_INTERRUPT | IRQ_S_TIMER:
		riscv64_timer_interrupt();
		break;
	default:
		hv_dump_trap(frame->cause & RISCV_CAUSE_INTERRUPT ?
			     "unhandled riscv64 interrupt" :
			     "unhandled riscv64 trap", frame);
		if (frame->cause == RISCV_CAUSE_FETCH_GUEST_PAGE_FAULT ||
		    frame->cause == RISCV_CAUSE_LOAD_GUEST_PAGE_FAULT ||
		    frame->cause == RISCV_CAUSE_STORE_GUEST_PAGE_FAULT) {
			/* htval: the guest physical address, shifted right by 2 */
			hv_console_write("  gpa    ");
			hv_console_hex(csr_read(htval) << 2 | (frame->value & 3));
			hv_console_putchar('\n');
		}
		for (;;)
			__asm__ volatile("wfi");
	}
}
