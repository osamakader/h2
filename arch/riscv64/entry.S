#include <hv/config.h>

.section .text.boot, "ax"
.global _start
_start:
	/*
	 * Zero .bss: boot loaders leave whatever that memory held. Only
	 * here, on the boot hart: the others come later, to a .bss in use.
	 */
	la t0, __bss_start
	la t1, __bss_end
1:	bgeu t0, t1, 2f
	sd zero, 0(t0)
	addi t0, t0, 8
	j 1b
2:
	/* a0: hartid, a1: the DTB, from the firmware; slot 0's stack */
	la sp, hv_cpu_stacks
	li t0, HV_STACK_SIZE
	add sp, sp, t0
	csrw sscratch, zero
	la t0, __riscv64_trap_vector
	csrw stvec, t0
	call riscv64_boot

1:
	wfi
	j 1b

/*
 * Harts started by SBI HSM hart_start on the guest's behalf enter here
 * in HS-mode with their hartid in a0 and their h2 slot in a1 (opaque).
 */
.text
.global riscv64_secondary_entry
riscv64_secondary_entry:
	la sp, hv_cpu_stacks
	addi t0, a1, 1
	li t1, HV_STACK_SIZE
	mul t0, t0, t1
	add sp, sp, t0
	csrw sscratch, zero
	la t0, __riscv64_trap_vector
	csrw stvec, t0
	call riscv64_secondary_boot

1:
	wfi
	j 1b
