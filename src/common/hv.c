#include <hv/hv.h>

void hv_main(void)
{
	hv_console_write("h2: hypervisor online\n");
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
