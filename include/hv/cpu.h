#ifndef HV_CPU_H
#define HV_CPU_H

#include <hv/config.h>
#include <hv/types.h>

/*
 * h2's CPUs, in slots: 0 is the boot CPU, the others are taken as the
 * guest starts CPUs, by their hardware id (MPIDR affinity, hartid). A CPU
 * runs h2 on its slot's stack, which is how it knows its slot.
 */
struct hv_cpu {
	int used;
	hv_addr_t hwid;
	hv_addr_t guest_entry;		/* where it enters the guest */
	hv_addr_t guest_arg;		/* and with what */
};

extern struct hv_cpu hv_cpus[HV_MAX_CPUS];
extern char hv_cpu_stacks[HV_MAX_CPUS][HV_STACK_SIZE];

#define HV_CPU_STACK_TOP(i)	((hv_addr_t)(hv_cpu_stacks[i] + HV_STACK_SIZE))

int hv_cpu_index(void);
void hv_cpu_boot(hv_addr_t hwid);
int hv_cpu_claim(hv_addr_t hwid, int *fresh);
void hv_cpu_release(int slot);

/*
 * Lamport's bakery lock: plain loads and stores and barriers. arm64 runs
 * h2 with the MMU off, where memory is Device memory and exclusives (so
 * atomics) aren't architecturally guaranteed to work.
 */
struct hv_lock {
	volatile uint32_t choosing[HV_MAX_CPUS];
	volatile uint32_t number[HV_MAX_CPUS];
};

void hv_lock(struct hv_lock *lock);
void hv_unlock(struct hv_lock *lock);

#endif
