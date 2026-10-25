#include <arch/boot.h>
#include <arch/sbi.h>
#include <arch/virq.h>
#include <arch/trap.h>
#include <hv/hv.h>

/*
 * SBI for the guest: h2's own extension, and everything else forwarded
 * to the firmware (OpenSBI) with the guest's a0..a7. HSM hart_start
 * starts the hart in h2, which then enters the guest.
 */

#define A(n)	HV_TRAP_ARG(n)

#define SBI_EXT_LEGACY_SET_TIMER 0x00UL
#define SBI_EXT_LEGACY_LAST	0x0fUL		/* legacy: result in a0 only */
#define SBI_EXT_TIME		0x54494d45UL	/* "TIME" */
#define SBI_TIME_SET_TIMER	0
#define SBI_EXT_HSM		0x48534dUL	/* "HSM" */
#define SBI_EXT_SUSP		0x53555350UL	/* "SUSP" */
#define SBI_HSM_HART_START	0
#define SBI_HSM_HART_SUSPEND	3

#define SBI_ERR_INVALID_PARAM	(-3L)

extern char riscv64_secondary_entry[];

/* where each hart enters the guest, from the guest's hart_start */
static struct {
	hv_addr_t entry;
	hv_addr_t opaque;
} guest_boot[HV_MAX_CPUS];

struct sbiret {
	hv_addr_t error;
	hv_addr_t value;
};

static struct sbiret firmware_ecall(const hv_addr_t *a)
{
	register hv_addr_t a0 __asm__("a0") = a[0];
	register hv_addr_t a1 __asm__("a1") = a[1];
	register hv_addr_t a2 __asm__("a2") = a[2];
	register hv_addr_t a3 __asm__("a3") = a[3];
	register hv_addr_t a4 __asm__("a4") = a[4];
	register hv_addr_t a5 __asm__("a5") = a[5];
	register hv_addr_t a6 __asm__("a6") = a[6];
	register hv_addr_t a7 __asm__("a7") = a[7];

	__asm__ volatile("ecall"
			 : "+r"(a0), "+r"(a1)
			 : "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6), "r"(a7)
			 : "memory");
	return (struct sbiret){ a0, a1 };
}

static struct sbiret hart_start(const hv_addr_t *a)
{
	hv_addr_t fw[8] = { 0 };
	hv_addr_t hartid = a[0];

	if (hartid >= HV_MAX_CPUS)
		return (struct sbiret){ (hv_addr_t)SBI_ERR_INVALID_PARAM, 0 };

	guest_boot[hartid].entry = a[1];
	guest_boot[hartid].opaque = a[2];

	fw[0] = hartid;
	fw[1] = (hv_addr_t)riscv64_secondary_entry;
	fw[6] = SBI_HSM_HART_START;
	fw[7] = SBI_EXT_HSM;
	return firmware_ecall(fw);
}

/* h2's own timer requests, for this hart */
void riscv64_sbi_set_timer(hv_addr_t stime)
{
	hv_addr_t fw[8] = { 0 };

	fw[0] = stime;
	fw[6] = SBI_TIME_SET_TIMER;
	fw[7] = SBI_EXT_TIME;
	firmware_ecall(fw);
}

void riscv64_secondary_boot(unsigned long hartid)
{
	riscv64_enter_guest(guest_boot[hartid].entry, 0,
			    (unsigned long)(hv_cpu_stacks[hartid] + HV_STACK_SIZE),
			    hartid, guest_boot[hartid].opaque);
}

/* calls whose entry point h2 would have to wrap: not yet */
static int sbi_unsupported(hv_addr_t ext, hv_addr_t fid)
{
	if (ext == SBI_EXT_HSM)
		return fid == SBI_HSM_HART_SUSPEND;
	return ext == SBI_EXT_SUSP;
}

void riscv64_sbi_call(struct hv_trap_frame *frame)
{
	hv_addr_t *a = &frame->regs[A(0)];
	hv_addr_t ext = a[7], fid = a[6];
	struct hv_hcall_ret ret;
	struct sbiret fw;

	if (ext == HV_SBI_EXT) {
		ret = hv_hypercall(fid, a[0]);
		a[0] = ret.error ? (hv_addr_t)SBI_ERR_NOT_SUPPORTED : 0;
		a[1] = ret.value;
		return;
	}

	if (sbi_unsupported(ext, fid)) {
		a[0] = (hv_addr_t)SBI_ERR_NOT_SUPPORTED;
		return;
	}

	if ((ext == SBI_EXT_TIME && fid == SBI_TIME_SET_TIMER) ||
	    ext == SBI_EXT_LEGACY_SET_TIMER)
		riscv64_guest_set_timer();

	if (ext == SBI_EXT_HSM && fid == SBI_HSM_HART_START)
		fw = hart_start(a);
	else
		fw = firmware_ecall(a);
	a[0] = fw.error;
	if (ext > SBI_EXT_LEGACY_LAST)
		a[1] = fw.value;
}
