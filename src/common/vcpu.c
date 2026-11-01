#include <hv/cpu.h>
#include <hv/vcpu.h>

static struct hv_vcpu vcpus[HV_MAX_VCPUS];
static struct hv_lock vcpus_lock;

/* the vCPU each physical CPU runs */
static struct hv_vcpu *current[HV_MAX_CPUS];

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
