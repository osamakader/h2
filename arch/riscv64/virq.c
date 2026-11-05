#include <arch/barrier.h>
#include <arch/csr.h>
#include <arch/sbi.h>
#include <arch/virq.h>
#include <arch/vplic.h>
#include <hv/cpu.h>
#include <hv/hv.h>
#include <hv/vcpu.h>

/*
 * Interrupts for the guest. The VS-level ones are delegated to it
 * (hideleg); h2 takes the hart's supervisor interrupts and raises the
 * guest's matching ones in hvip.
 *
 * The timer: with Sstc, each vCPU has its own comparator (vstimecmp,
 * which raises its VSTIP by itself), an SBI set_timer only sets it, and
 * a guest that knows Sstc sets it without h2, and the hart's timer is
 * h2's tick alone. Without Sstc, one timer in the firmware does for both,
 * set to whichever comes first of the tick and the running vCPU's
 * set_timer; when that one is due, h2 passes it on.
 */

#define VS_IRQS		((1UL << IRQ_VS_SOFT) | (1UL << IRQ_VS_TIMER) | \
			 (1UL << IRQ_VS_EXT))

/* the timer's frequency on qemu virt, the only riscv64 machine so far */
#define TIMEBASE_HZ	10000000UL
#define TICK		(TIMEBASE_HZ / HV_TICK_HZ)

static int sstc, said;
static hv_addr_t tick_at[HV_MAX_CPUS];		/* 0: no tick */

/* the hart's timer, for what comes first */
void riscv64_timer_program(void)
{
	struct hv_vcpu *vcpu = hv_vcpu_current();
	hv_addr_t when = tick_at[hv_cpu_index()];

	if (!when)
		when = ~0UL;
	if (sstc) {
		csr_write(CSR_STIMECMP, when);
		return;
	}
	if (vcpu && vcpu->arch.stime < when)
		when = vcpu->arch.stime;
	riscv64_sbi_set_timer(when);
}

void arch_tick_start(void)
{
	tick_at[hv_cpu_index()] = csr_read(time) + TICK;
	riscv64_timer_program();
}

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
	if (sstc)
		csr_write(CSR_STIMECMP, ~0UL);
	csr_write(hvip, 0);
	csr_write(hideleg, VS_IRQS);
	csr_set(sie, (1UL << IRQ_S_TIMER) | (1UL << IRQ_S_SOFT) | (1UL << IRQ_S_EXT));
}

/*
 * The hart's timer went off. The running vCPU's set_timer, if that is
 * due, is passed on, pending for it until its next set_timer; on a tick
 * it gives way.
 */
void riscv64_timer_interrupt(void)
{
	struct hv_vcpu *vcpu = hv_vcpu_current();
	hv_addr_t now = csr_read(time);
	int me = hv_cpu_index();

	if (!sstc && vcpu->arch.stime <= now) {
		vcpu->arch.stime = ~0UL;
		csr_set(hvip, 1UL << IRQ_VS_TIMER);
	}
	if (tick_at[me] && tick_at[me] <= now) {
		tick_at[me] = now + TICK;
		hv_vcpu_yield();
	}
	riscv64_timer_program();
}

/*
 * The running vCPU sets its timer, which clears the pending one: with
 * Sstc, its comparator; without, the hart's timer follows.
 */
void riscv64_guest_set_timer(hv_addr_t stime)
{
	if (sstc) {
		csr_write(CSR_VSTIMECMP, stime);
		return;
	}
	hv_vcpu_current()->arch.stime = stime;
	csr_clear(hvip, 1UL << IRQ_VS_TIMER);
	riscv64_timer_program();
}

/*
 * IPIs: the guest's (SBI send_ipi, which h2 forwards and marks), and h2's
 * own, for a hart to look at its external interrupt again.
 */
static volatile int guest_ipi[HV_MAX_CPUS], kicked[HV_MAX_CPUS];

void riscv64_guest_ipi(int slot)
{
	guest_ipi[slot] = 1;
	hv_mb();
}

void riscv64_kick(int slot)
{
	kicked[slot] = 1;
	hv_mb();
	riscv64_sbi_send_ipi(hv_cpus[slot].hwid);
}

/*
 * An IPI for this hart. The guest's is passed on (it clears it by
 * clearing its sip.SSIP, which is hvip's); one from neither h2 nor the
 * guest (the firmware's?) is taken for the guest's too. A flag is only
 * cleared once seen set: one set meanwhile comes with its own IPI.
 */
void riscv64_soft_interrupt(void)
{
	int me = hv_cpu_index(), guest = 0, kick = 0;

	csr_clear(sip, 1UL << IRQ_S_SOFT);
	hv_mb();
	if (guest_ipi[me]) {
		guest_ipi[me] = 0;
		guest = 1;
	}
	if (kicked[me]) {
		kicked[me] = 0;
		kick = 1;
	}
	if (guest || !kick) {
		/* guest 0's: to the vCPU it has here */
		struct hv_vcpu *owner = hv_vcpu_owner();

		if (owner == hv_vcpu_current()) {
			csr_set(hvip, 1UL << IRQ_VS_SOFT);
		} else if (owner) {
			owner->arch.hvip |= 1UL << IRQ_VS_SOFT;
			hv_vcpu_yield();
		}
	}
	if (kick)
		riscv64_external_sync();
}
