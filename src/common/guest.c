#include <hv/cpu.h>
#include <hv/fdt.h>
#include <hv/guest.h>
#include <hv/hv.h>
#include <hv/stage2.h>
#include <hv/vcpu.h>

#define SECTION_2M		0x200000UL
#define PAGE_UP(a)		(((a) + 0xfffUL) & ~0xfffUL)
#define FDT_RSV_ENTRY		16

extern const char hv_guest2_image[], hv_guest2_image_end[];
extern char __image_start[], __image_end[];

static int second;

static hv_addr_t refuse(hv_addr_t dtb, const char *why)
{
	hv_console_write("h2: no second guest: ");
	hv_console_write(why);
	hv_console_putchar('\n');
	return dtb;
}

/*
 * RAM in two: guest 0 keeps the lower half, the second guest gets the
 * upper half, as if it were RAM from the same address, with its device
 * tree where qemu puts it, at the start, and its image where it is linked.
 */
hv_addr_t hv_guest2_prepare(hv_addr_t dtb)
{
	struct hv_guest *g = &hv_guests[1];
	hv_addr_t half, offset, end = PAGE_UP((hv_addr_t)__image_end), copy;
	uint64_t base, size;
	uint32_t reg, dtb_size;

	if (&hv_guest2_image[0] == &hv_guest2_image_end[0])
		return dtb;
	if (!dtb || fdt_memory((void *)dtb, &base, &size, &reg) < 0)
		return refuse(dtb, "RAM isn't known from the device tree");
	half = (size / 2) & ~(SECTION_2M - 1);
	offset = half;
	if (end > base + half)
		return refuse(dtb, "h2 isn't in the lower half of RAM (more MEM)");

	g->ram_base = base;
	g->ram_size = half;
	g->pa_offset = offset;
	if (arch_stage2_window(g) < 0)
		return refuse(dtb, "no VMIDs or no page tables");

	/* its image first: nothing of guest 0's up there is touched yet */
	dtb_size = fdt_size((void *)dtb);
	if (dtb_size >= SECTION_2M)
		return refuse(dtb, "the device tree is too big");
	if (hv_load_image(hv_guest2_image, hv_guest2_image_end, offset, base,
			  base + half, 0, &g->entry) != 1 ||
	    g->entry < base + SECTION_2M)
		return refuse(dtb, "its image doesn't load above its first 2MB");

	/* a device tree of its own, saying how much RAM it has */
	fdt_move((void *)dtb, (void *)(base + offset), dtb_size);
	fdt_memory_set_size((void *)(base + offset), reg, half);
	g->dtb = base;
	second = 1;

	/* guest 0's: the same, and it's kept out of the upper half */
	fdt_memory_set_size((void *)dtb, reg, half);
	hv_stage2_hole(base + half, base + size);

	/* a device tree in the upper half moves down, past h2 */
	if (dtb >= base + half) {
		copy = end;
		fdt_move((void *)dtb, (void *)copy, dtb_size + FDT_RSV_ENTRY);
		fdt_add_memreserve((void *)copy, copy, PAGE_UP(dtb_size + FDT_RSV_ENTRY));
		dtb = copy;
		hv_console_write("h2: device tree moved to ");
		hv_console_hex(dtb);
		hv_console_putchar('\n');
	}

	hv_console_write("h2: second guest in ");
	hv_console_hex(base + offset);
	hv_console_putchar('-');
	hv_console_hex(base + offset + half);
	hv_console_putchar('\n');
	return dtb;
}

void hv_guest2_start(int cpu, hv_addr_t hwid)
{
	struct hv_guest *g = &hv_guests[1];

	(void)hwid;
	if (second && !hv_vcpu_new_in(g, cpu, g->entry, 0,
				      ARCH_VCPU_START_ARGS(hwid, g->dtb)))
		hv_console_write("h2: no room for the second guest's vCPU\n");
}
