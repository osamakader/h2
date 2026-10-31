#include <arch/platform.h>
#include <hv/config.h>

/*
 * EL2's own controls, not whatever the firmware left: MMU, D-cache and
 * alignment checks off, the I-cache on (invalidated first). SCTLR_EL2's
 * RES1 bits are 29, 28, 23, 22, 18, 16, 11, 5 and 4.
 */
#define SCTLR_EL2_RES1	0x30c50830
#define SCTLR_EL2_I	(1 << 12)

.macro el2_controls
	ic iallu
	dsb nsh
	isb
	ldr x9, =(SCTLR_EL2_RES1 | SCTLR_EL2_I)
	msr sctlr_el2, x9
	isb
.endm

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
	/* only in EL2: elsewhere arm64_boot() says so and stops */
	mrs x9, CurrentEL
	cmp x9, #(2 << 2)
	b.ne 2f
	el2_controls
2:
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
	el2_controls
	ldr x1, =hv_cpu_stacks
	add x2, x0, #1
	mov x3, #HV_STACK_SIZE
	madd x1, x2, x3, x1
	mov sp, x1
	bl arm64_secondary_boot

1:
	wfe
	b 1b

#ifdef PLATFORM_SPIN_TABLE
/*
 * Secondary CPUs h2 takes from the firmware's spin table enter here at
 * EL2 (arm64_spin_take); their slot is in arm64_spin_slot[Aff0].
 */
.global arm64_spin_entry
.type arm64_spin_entry, %function
arm64_spin_entry:
	el2_controls
	mrs x0, mpidr_el1
	and x0, x0, #(PLATFORM_SPIN_CPUS - 1)
	ldr x1, =arm64_spin_slot
	ldrsw x2, [x1, x0, lsl #2]
	add x2, x2, #1
	ldr x1, =hv_cpu_stacks
	mov x3, #HV_STACK_SIZE
	madd x1, x2, x3, x1
	mov sp, x1
	bl arm64_spin_park

1:
	wfe
	b 1b
#endif
