#ifndef HV_H
#define HV_H

#include <hv/types.h>

/*
 * h2's own calls. The guest reaches them through the arch conduit:
 * SMCCC vendor-specific hypervisor calls on arm64, an SBI extension on
 * riscv64; the number is the call's function number there.
 */
enum hv_hypercall {
	HV_HCALL_PUTC = 1,
};

enum hv_error {
	HV_OK = 0,
	HV_ENOSYS = -1,
};

struct hv_hcall_ret {
	enum hv_error error;
	hv_addr_t value;
};

struct hv_trap_frame {
	hv_addr_t regs[31];
	hv_addr_t pc;
	hv_addr_t status;
	hv_addr_t cause;
	hv_addr_t value;
};

void hv_main(void);
struct hv_hcall_ret hv_hypercall(hv_addr_t nr, hv_addr_t arg0);
void hv_console_putchar(char ch);
void hv_console_write(const char *text);
void hv_console_hex(hv_addr_t value);
void hv_dump_trap(const char *what, const struct hv_trap_frame *frame);

#endif
