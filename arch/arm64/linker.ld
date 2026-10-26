ENTRY(_start)

PHDRS
{
	text PT_LOAD FLAGS(5);
	data PT_LOAD FLAGS(6);
}

SECTIONS
{
	/*
	 * qemu puts the DTB (1MB) at the start of RAM, 0x40000000, only if
	 * it fits below the lowest ELF segment
	 */
	. = 0x40100000;
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
