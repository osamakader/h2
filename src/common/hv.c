#include <hv/fdt.h>
#include <hv/hv.h>

#define HV_PAGE_SIZE	0x1000UL

extern char __image_start[], __image_end[];

/*
 * Bring h2 up. dtb is the firmware's device tree, or 0; returns the one
 * for the guest: the same, with h2's image reserved, or 0 if none.
 */
hv_addr_t hv_main(hv_addr_t dtb)
{
	hv_addr_t start = (hv_addr_t)__image_start;
	hv_addr_t end = ((hv_addr_t)__image_end + HV_PAGE_SIZE - 1) &
			~(HV_PAGE_SIZE - 1);

	hv_console_write("h2: hypervisor online\n");

	if (!fdt_valid((void *)dtb)) {
		hv_console_write("h2: no device tree\n");
		return 0;
	}

	hv_console_write("h2: device tree at ");
	hv_console_hex(dtb);
	if (fdt_add_memreserve((void *)dtb, start, end - start) < 0) {
		hv_console_write(", no room in it to reserve h2\n");
	} else {
		hv_console_write(", h2 reserved ");
		hv_console_hex(start);
		hv_console_putchar('-');
		hv_console_hex(end);
		hv_console_putchar('\n');
	}

	return dtb;
}

struct hv_hcall_ret hv_hypercall(hv_addr_t nr, hv_addr_t arg0)
{
	struct hv_hcall_ret ret = {
		.error = HV_OK,
		.value = 0,
	};

	switch (nr) {
	case HV_HCALL_PUTC:
		hv_console_putchar((char)arg0);
		break;
	default:
		ret.error = HV_ENOSYS;
		break;
	}

	return ret;
}

void hv_console_write(const char *text)
{
	while (*text)
		hv_console_putchar(*text++);
}

void hv_console_hex(hv_addr_t value)
{
	static const char digits[] = "0123456789abcdef";
	int shift;

	hv_console_write("0x");
	for (shift = (int)sizeof(value) * 8 - 4; shift >= 0; shift -= 4)
		hv_console_putchar(digits[(value >> shift) & 0xf]);
}

static void dump_field(const char *name, hv_addr_t value)
{
	hv_console_write(name);
	hv_console_hex(value);
	hv_console_putchar('\n');
}

void hv_dump_trap(const char *what, const struct hv_trap_frame *frame)
{
	hv_console_write("h2: ");
	hv_console_write(what);
	hv_console_putchar('\n');
	dump_field("  cause  ", frame->cause);
	dump_field("  pc     ", frame->pc);
	dump_field("  value  ", frame->value);
	dump_field("  status ", frame->status);
}
