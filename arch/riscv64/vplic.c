#include <arch/csr.h>
#include <arch/stage2.h>
#include <arch/trap.h>
#include <arch/virq.h>
#include <hv/hv.h>

/*
 * External interrupts for the guest. The hart's supervisor external
 * interrupt (sip.SEIP) comes from its PLIC context and stays up until the
 * guest claims the interrupt, so h2 needs to see the claims: the PLIC's
 * per-context registers (threshold, claim/complete) are left out of stage
 * 2, the guest's accesses to them trap here, and h2 does them on the real
 * PLIC. After each, the guest's external interrupt (hvip.VSEIP) follows
 * the hart's.
 */

#define QEMU_VIRT_PLIC		0x0c000000UL
#define PLIC_CONTEXT_BASE	(QEMU_VIRT_PLIC + 0x200000)
#define PLIC_CONTEXT_END	(QEMU_VIRT_PLIC + 0x400000)
#define PLIC_CONTEXT_SIZE	0x1000UL
#define PLIC_CLAIM		4	/* claim on read, complete on write */

/* qemu virt: hart h has context 2h (M-mode) and 2h + 1 (S-mode) */
#define PLIC_CONTEXT_S(ctx)	((ctx) & 1)

#define OPCODE_LOAD		0x03
#define OPCODE_STORE		0x23
#define FUNCT3_W		2
#define FUNCT3_WU		6
#define C_FUNCT3_LW		2
#define C_FUNCT3_SW		6

void riscv64_vplic_init(void)
{
	riscv64_stage2_hole(PLIC_CONTEXT_BASE, PLIC_CONTEXT_END);
}

/* hvip.VSEIP = sip.SEIP; while it is up, h2 needn't hear about it again */
void riscv64_external_sync(void)
{
	if (csr_read(sip) & (1UL << IRQ_S_EXT)) {
		csr_set(hvip, 1UL << IRQ_VS_EXT);
		csr_clear(sie, 1UL << IRQ_S_EXT);
	} else {
		csr_clear(hvip, 1UL << IRQ_VS_EXT);
		csr_set(sie, 1UL << IRQ_S_EXT);
	}
}

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

/* a guest page fault: 1 if it was a PLIC context access, now done */
int riscv64_vplic_access(struct hv_trap_frame *frame)
{
	hv_addr_t gpa = csr_read(htval) << 2 | (frame->value & 3);
	volatile uint32_t *reg = (volatile uint32_t *)gpa;
	unsigned int rd, rs2, funct3;
	uint32_t insn;
	int len, store;

	if (gpa < PLIC_CONTEXT_BASE || gpa >= PLIC_CONTEXT_END || (gpa & 3) ||
	    !PLIC_CONTEXT_S((gpa - PLIC_CONTEXT_BASE) / PLIC_CONTEXT_SIZE))
		return 0;

	insn = guest_insn(frame, &len);
	if (len == 2 && (insn & 3) == 0) {
		/* C.LW / C.SW: registers x8..x15 */
		funct3 = insn >> 13 & 7;
		rd = rs2 = 8 + (insn >> 2 & 7);
		store = funct3 == C_FUNCT3_SW;
		if (funct3 != C_FUNCT3_LW && !store)
			return 0;
		funct3 = FUNCT3_W;
	} else {
		funct3 = insn >> 12 & 7;
		rd = insn >> 7 & 31;
		rs2 = insn >> 20 & 31;
		store = (insn & 0x7f) == OPCODE_STORE;
		if (store ? funct3 != FUNCT3_W :
		    (insn & 0x7f) != OPCODE_LOAD ||
		    (funct3 != FUNCT3_W && funct3 != FUNCT3_WU))
			return 0;
	}

	if (store)
		*reg = (uint32_t)get_reg(frame, rs2);
	else if (funct3 == FUNCT3_W)
		set_reg(frame, rd, (hv_addr_t)(int64_t)(int32_t)*reg);
	else
		set_reg(frame, rd, *reg);

	if ((gpa & (PLIC_CONTEXT_SIZE - 1)) == PLIC_CLAIM)
		riscv64_external_sync();

	frame->pc += len;
	return 1;
}
