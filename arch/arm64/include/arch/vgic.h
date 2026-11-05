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
int arm64_vgic_gic3(void);
void arm64_vgic_cpu_init(void);
void arm64_vgic_save(struct arm64_vgic *vgic);
void arm64_vgic_load(struct arm64_vgic *vgic);
int arm64_vgic_sysreg(struct hv_trap_frame *frame);
void arm64_handle_irq(struct hv_trap_frame *frame);
void arm64_vgic_sgi_v2(uint32_t sgir);
void arm64_vgic_changed(int guest);

/* each guest's distributor (vgicd.c) */
void arm64_vgicd_setup(int gic3);
void arm64_vgicd_sgi(int guest, int target, unsigned int intid, unsigned int src);
int arm64_vgicd_sgi_take(int guest, int cpu, unsigned int *intid, unsigned int *src);
int arm64_vgicd_sgi_waiting(int guest, int cpu);
int arm64_vgicd_enabled(int guest, int cpu, unsigned int intid);
unsigned int arm64_vgicd_prio(int guest, int cpu, unsigned int intid);
int arm64_vgicd_route(int guest, unsigned int intid);
uint32_t arm64_vgic_ppi_wanted(int guest, int cpu);

#endif
