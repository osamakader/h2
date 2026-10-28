#include <arch/barrier.h>
#include <arch/boot.h>
#include <arch/platform.h>
#include <hv/cpu.h>
#include <hv/hv.h>

/*
 * Secondary CPUs that the firmware keeps in a spin table (the Pi's
 * armstub, qemu raspi): CPU n waits in EL2 for a nonzero word at
 * PLATFORM_SPIN_TABLE + 8n and jumps there. h2 takes them at boot into
 * its own EL2 setup, then has them wait on the same words for the guest,
 * which starts them as its device tree says (an entry address written
 * and cleaned to the point of coherency, then an event): they enter the
 * guest there, in EL1. The device tree stays as the firmware made it.
 */

#define SPIN_WORD(n)	((volatile uint64_t *)(hv_addr_t)(PLATFORM_SPIN_TABLE + 8 * (n)))
#define SPIN_WAIT_HZ	10	/* how long to wait for them: 1/10 s */

extern char arm64_spin_entry[];

/* CPU n's slot, for arm64_spin_entry; CPU n's hardware id is n */
int arm64_spin_slot[PLATFORM_SPIN_CPUS];
static volatile int parked[PLATFORM_SPIN_CPUS];

static unsigned long counter(void)
{
	unsigned long v;

	__asm__ volatile("isb; mrs %0, CNTPCT_EL0" : "=r"(v));
	return v;
}

/* on the boot CPU, after stage 2 is set up */
void arm64_spin_take(unsigned long boot_mpidr)
{
	unsigned long freq, start;
	int n, slot, fresh, want = 0, got;

	for (n = 0; n < PLATFORM_SPIN_CPUS; n++) {
		if ((hv_addr_t)n == (boot_mpidr & (PLATFORM_SPIN_CPUS - 1)))
			continue;
		slot = hv_cpu_claim(n, &fresh);
		if (slot < 0)
			break;
		arm64_spin_slot[n] = slot;
		*SPIN_WORD(n) = (hv_addr_t)arm64_spin_entry;
		want++;
	}
	__asm__ volatile("dsb sy; sev" ::: "memory");

	/* fewer CPUs than the table has slots for (arm_nr_cores) is fine */
	__asm__ volatile("mrs %0, CNTFRQ_EL0" : "=r"(freq));
	start = counter();
	do {
		for (got = 0, n = 0; n < PLATFORM_SPIN_CPUS; n++)
			got += parked[n];
	} while (got < want && counter() - start < freq / SPIN_WAIT_HZ);

	if (got < want) {
		hv_console_write("h2: spin table: CPUs that didn't come: ");
		hv_console_hex((hv_addr_t)(want - got));
		hv_console_putchar('\n');
	}
}

/* CPU n, from arm64_spin_entry on its slot's stack */
void arm64_spin_park(unsigned long n)
{
	volatile uint64_t *word = SPIN_WORD(n);
	hv_addr_t entry;

	arm64_el2_init();
	*word = 0;
	hv_mb();
	parked[n] = 1;

	while (!(entry = *word))
		__asm__ volatile("wfe");
	arm64_enter_guest(entry, 0, 0);
}
