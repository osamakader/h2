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

	switch (frame->regs[0]) {
	case HV_HCALL_PUTC:
		hv_console_putchar((char)frame->regs[1]);
		result.return_value = 0;
		break;
	default:
		result.return_value = hypercall_count;
		break;
	}

	frame->regs[0] = result.return_value;

	return result;
}

void hv_console_write(const char *text)
{
	while (*text)
		hv_console_putchar(*text++);
}
