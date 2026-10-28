#include <arch/platform.h>
#include <hv/config.h>

.section .text.boot, "ax"
.global _start
.type _start, %function
_start:
	/*
	 * An arm64 Linux Image header, so that boot loaders (the Pi firmware,
	 * qemu -kernel with a raw image) load h2 where it is linked and start
	 * it as they start Linux: in EL2, with the DTB in x0.
	 */
	b 1f				/* code0 */
	.long 0				/* code1 */
	.quad __image_start - PLATFORM_RAM_BASE	/* text_offset */
	.quad __image_size		/* image_size, with .bss */
	.quad 0x2			/* flags: little-endian, 4KB pages */
	.quad 0, 0, 0
	.ascii "ARM\x64"		/* magic */
	.long 0
1:
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
