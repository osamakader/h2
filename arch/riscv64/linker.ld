ENTRY(_start)

PHDRS
{
	text PT_LOAD FLAGS(5);
	data PT_LOAD FLAGS(6);
}

SECTIONS
{
	/*
	 * Not 0x80200000, where OpenSBI starts S-mode kernels: that is the
	 * guest's. OpenSBI jumps to our ELF entry, wherever it is.
	 */
	. = 0x84000000;
	__image_start = .;

	.text : ALIGN(4096) {
		*(.text.boot)
		*(.text.trap)
		*(.text.guest)
		*(.text*)
	} :text

	.rodata : ALIGN(4096) {
		*(.rodata*)
	} :text

	/* with the small data the compiler puts in .sdata / .sbss */
	.data : ALIGN(4096) {
		*(.data*)
		*(.sdata*)
	} :data

	.bss : ALIGN(4096) {
		__bss_start = .;
		*(.bss*)
		*(.sbss*)
		*(COMMON)
		__bss_end = .;

		/* in .bss, so the data segment is never empty */
		. = ALIGN(16);
		. += 0x4000;
		__guest_stack_top = .;
	} :data

	__image_end = .;
}
