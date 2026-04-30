#ifndef HV_H
#define HV_H

#include <hv/types.h>

enum hv_trap_reason {
	HV_TRAP_UNKNOWN = 0,
	HV_TRAP_HYPERCALL,
};

enum hv_hypercall {
	HV_HCALL_PUTC = 1,
};

struct hv_trap_frame {
	hv_addr_t regs[31];
	hv_addr_t pc;
	hv_addr_t status;
	hv_addr_t cause;
	hv_addr_t value;
};

struct hv_trap_result {
	int handled;
	hv_addr_t return_value;
};

void hv_main(void);
struct hv_trap_result hv_handle_trap(enum hv_trap_reason reason,
				     struct hv_trap_frame *frame);
void hv_console_putchar(char ch);
void hv_console_write(const char *text);

#endif
