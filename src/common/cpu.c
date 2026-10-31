#include <arch/barrier.h>
#include <hv/cpu.h>

struct hv_cpu hv_cpus[HV_MAX_CPUS];
char hv_cpu_stacks[HV_MAX_CPUS][HV_STACK_SIZE] __attribute__((aligned(16)));

static struct hv_lock slots_lock;

/* -1 on a CPU that isn't on a slot's stack (a fault before it got one) */
int hv_cpu_index(void)
{
	char here;
	hv_addr_t sp = (hv_addr_t)&here, base = (hv_addr_t)hv_cpu_stacks;

	if (sp < base || sp >= base + sizeof(hv_cpu_stacks))
		return -1;
	return (int)((sp - base) / HV_STACK_SIZE);
}

void hv_cpu_boot(hv_addr_t hwid)
{
	hv_cpus[0].used = 1;
	hv_cpus[0].hwid = hwid;
}

/*
 * The slot for the CPU hwid: the one it had, if it was started before,
 * else a free one (*fresh = 1). -1 if there is none.
 */
int hv_cpu_claim(hv_addr_t hwid, int *fresh)
{
	int i, slot = -1;

	*fresh = 0;
	hv_lock(&slots_lock);
	for (i = 0; i < HV_MAX_CPUS; i++)
		if (hv_cpus[i].used && hv_cpus[i].hwid == hwid)
			slot = i;
	for (i = 0; slot < 0 && i < HV_MAX_CPUS; i++) {
		if (!hv_cpus[i].used) {
			hv_cpus[i].used = 1;
			hv_cpus[i].hwid = hwid;
			slot = i;
			*fresh = 1;
		}
	}
	hv_unlock(&slots_lock);
	return slot;
}

/* a fresh slot whose CPU didn't start after all */
void hv_cpu_release(int slot)
{
	hv_lock(&slots_lock);
	hv_cpus[slot].used = 0;
	hv_unlock(&slots_lock);
}

/* not for a CPU without a slot: that one goes ahead, to report a fault */
void hv_lock(struct hv_lock *lock)
{
	int me = hv_cpu_index(), j;
	uint32_t max = 0;

	if (me < 0)
		return;

	lock->choosing[me] = 1;
	hv_mb();
	for (j = 0; j < HV_MAX_CPUS; j++)
		if (lock->number[j] > max)
			max = lock->number[j];
	lock->number[me] = max + 1;
	hv_mb();
	lock->choosing[me] = 0;
	hv_mb();

	for (j = 0; j < HV_MAX_CPUS; j++) {
		while (lock->choosing[j])
			;
		while (lock->number[j] &&
		       (lock->number[j] < lock->number[me] ||
			(lock->number[j] == lock->number[me] && j < me)))
			;
	}
	hv_mb();
}

void hv_unlock(struct hv_lock *lock)
{
	int me = hv_cpu_index();

	hv_mb();
	if (me >= 0)
		lock->number[me] = 0;
}
