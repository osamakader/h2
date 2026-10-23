#include <arch/sbi.h>
#include <arch/trap.h>
#include <hv/hv.h>

/*
 * SBI for the guest: h2's own extension, and everything else forwarded
 * to the firmware (OpenSBI) with the guest's a0..a7.
 */

#define A(n)	HV_TRAP_ARG(n)

#define SBI_EXT_LEGACY_LAST	0x0fUL		/* legacy: result in a0 only */
#define SBI_EXT_HSM		0x48534dUL	/* "HSM" */
#define SBI_EXT_SUSP		0x53555350UL	/* "SUSP" */
#define SBI_HSM_HART_START	0
#define SBI_HSM_HART_SUSPEND	3

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

/* calls whose entry point h2 would have to wrap: not yet */
static int sbi_unsupported(hv_addr_t ext, hv_addr_t fid)
{
	if (ext == SBI_EXT_HSM)
		return fid == SBI_HSM_HART_START || fid == SBI_HSM_HART_SUSPEND;
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

	fw = firmware_ecall(a);
	a[0] = fw.error;
	if (ext > SBI_EXT_LEGACY_LAST)
		a[1] = fw.value;
}
