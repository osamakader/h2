#ifndef HV_H
#define HV_H

#include <hv/config.h>
#include <hv/types.h>

/*
 * h2's own calls. The guest reaches them through the arch conduit:
 * SMCCC vendor-specific hypervisor calls on arm64, an SBI extension on
 * riscv64; the number is the call's function number there.
 */
enum hv_hypercall {
	HV_HCALL_PUTC = 1,		/* (char) */
	HV_HCALL_YIELD = 2,		/* to the next vCPU on this CPU */
	HV_HCALL_VCPU_START = 3,	/* (entry, arg): a vCPU on this CPU, */
					/* arg as from PSCI / SBI HSM */
};

enum hv_error {
	HV_OK = 0,
	HV_ENOSYS = -1,			/* no such call */
	HV_ENOMEM = -2,			/* no room */
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

hv_addr_t hv_main(hv_addr_t dtb);
int hv_load_guest(hv_addr_t dtb, hv_addr_t *entry);
struct hv_hcall_ret hv_hypercall(hv_addr_t nr, hv_addr_t arg0, hv_addr_t arg1);
void hv_console_putchar(char ch);
/* the console UART's own: raw output, input, its receive interrupt */
void hv_console_out(char ch);
int hv_console_getc(void);
void hv_console_rx_init(void);
void hv_console_rx_ack(void);
void hv_console_lock(void);
void hv_console_unlock(void);
void hv_console_write(const char *text);
void hv_console_hex(hv_addr_t value);
void hv_dump_trap(const char *what, const struct hv_trap_frame *frame);

#endif
