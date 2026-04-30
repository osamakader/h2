.section .text.boot, "ax"
.global _start
.type _start, %function
_start:
	ldr x0, =__stack_top
	mov sp, x0
	bl arm64_boot

1:
	wfe
	b 1b
