#ifndef ARCH_ARM64_VGIC_H
#define ARCH_ARM64_VGIC_H

#include <hv/hv.h>

#define ARM64_VGIC_LRS		16	/* list registers: at most 16 */
#define ARM64_VGIC_QUEUE	32

/*
 * A vCPU's virtual GIC CPU interface: its list registers (GICv3's
 * ICH_LR<n>_EL2 layout, for GICv2 too), its control and active
 * priorities, and the interrupts waiting for a free list register.
 */
struct arm64_vgic {
	uint64_t lr[ARM64_VGIC_LRS];
	uint64_t vmcr;
	uint32_t ap0r[4];
	uint32_t ap1r[4];		/* GICv2: GICH_APR in ap1r[0] */
	uint64_t queue[ARM64_VGIC_QUEUE];
	int queued;
};

void arm64_vgic_setup(void);
int arm64_vgic_enabled(void);
void arm64_vgic_cpu_init(void);
void arm64_vgic_save(struct arm64_vgic *vgic);
void arm64_vgic_load(struct arm64_vgic *vgic);
int arm64_vgic_sysreg(struct hv_trap_frame *frame);
void arm64_handle_irq(struct hv_trap_frame *frame);

#endif
