#include <arch/csr.h>
#include <arch/mmio.h>
#include <hv/hv.h>
#include <hv/mmio.h>

/*
 * A guest page fault at a hole in stage 2: decode the load or store
 * (htinst, or the instruction itself, read as the guest would fetch it)
 * and hand it to the emulated device. Loads and stores of 1-8 bytes,
 * and their compressed forms C.LW, C.LD, C.SW, C.SD.
 */

#define OPCODE_LOAD	0x03
#define OPCODE_STORE	0x23
#define FUNCT3_UNSIGNED	4		/* LBU, LHU, LWU */
#define C_FUNCT3_LW	2
#define C_FUNCT3_LD	3
#define C_FUNCT3_SW	6
#define C_FUNCT3_SD	7

static unsigned long guest_load_insn_half(hv_addr_t va)
{
	unsigned long half;

	/* as the guest would fetch it: its privilege, its translation */
	__asm__ volatile("hlvx.hu %0, (%1)" : "=r"(half) : "r"(va) : "memory");
	return half;
}

/* the faulting instruction, and its length */
static uint32_t guest_insn(const struct hv_trap_frame *frame, int *len)
{
	unsigned long tinst = csr_read(htinst), lo;

	/* transformed by the hardware: bit 1 clear if it was compressed */
	if (tinst & 1) {
		*len = (tinst & 2) ? 4 : 2;
		return (uint32_t)tinst | 2;
	}

	lo = guest_load_insn_half(frame->pc);
	if ((lo & 3) != 3) {
		*len = 2;
		return (uint32_t)lo;
	}
	*len = 4;
	return (uint32_t)(lo | guest_load_insn_half(frame->pc + 2) << 16);
}

static hv_addr_t get_reg(const struct hv_trap_frame *frame, unsigned int x)
{
	return x ? frame->regs[x - 1] : 0;
}

static void set_reg(struct hv_trap_frame *frame, unsigned int x, hv_addr_t v)
{
	if (x)
		frame->regs[x - 1] = v;
}

int riscv64_mmio_access(struct hv_trap_frame *frame)
{
	hv_addr_t gpa = csr_read(htval) << 2 | (frame->value & 3);
	unsigned int reg, funct3;
	uint32_t insn;
	int len, size, store, sign;
	uint64_t value = 0;

	/* htinst gives a compressed one in its 32-bit form, len still 2 */
	insn = guest_insn(frame, &len);
	if ((insn & 3) != 3) {
		/* quadrant 0: registers x8..x15 */
		if ((insn & 3) != 0)
			return 0;
		funct3 = insn >> 13 & 7;
		reg = 8 + (insn >> 2 & 7);
		store = funct3 == C_FUNCT3_SW || funct3 == C_FUNCT3_SD;
		if (funct3 == C_FUNCT3_LW || funct3 == C_FUNCT3_SW)
			size = 4;
		else if (funct3 == C_FUNCT3_LD || funct3 == C_FUNCT3_SD)
			size = 8;
		else
			return 0;
		sign = 1;
	} else {
		funct3 = insn >> 12 & 7;
		store = (insn & 0x7f) == OPCODE_STORE;
		if (!store && (insn & 0x7f) != OPCODE_LOAD)
			return 0;
		if (store ? funct3 > 3 : funct3 == 7)
			return 0;
		reg = store ? insn >> 20 & 31 : insn >> 7 & 31;
		size = 1 << (funct3 & 3);
		sign = !(funct3 & FUNCT3_UNSIGNED);
	}

	if (store) {
		value = get_reg(frame, reg);
		if (size < 8)
			value &= (1UL << (8 * size)) - 1;
	}
	if (!hv_mmio_emulate(gpa, size, store, &value))
		return 0;

	if (!store) {
		if (sign && size < 8 && (value >> (8 * size - 1) & 1))
			value |= ~0UL << (8 * size);
		set_reg(frame, reg, value);
	}
	frame->pc += len;
	return 1;
}
