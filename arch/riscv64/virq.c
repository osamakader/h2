#include <arch/csr.h>
#include <arch/sbi.h>
#include <arch/virq.h>
#include <arch/vplic.h>
#include <hv/hv.h>
#include <hv/vcpu.h>

/*
 * Interrupts for the guest. The VS-level ones are delegated to it
 * (hideleg); h2 takes the hart's supervisor interrupts and raises the
 * guest's matching ones in hvip.
 *
 * The timer: with Sstc, each vCPU has its own comparator (vstimecmp,
 * which raises its VSTIP by itself), an SBI set_timer only sets it, and
 * a guest that knows Sstc sets it without h2. Without Sstc, h2 forwards
 * set_timer to the firmware and passes the hart's timer interrupt on.
 */

#define VS_IRQS		((1UL << IRQ_VS_SOFT) | (1UL << IRQ_VS_TIMER) | \
			 (1UL << IRQ_VS_EXT))

static int sstc, said;

/* the guest's own timer comparator: STCE sticks if the firmware allows it */
int riscv64_sstc(void)
{
	return sstc;
}

/* per hart, before entering the guest: nothing pending for it yet */
void riscv64_virq_init(void)
{
	csr_set(CSR_HENVCFG, HENVCFG_STCE);
	sstc = (csr_read(CSR_HENVCFG) & HENVCFG_STCE) != 0;
	if (sstc && !said) {
		hv_console_write("h2: guest timer: Sstc (vstimecmp)\n");
		said = 1;
	}
	csr_write(CSR_HTIMEDELTA, 0);
	riscv64_sbi_set_timer(~0UL);
	csr_write(hvip, 0);
	csr_write(hideleg, VS_IRQS);
	csr_set(sie, (1UL << IRQ_S_TIMER) | (1UL << IRQ_S_SOFT));
	riscv64_external_sync();
}

/*
 * The timer the running vCPU set with SBI set_timer went off: disarm it
 * and pass it on. It stays pending for the vCPU until its next set_timer.
 */
void riscv64_timer_interrupt(void)
{
	riscv64_sbi_set_timer(~0UL);
	hv_vcpu_current()->arch.stime = ~0UL;
	csr_set(hvip, 1UL << IRQ_VS_TIMER);
}

/*
 * The running vCPU sets its timer, which clears the pending one: with
 * Sstc, its comparator; without, h2 forwards the call to the firmware.
 */
void riscv64_guest_set_timer(hv_addr_t stime)
{
	if (sstc) {
		csr_write(CSR_VSTIMECMP, stime);
		return;
	}
	hv_vcpu_current()->arch.stime = stime;
	csr_clear(hvip, 1UL << IRQ_VS_TIMER);
}

/*
 * An IPI for this hart (SBI send_ipi, from the guest on some hart): pass
 * it on. The guest clears it by clearing its sip.SSIP, which is hvip's.
 */
void riscv64_soft_interrupt(void)
{
	csr_clear(sip, 1UL << IRQ_S_SOFT);
	csr_set(hvip, 1UL << IRQ_VS_SOFT);
}
