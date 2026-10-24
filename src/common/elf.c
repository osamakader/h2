#include <arch/elf.h>
#include <hv/fdt.h>
#include <hv/hv.h>

/*
 * Load the embedded guest: copy each PT_LOAD segment to its physical
 * address, zero the rest of it, and return the entry point.
 */

#define EI_NIDENT	16
#define ELFCLASS64	2
#define ELFDATA2LSB	1
#define ET_EXEC		2
#define PT_LOAD		1

struct elf64_ehdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct elf64_phdr {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

extern const char hv_guest_image[], hv_guest_image_end[];
extern char __image_start[], __image_end[];

static int overlaps(uint64_t a, uint64_t a_end, uint64_t b, uint64_t b_end)
{
	return a < b_end && b < a_end;
}

static int guest_error(const char *why)
{
	hv_console_write("h2: guest: ");
	hv_console_write(why);
	hv_console_putchar('\n');
	return -1;
}

/* the ELF header and program headers, checked; 0 if they aren't sound */
static const struct elf64_phdr *guest_phdrs(const struct elf64_ehdr *eh,
					    uint64_t size)
{
	if (size < sizeof(*eh) || eh->e_ident[0] != 0x7f ||
	    eh->e_ident[1] != 'E' || eh->e_ident[2] != 'L' ||
	    eh->e_ident[3] != 'F')
		return 0;
	if (eh->e_ident[4] != ELFCLASS64 || eh->e_ident[5] != ELFDATA2LSB ||
	    eh->e_type != ET_EXEC || eh->e_machine != ARCH_ELF_MACHINE)
		return 0;
	if (eh->e_phentsize != sizeof(struct elf64_phdr) ||
	    eh->e_phoff % 8 ||
	    eh->e_phoff + (uint64_t)eh->e_phnum * sizeof(struct elf64_phdr) > size)
		return 0;
	return (const struct elf64_phdr *)((const char *)eh + eh->e_phoff);
}

/*
 * 1 and the entry in *entry if a guest was loaded, 0 if none is
 * embedded, -1 if it can't be loaded. dtb (or 0) is kept out of its way.
 */
int hv_load_guest(hv_addr_t dtb, hv_addr_t *entry)
{
	const struct elf64_ehdr *eh = (const void *)hv_guest_image;
	uint64_t size = (uint64_t)(hv_guest_image_end - hv_guest_image);
	uint64_t dtb_end = dtb ? dtb + fdt_size((void *)dtb) : 0;
	const struct elf64_phdr *ph;
	uint64_t low = ~0UL, high = 0, i, j;

	if (size == 0)
		return 0;
	if (!(ph = guest_phdrs(eh, size)))
		return guest_error("not an ELF executable for this CPU");

	/* check every segment before writing anything */
	for (i = 0; i < eh->e_phnum; i++) {
		uint64_t start = ph[i].p_paddr, end = start + ph[i].p_memsz;

		if (ph[i].p_type != PT_LOAD)
			continue;
		if (ph[i].p_filesz > ph[i].p_memsz ||
		    ph[i].p_offset + ph[i].p_filesz > size || end < start)
			return guest_error("segment out of bounds");
		if (overlaps(start, end, (uint64_t)__image_start,
			     (uint64_t)__image_end))
			return guest_error("segment overlaps h2");
		if (overlaps(start, end, dtb, dtb_end))
			return guest_error("segment overlaps the device tree");
		if (start < low)
			low = start;
		if (end > high)
			high = end;
	}
	if (high == 0)
		return guest_error("no loadable segments");

	/* byte accesses: Device memory on arm64 with the MMU off */
	for (i = 0; i < eh->e_phnum; i++) {
		volatile char *dst = (volatile char *)ph[i].p_paddr;
		const volatile char *src = hv_guest_image + ph[i].p_offset;

		if (ph[i].p_type != PT_LOAD)
			continue;
		for (j = 0; j < ph[i].p_filesz; j++)
			dst[j] = src[j];
		for (; j < ph[i].p_memsz; j++)
			dst[j] = 0;
	}

	/* a virtual entry point: the physical address it is loaded at */
	*entry = eh->e_entry;
	for (i = 0; i < eh->e_phnum; i++)
		if (ph[i].p_type == PT_LOAD && eh->e_entry >= ph[i].p_vaddr &&
		    eh->e_entry - ph[i].p_vaddr < ph[i].p_memsz)
			*entry = ph[i].p_paddr + (eh->e_entry - ph[i].p_vaddr);

	hv_console_write("h2: guest loaded at ");
	hv_console_hex(low);
	hv_console_putchar('-');
	hv_console_hex(high);
	hv_console_write(", entry ");
	hv_console_hex(*entry);
	hv_console_putchar('\n');

	return 1;
}
