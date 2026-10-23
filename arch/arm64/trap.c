#include <arch/trap.h>
#include <hv/hv.h>

static void smccc_call(struct hv_trap_frame *frame)
{
	uint32_t fid = (uint32_t)frame->regs[0];
	struct hv_hcall_ret ret;

	switch (SMCCC_OWNER(fid)) {
	case SMCCC_OWNER_VENDOR_HYP:
		ret = hv_hypercall(SMCCC_NUMBER(fid), frame->regs[1]);
		frame->regs[0] = ret.error ? (hv_addr_t)SMCCC_RET_NOT_SUPPORTED : 0;
		frame->regs[1] = ret.value;
		break;
	default:
		frame->regs[0] = (hv_addr_t)SMCCC_RET_NOT_SUPPORTED;
		break;
	}
}

void arm64_handle_trap(struct hv_trap_frame *frame)
{
	switch (ARM64_ESR_EC(frame->cause)) {
	case ARM64_ESR_EC_HVC64:
		/* ELR_EL2 already points past the HVC */
		smccc_call(frame);
		break;
	default:
		hv_dump_trap("unhandled arm64 trap", frame);
		for (;;)
			__asm__ volatile("wfe");
	}
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
