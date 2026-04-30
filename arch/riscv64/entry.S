.section .text.boot, "ax"
.global _start
_start:
	la sp, __stack_top
	call riscv64_boot

1:
	wfi
	j 1b
