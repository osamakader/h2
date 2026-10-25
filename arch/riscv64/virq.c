#include <arch/csr.h>
#include <arch/sbi.h>
#include <arch/virq.h>

/*
 * Interrupts for the guest. The VS-level ones are delegated to it
 * (hideleg); h2 takes the hart's supervisor interrupts and raises the
 * guest's matching ones in hvip.
 */

#define VS_IRQS		((1UL << IRQ_VS_SOFT) | (1UL << IRQ_VS_TIMER) | \
			 (1UL << IRQ_VS_EXT))

/* per hart, before entering the guest: nothing pending for it yet */
void riscv64_virq_init(void)
{
	riscv64_sbi_set_timer(~0UL);
	csr_write(hvip, 0);
	csr_write(hideleg, VS_IRQS);
	csr_set(sie, 1UL << IRQ_S_TIMER);
}

/*
 * The timer the guest set with SBI set_timer went off: disarm it and
 * pass it on. It stays pending for the guest until its next set_timer.
 */
void riscv64_timer_interrupt(void)
{
	riscv64_sbi_set_timer(~0UL);
	csr_set(hvip, 1UL << IRQ_VS_TIMER);
}

/* the guest is setting its timer: SBI clears the pending one */
void riscv64_guest_set_timer(void)
{
	csr_clear(hvip, 1UL << IRQ_VS_TIMER);
}
