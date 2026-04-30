#include <arch/trap.h>
#include <hv/hv.h>

void arm64_handle_trap(struct hv_trap_frame *frame)
{
	enum hv_trap_reason reason = HV_TRAP_UNKNOWN;

	if (ARM64_ESR_EC(frame->cause) == ARM64_ESR_EC_HVC64)
		reason = HV_TRAP_HYPERCALL;

	if (!hv_handle_trap(reason, frame).handled) {
		hv_console_write("h2: unhandled arm64 trap\n");
		for (;;)
			__asm__ volatile("wfe");
	}

	/*
	 * For HVC, ELR_EL2 already contains the preferred return address.
	 * RISC-V ECALL is different and advances sepc in its arch handler.
	 */
}
