#include <hv/config.h>

.section .text.boot, "ax"
.global _start
.type _start, %function
_start:
	/* x0: the DTB, from a boot loader that starts us like Linux */
	ldr x1, =hv_cpu_stacks		/* slot 0's stack */
	mov x2, #HV_STACK_SIZE
	add sp, x1, x2
	bl arm64_boot

1:
	wfe
	b 1b

/*
 * Secondary CPUs, started by PSCI CPU_ON on the guest's behalf, enter
 * here at EL2 with their h2 slot in x0 (CPU_ON's context id).
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
