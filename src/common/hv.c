#include <arch/trap.h>
#include <hv/hv.h>

static volatile hv_addr_t hypercall_count;

void hv_main(void)
{
	hypercall_count = 0;
	hv_console_write("h2: hypervisor online\n");
}

struct hv_trap_result hv_handle_trap(enum hv_trap_reason reason,
				     struct hv_trap_frame *frame)
{
	struct hv_trap_result result = {
		.handled = 0,
		.return_value = 0,
	};

	if (reason != HV_TRAP_HYPERCALL)
		return result;

	hypercall_count++;
	result.handled = 1;

	switch (frame->regs[HV_TRAP_ARG(0)]) {
	case HV_HCALL_PUTC:
		hv_console_putchar((char)frame->regs[HV_TRAP_ARG(1)]);
		result.return_value = 0;
		break;
	default:
		result.return_value = hypercall_count;
		break;
	}

	frame->regs[HV_TRAP_ARG(0)] = result.return_value;

	return result;
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
