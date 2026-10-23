#include <hv/config.h>

.section .text.boot, "ax"
.global _start
_start:
	la sp, __stack_top
	call riscv64_boot

1:
	wfi
	j 1b

/*
 * Harts started by SBI HSM hart_start on the guest's behalf enter here
 * in HS-mode with their hartid in a0.
 */
.text
.global riscv64_secondary_entry
riscv64_secondary_entry:
	la sp, hv_cpu_stacks
	addi t0, a0, 1
	li t1, HV_STACK_SIZE
	mul t0, t0, t1
	add sp, sp, t0
	call riscv64_secondary_boot

1:
	wfi
	j 1b
