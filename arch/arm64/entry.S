#include <hv/config.h>

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

/*
 * Secondary CPUs, started by PSCI CPU_ON on the guest's behalf, enter
 * here at EL2 with h2's CPU index in x0 (CPU_ON's context id).
 */
.text
.global arm64_secondary_entry
.type arm64_secondary_entry, %function
arm64_secondary_entry:
	ldr x1, =hv_cpu_stacks
	add x2, x0, #1
	mov x3, #HV_STACK_SIZE
	madd x1, x2, x3, x1
	mov sp, x1
	bl arm64_secondary_boot

1:
	wfe
	b 1b
