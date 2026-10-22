#include <arch/trap.h>
#include <hv/hv.h>

void arm64_handle_trap(struct hv_trap_frame *frame)
{
	enum hv_trap_reason reason = HV_TRAP_UNKNOWN;

	if (ARM64_ESR_EC(frame->cause) == ARM64_ESR_EC_HVC64)
		reason = HV_TRAP_HYPERCALL;

	if (!hv_handle_trap(reason, frame).handled) {
		hv_dump_trap("unhandled arm64 trap", frame);
		for (;;)
			__asm__ volatile("wfe");
	}

	/*
	 * For HVC, ELR_EL2 already contains the preferred return address.
	 * RISC-V ECALL is different and advances sepc in its arch handler.
	 */
}

/*
 * Exceptions h2 doesn't expect at all (from EL2 itself, IRQ/FIQ/SError,
 * AArch32): vector is the entry's index in the vector table.
 */
void arm64_unhandled_vector(unsigned long vector)
{
	/* only the fields hv_dump_trap() prints; no memset in h2 */
	struct hv_trap_frame frame;

	__asm__ volatile("mrs %0, ELR_EL2" : "=r"(frame.pc));
	__asm__ volatile("mrs %0, SPSR_EL2" : "=r"(frame.status));
	__asm__ volatile("mrs %0, ESR_EL2" : "=r"(frame.cause));
	__asm__ volatile("mrs %0, FAR_EL2" : "=r"(frame.value));

	hv_console_write("h2: unexpected exception, vector ");
	hv_console_hex(vector);
	hv_console_putchar('\n');
	hv_dump_trap("state", &frame);
}
