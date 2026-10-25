/*
 * Trap frame: regs[i] holds x(i+1), then pc (sepc), status (sstatus),
 * cause (scause) and value (stval), as in struct hv_trap_frame.
 * sscratch holds the host stack while the guest runs, and 0 while h2 does.
 */
#define REG(n)		(((n) - 1) * 8)
#define FRAME_PC	248
#define FRAME_STATUS	256
#define FRAME_CAUSE	264
#define FRAME_VALUE	272
#define FRAME_SIZE	288

.section .text.trap, "ax"
.align 4
.global __riscv64_trap_vector
__riscv64_trap_vector:
	csrrw sp, sscratch, sp
	bnez sp, 1f
	/* from h2 itself: put sp back, sscratch stays 0 */
	csrrw sp, sscratch, sp
	j riscv64_host_fault

1:
	addi sp, sp, -FRAME_SIZE
	sd x1, REG(1)(sp)
	sd x3, REG(3)(sp)
	sd x4, REG(4)(sp)
	sd x5, REG(5)(sp)
	sd x6, REG(6)(sp)
	sd x7, REG(7)(sp)
	sd x8, REG(8)(sp)
	sd x9, REG(9)(sp)
	sd x10, REG(10)(sp)
	sd x11, REG(11)(sp)
	sd x12, REG(12)(sp)
	sd x13, REG(13)(sp)
	sd x14, REG(14)(sp)
	sd x15, REG(15)(sp)
	sd x16, REG(16)(sp)
	sd x17, REG(17)(sp)
	sd x18, REG(18)(sp)
	sd x19, REG(19)(sp)
	sd x20, REG(20)(sp)
	sd x21, REG(21)(sp)
	sd x22, REG(22)(sp)
	sd x23, REG(23)(sp)
	sd x24, REG(24)(sp)
	sd x25, REG(25)(sp)
	sd x26, REG(26)(sp)
	sd x27, REG(27)(sp)
	sd x28, REG(28)(sp)
	sd x29, REG(29)(sp)
	sd x30, REG(30)(sp)
	sd x31, REG(31)(sp)

	/* the guest's sp was swapped into sscratch; h2 runs now */
	csrr t0, sscratch
	sd t0, REG(2)(sp)
	csrw sscratch, zero

	csrr t0, sepc
	csrr t1, sstatus
	csrr t2, scause
	csrr t3, stval
	sd t0, FRAME_PC(sp)
	sd t1, FRAME_STATUS(sp)
	sd t2, FRAME_CAUSE(sp)
	sd t3, FRAME_VALUE(sp)

	mv a0, sp
	call riscv64_handle_trap

	ld t0, FRAME_PC(sp)
	ld t1, FRAME_STATUS(sp)
	ld t2, REG(2)(sp)
	csrw sepc, t0
	csrw sstatus, t1
	csrw sscratch, t2

	ld x1, REG(1)(sp)
	ld x3, REG(3)(sp)
	ld x4, REG(4)(sp)
	ld x5, REG(5)(sp)
	ld x6, REG(6)(sp)
	ld x7, REG(7)(sp)
	ld x8, REG(8)(sp)
	ld x9, REG(9)(sp)
	ld x10, REG(10)(sp)
	ld x11, REG(11)(sp)
	ld x12, REG(12)(sp)
	ld x13, REG(13)(sp)
	ld x14, REG(14)(sp)
	ld x15, REG(15)(sp)
	ld x16, REG(16)(sp)
	ld x17, REG(17)(sp)
	ld x18, REG(18)(sp)
	ld x19, REG(19)(sp)
	ld x20, REG(20)(sp)
	ld x21, REG(21)(sp)
	ld x22, REG(22)(sp)
	ld x23, REG(23)(sp)
	ld x24, REG(24)(sp)
	ld x25, REG(25)(sp)
	ld x26, REG(26)(sp)
	ld x27, REG(27)(sp)
	ld x28, REG(28)(sp)
	ld x29, REG(29)(sp)
	ld x30, REG(30)(sp)
	ld x31, REG(31)(sp)
	addi sp, sp, FRAME_SIZE
	csrrw sp, sscratch, sp
	sret
