#include <hv/fdt.h>

/*
 * Just enough of the devicetree blob format to edit one where the
 * firmware left it. Byte accesses only: with the MMU off, arm64 EL2 sees
 * all memory as Device memory, where unaligned accesses fault.
 */

#define FDT_MAGIC		0xd00dfeedU
#define FDT_VERSION_MIN		16

/* header fields, as offsets into the header */
#define FDT_MAGIC_OFF		0
#define FDT_TOTALSIZE		4
#define FDT_OFF_DT_STRUCT	8
#define FDT_OFF_DT_STRINGS	12
#define FDT_OFF_MEM_RSVMAP	16
#define FDT_VERSION		20
#define FDT_SIZE_DT_STRINGS	32

#define FDT_RSV_ENTRY_SIZE	16	/* be64 address, be64 size */

static uint32_t get32(const void *base, uint32_t off)
{
	const volatile uint8_t *p = (const volatile uint8_t *)base + off;

	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
	       (uint32_t)p[2] << 8 | p[3];
}

static void put32(void *base, uint32_t off, uint32_t value)
{
	volatile uint8_t *p = (volatile uint8_t *)base + off;

	p[0] = value >> 24;
	p[1] = value >> 16;
	p[2] = value >> 8;
	p[3] = value;
}

static uint64_t get64(const void *base, uint32_t off)
{
	return (uint64_t)get32(base, off) << 32 | get32(base, off + 4);
}

static void put64(void *base, uint32_t off, uint64_t value)
{
	put32(base, off, value >> 32);
	put32(base, off + 4, (uint32_t)value);
}

int fdt_valid(const void *fdt)
{
	return fdt && get32(fdt, FDT_MAGIC_OFF) == FDT_MAGIC &&
	       get32(fdt, FDT_VERSION) >= FDT_VERSION_MIN;
}

/*
 * Add a /memreserve/ entry. The entries come right before the structure
 * block, so everything from the terminating entry to the end of the
 * strings block moves up into the free space at the end of the blob.
 */
int fdt_add_memreserve(void *fdt, uint64_t addr, uint64_t size)
{
	uint32_t rsvmap, structs, strings, end, entry, i;
	volatile uint8_t *p = fdt;

	if (!fdt_valid(fdt))
		return -1;

	rsvmap = get32(fdt, FDT_OFF_MEM_RSVMAP);
	structs = get32(fdt, FDT_OFF_DT_STRUCT);
	strings = get32(fdt, FDT_OFF_DT_STRINGS);
	end = strings + get32(fdt, FDT_SIZE_DT_STRINGS);

	/* the usual layout: rsvmap, structure, strings, free space */
	if (rsvmap >= structs || structs > strings ||
	    end + FDT_RSV_ENTRY_SIZE > get32(fdt, FDT_TOTALSIZE))
		return -1;

	for (entry = rsvmap; get64(fdt, entry) || get64(fdt, entry + 8);
	     entry += FDT_RSV_ENTRY_SIZE)
		if (entry + FDT_RSV_ENTRY_SIZE >= structs)
			return -1;

	for (i = end; i > entry; i--)
		p[i - 1 + FDT_RSV_ENTRY_SIZE] = p[i - 1];

	put64(fdt, entry, addr);
	put64(fdt, entry + 8, size);
	put32(fdt, FDT_OFF_DT_STRUCT, structs + FDT_RSV_ENTRY_SIZE);
	put32(fdt, FDT_OFF_DT_STRINGS, strings + FDT_RSV_ENTRY_SIZE);

	return 0;
}
