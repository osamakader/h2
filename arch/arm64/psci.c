#include <arch/boot.h>
#include <arch/psci.h>
#include <arch/trap.h>
#include <hv/cpu.h>
#include <hv/hv.h>

/*
 * PSCI for the guest. h2 forwards calls to the firmware's PSCI, through
 * SMC: on qemu virt with virtualization=on, the conduit for EL2.
 * CPU_ON starts the CPU in h2 at EL2, which then enters the guest.
 */

#define PSCI_CPU_SUSPEND	0x01
#define PSCI_CPU_ON		0x03
#define PSCI_FEATURES		0x0a
#define PSCI_SYSTEM_SUSPEND	0x0e
#define PSCI_LAST		0x1f	/* PSCI's part of the standard range */

#define PSCI_RET_NOT_SUPPORTED	(-1L)
#define PSCI_RET_DENIED		(-3L)
#define PSCI_RET_ALREADY_ON	(-4L)
#define PSCI_RET_ON_PENDING	(-5L)

extern char arm64_secondary_entry[];

static hv_addr_t firmware_smc(hv_addr_t fid, hv_addr_t a1, hv_addr_t a2,
			      hv_addr_t a3)
{
	register hv_addr_t x0 __asm__("x0") = fid;
	register hv_addr_t x1 __asm__("x1") = a1;
	register hv_addr_t x2 __asm__("x2") = a2;
	register hv_addr_t x3 __asm__("x3") = a3;

	__asm__ volatile("smc #0"
			 : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
			 :
			 : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
			   "x12", "x13", "x14", "x15", "x16", "x17", "memory");
	return x0;
}

static hv_addr_t cpu_on(hv_addr_t fid, hv_addr_t mpidr, hv_addr_t entry,
			hv_addr_t context)
{
	int fresh, slot = hv_cpu_claim(mpidr & MPIDR_AFFINITY, &fresh);
	long ret;

	/* no slot left in h2 */
	if (slot < 0)
		return (hv_addr_t)PSCI_RET_DENIED;

	hv_cpus[slot].guest_entry = entry;
	hv_cpus[slot].guest_arg = context;

	ret = (long)firmware_smc(fid, mpidr,
				 (hv_addr_t)arm64_secondary_entry, slot);
	if (ret && ret != PSCI_RET_ALREADY_ON && ret != PSCI_RET_ON_PENDING &&
	    fresh)
		hv_cpu_release(slot);
	return (hv_addr_t)ret;
}

void arm64_secondary_boot(unsigned long slot)
{
	arm64_el2_init();
	arm64_enter_guest(hv_cpus[slot].guest_entry, 0,
			  hv_cpus[slot].guest_arg);
}

/* calls whose entry point h2 would have to wrap: not yet */
static int psci_unsupported(uint32_t fid)
{
	switch (SMCCC_NUMBER(fid)) {
	case PSCI_CPU_SUSPEND:
	case PSCI_SYSTEM_SUSPEND:
		return 1;
	default:
		return 0;
	}
}

int arm64_psci_call(struct hv_trap_frame *frame)
{
	uint32_t fid = (uint32_t)frame->regs[0];
	hv_addr_t *x = frame->regs;

	if (SMCCC_NUMBER(fid) > PSCI_LAST)
		return 0;

	if (psci_unsupported(fid) ||
	    (SMCCC_NUMBER(fid) == PSCI_FEATURES && psci_unsupported((uint32_t)x[1])))
		x[0] = (hv_addr_t)PSCI_RET_NOT_SUPPORTED;
	else if (SMCCC_NUMBER(fid) == PSCI_CPU_ON)
		x[0] = cpu_on(x[0], x[1], x[2], x[3]);
	else
		x[0] = firmware_smc(x[0], x[1], x[2], x[3]);

	return 1;
}
