#include <arch/psci.h>
#include <arch/trap.h>
#include <arch/vgic.h>
#include <hv/hv.h>
#include <hv/mmio.h>
#include <hv/vcpu.h>

static void smccc_call(struct hv_trap_frame *frame)
{
	uint32_t fid = (uint32_t)frame->regs[0];
	struct hv_hcall_ret ret;

	switch (SMCCC_OWNER(fid)) {
	case SMCCC_OWNER_VENDOR_HYP:
		ret = hv_hypercall(SMCCC_NUMBER(fid), frame->regs[1],
				   frame->regs[2]);
		/* HV_ENOSYS is SMCCC's NOT_SUPPORTED */
		frame->regs[0] = (hv_addr_t)(long)ret.error;
		frame->regs[1] = ret.value;
		break;
	case SMCCC_OWNER_STANDARD:
		if (arm64_psci_call(frame))
			break;
		/* fall through */
	default:
		frame->regs[0] = (hv_addr_t)SMCCC_RET_NOT_SUPPORTED;
		break;
	}
}

/* data abort syndrome (ESR_EL2.ISS), when ISV says it is valid */
#define DABT_ISV		(1UL << 24)
#define DABT_SAS(iss)		(((iss) >> 22) & 3)	/* size: 1 << SAS */
#define DABT_SSE		(1UL << 21)		/* sign extend */
#define DABT_SRT(iss)		(((iss) >> 16) & 0x1f)	/* the register */
#define DABT_SF			(1UL << 15)		/* 64-bit register */
#define DABT_S1PTW		(1UL << 7)		/* on its stage 1 walk */
#define DABT_WNR		(1UL << 6)		/* a write */

/* the faulting IPA (HPFAR_EL2.FIPA: its page) */
static hv_addr_t fault_ipa(const struct hv_trap_frame *frame)
{
	unsigned long hpfar;

	__asm__ volatile("mrs %0, HPFAR_EL2" : "=r"(hpfar));
	return (hpfar >> 4) << 12 | (frame->value & 0xfff);
}

/*
 * A data abort from the guest at a hole in stage 2: 1 if it was an
 * emulated device's, now done. Only single loads and stores (ISV):
 * not pairs, nor with writeback.
 */
static int mmio_abort(struct hv_trap_frame *frame)
{
	unsigned long iss = frame->cause & 0x1ffffff;
	unsigned int rt = DABT_SRT(iss);
	int size = 1 << DABT_SAS(iss), write = (iss & DABT_WNR) != 0;
	uint64_t value = 0;

	if (!(iss & DABT_ISV) || (iss & DABT_S1PTW))
		return 0;

	if (write && rt != 31)
		value = frame->regs[rt];
	if (size < 8)
		value &= (1UL << (8 * size)) - 1;
	if (!hv_mmio_emulate(fault_ipa(frame), size, write, &value))
		return 0;

	if (!write && rt != 31) {
		if ((iss & DABT_SSE) && size < 8 && (value >> (8 * size - 1) & 1))
			value |= ~0UL << (8 * size);
		if (!(iss & DABT_SF))
			value &= 0xffffffffUL;
		frame->regs[rt] = value;
	}
	frame->pc += 4;
	return 1;
}

void arm64_handle_trap(struct hv_trap_frame *frame)
{
	switch (ARM64_ESR_EC(frame->cause)) {
	case ARM64_ESR_EC_HVC64:
		/* ELR_EL2 already points past the HVC */
		smccc_call(frame);
		break;
	case ARM64_ESR_EC_SMC64:
		/* trapped by HCR_EL2.TSC: ELR_EL2 points at the SMC itself */
		smccc_call(frame);
		frame->pc += 4;
		break;
	case ARM64_ESR_EC_SYSREG:
		if (arm64_vgic_sysreg(frame))
			break;
		goto unhandled;
	case ARM64_ESR_EC_DABT_LOW:
		if (mmio_abort(frame))
			break;
		/* fall through */
	default:
	unhandled:
		hv_console_lock();
		hv_dump_trap("unhandled arm64 trap", frame);
		if (ARM64_ESR_EC(frame->cause) == ARM64_ESR_EC_IABT_LOW ||
		    ARM64_ESR_EC(frame->cause) == ARM64_ESR_EC_DABT_LOW) {
			hv_console_write("  ipa    ");
			hv_console_hex(fault_ipa(frame));
			hv_console_putchar('\n');
		}
		hv_console_unlock();
		for (;;)
			__asm__ volatile("wfe");
	}

	hv_vcpu_trap_done(frame);
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

	hv_console_lock();
	hv_console_write("h2: unexpected exception, vector ");
	hv_console_hex(vector);
	hv_console_putchar('\n');
	hv_dump_trap("state", &frame);
	hv_console_unlock();
}
