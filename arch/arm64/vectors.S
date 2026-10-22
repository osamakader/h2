/* an entry h2 doesn't expect: report its index and stop */
.macro vector_unhandled index
	.align 7
	mov x0, #\index
	b arm64_unhandled_exception
.endm

.section .text.vectors, "ax"
.align 11
.global __arm64_vectors
__arm64_vectors:
	vector_unhandled 0	/* current EL, SP_EL0: sync */
	vector_unhandled 1	/*   IRQ */
	vector_unhandled 2	/*   FIQ */
	vector_unhandled 3	/*   SError */
	vector_unhandled 4	/* current EL, SP_ELx: sync */
	vector_unhandled 5	/*   IRQ */
	vector_unhandled 6	/*   FIQ */
	vector_unhandled 7	/*   SError */

	.align 7		/* lower EL, AArch64: sync */
	b arm64_sync_lower_aarch64
	vector_unhandled 9	/*   IRQ */
	vector_unhandled 10	/*   FIQ */
	vector_unhandled 11	/*   SError */
	vector_unhandled 12	/* lower EL, AArch32: sync */
	vector_unhandled 13	/*   IRQ */
	vector_unhandled 14	/*   FIQ */
	vector_unhandled 15	/*   SError */

.text
.global arm64_sync_lower_aarch64
.type arm64_sync_lower_aarch64, %function
arm64_sync_lower_aarch64:
	sub sp, sp, #288
	stp x0, x1, [sp, #0]
	stp x2, x3, [sp, #16]
	stp x4, x5, [sp, #32]
	stp x6, x7, [sp, #48]
	stp x8, x9, [sp, #64]
	stp x10, x11, [sp, #80]
	stp x12, x13, [sp, #96]
	stp x14, x15, [sp, #112]
	stp x16, x17, [sp, #128]
	stp x18, x19, [sp, #144]
	stp x20, x21, [sp, #160]
	stp x22, x23, [sp, #176]
	stp x24, x25, [sp, #192]
	stp x26, x27, [sp, #208]
	stp x28, x29, [sp, #224]
	str x30, [sp, #240]

	mrs x2, ELR_EL2
	mrs x3, SPSR_EL2
	mrs x1, ESR_EL2
	mrs x4, FAR_EL2
	str x2, [sp, #248]
	str x3, [sp, #256]
	str x1, [sp, #264]
	str x4, [sp, #272]

	mov x0, sp
	bl arm64_handle_trap

	ldr x2, [sp, #248]
	ldr x3, [sp, #256]
	msr ELR_EL2, x2
	msr SPSR_EL2, x3

	ldp x0, x1, [sp, #0]
	ldp x2, x3, [sp, #16]
	ldp x4, x5, [sp, #32]
	ldp x6, x7, [sp, #48]
	ldp x8, x9, [sp, #64]
	ldp x10, x11, [sp, #80]
	ldp x12, x13, [sp, #96]
	ldp x14, x15, [sp, #112]
	ldp x16, x17, [sp, #128]
	ldp x18, x19, [sp, #144]
	ldp x20, x21, [sp, #160]
	ldp x22, x23, [sp, #176]
	ldp x24, x25, [sp, #192]
	ldp x26, x27, [sp, #208]
	ldp x28, x29, [sp, #224]
	ldr x30, [sp, #240]
	add sp, sp, #288
	eret

.global arm64_unhandled_exception
.type arm64_unhandled_exception, %function
arm64_unhandled_exception:
	bl arm64_unhandled_vector
1:
	wfe
	b 1b
