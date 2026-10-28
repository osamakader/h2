ENTRY(_start)

PHDRS
{
	text PT_LOAD FLAGS(5);
	data PT_LOAD FLAGS(6);
}

SECTIONS
{
	/* from the Makefile: where the platform wants h2 */
	. = H2_LOAD_ADDR;
	__image_start = .;

	.text : ALIGN(4096) {
		*(.text.boot)
		*(.text.vectors)
		*(.text.guest)
		*(.text*)
	} :text

	.rodata : ALIGN(4096) {
		*(.rodata*)
	} :text

	.data : ALIGN(4096) {
		*(.data*)
	} :data

	.bss : ALIGN(4096) {
		__bss_start = .;
		*(.bss*)
		*(COMMON)
		__bss_end = .;

		/* in .bss, so the data segment is never empty */
		. = ALIGN(16);
		. += 0x4000;
		__guest_stack_top = .;
	} :data

	__image_end = .;
}
