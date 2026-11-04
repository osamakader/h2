#include <hv/cpu.h>
#include <hv/vcpu.h>

static struct hv_vcpu vcpus[HV_MAX_VCPUS];
static struct hv_lock vcpus_lock;

/* the vCPU each physical CPU runs, and whether it asked to give way */
static struct hv_vcpu *current[HV_MAX_CPUS];
static int yielding[HV_MAX_CPUS];

static void zero(void *p, hv_addr_t size)
{
	hv_addr_t *w = p;
	hv_addr_t i;

	for (i = 0; i < size / sizeof(*w); i++)
		w[i] = 0;
}

/*
 * A vCPU for physical CPU cpu, which enters entry with the MMU off, its
 * stack pointer at stack and arguments arg0, arg1 (x0 / a0, a1), in
 * cpu's ring after the one it runs. Only cpu itself changes its ring.
 * vCPUs take turns: when one yields, and on each tick.
 * 0 if there is no room.
 */
struct hv_vcpu *hv_vcpu_new(int cpu, hv_addr_t entry, hv_addr_t stack,
			    hv_addr_t arg0, hv_addr_t arg1)
{
	struct hv_vcpu *vcpu = 0;
	int i;

	hv_lock(&vcpus_lock);
	for (i = 0; i < HV_MAX_VCPUS && !vcpu; i++)
		if (!vcpus[i].used)
			vcpu = &vcpus[i];
	if (vcpu)
		vcpu->used = 1;
	hv_unlock(&vcpus_lock);
	if (!vcpu)
		return 0;

	zero(&vcpu->frame, sizeof(vcpu->frame));
	zero(&vcpu->arch, sizeof(vcpu->arch));
	vcpu->cpu = cpu;
	arch_vcpu_init(vcpu, entry, stack, arg0, arg1);

	if (current[cpu]) {
		/* a CPU with one vCPU has no tick: from now on it needs one */
		if (current[cpu]->next == current[cpu])
			arch_tick_start();
		vcpu->next = current[cpu]->next;
		current[cpu]->next = vcpu;
	} else {
		vcpu->next = vcpu;
	}
	return vcpu;
}

/* the first vCPU on its CPU, from h2's own code: run it, for good */
void hv_vcpu_run(struct hv_vcpu *vcpu)
{
	current[vcpu->cpu] = vcpu;
	arch_vcpu_load(vcpu);
	arch_vcpu_enter(&vcpu->frame, HV_CPU_STACK_TOP(vcpu->cpu));
}

/* the vCPU this CPU runs: from a trap, the one that trapped */
struct hv_vcpu *hv_vcpu_current(void)
{
	return current[hv_cpu_index()];
}

/* the vCPU pool: ids are indices into it */
struct hv_vcpu *hv_vcpu_first(void)
{
	return vcpus;
}

/* the running vCPU gives way, when its trap is done */
void hv_vcpu_yield(void)
{
	yielding[hv_cpu_index()] = 1;
}

static void copy_frame(struct hv_trap_frame *to,
		       const struct hv_trap_frame *from)
{
	hv_addr_t *t = (hv_addr_t *)to;
	const hv_addr_t *f = (const hv_addr_t *)from;
	hv_addr_t i;

	for (i = 0; i < sizeof(*to) / sizeof(*t); i++)
		t[i] = f[i];
}

/*
 * The end of a trap from the guest, frame holding the trapping vCPU's
 * registers (and the trap's results): if it gives way, save it and
 * switch frame and CPU to the next vCPU in this CPU's ring. Two vCPUs
 * of a guest may use the same ASIDs for different translations: the
 * TLB doesn't keep the last one's.
 */
void hv_vcpu_trap_done(struct hv_trap_frame *frame)
{
	int cpu = hv_cpu_index();
	struct hv_vcpu *from = current[cpu], *to;

	if (!yielding[cpu])
		return;
	yielding[cpu] = 0;
	to = from->next;
	if (to == from)
		return;

	copy_frame(&from->frame, frame);
	arch_vcpu_save(from);
	arch_vcpu_flush_tlb();
	current[cpu] = to;
	arch_vcpu_load(to);
	copy_frame(frame, &to->frame);
}
