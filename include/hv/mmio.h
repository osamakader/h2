#ifndef HV_MMIO_H
#define HV_MMIO_H

#include <hv/types.h>

/*
 * Devices h2 emulates for the guest: a range of guest physical
 * addresses left out of stage 2, so the guest's accesses trap and the
 * architecture's code hands them here, decoded. The rest of the pages
 * the range is in stays the real device's (as if mapped). A device may
 * also own a physical interrupt (its real counterpart's), which h2
 * keeps from the guest and hands to it.
 */
struct hv_mmio_dev {
	hv_addr_t base;
	hv_addr_t size;
	void *ctx;
	/* size 1, 2, 4 or 8, offset from base; reads zero-extended */
	uint64_t (*read)(void *ctx, hv_addr_t offset, int size);
	void (*write)(void *ctx, hv_addr_t offset, int size, uint64_t value);
	unsigned int phys_irq;			/* 0: none */
	void (*irq)(void *ctx);
	int all_guests;				/* else guest 0's alone */
};

/* before stage 2 is built: 0, or -1 if there is no room */
int hv_mmio_register(const struct hv_mmio_dev *dev);
/* a trapped access: 1 if it was to an emulated device, now done */
int hv_mmio_emulate(hv_addr_t gpa, int size, int write, uint64_t *value);
/* a physical interrupt: 1 if a device owns it, now handled */
int hv_mmio_phys_irq(unsigned int irq);

/* the architecture's: a device owns a real interrupt from now on */
void arch_irq_own(unsigned int irq);

/* a device's interrupt line, as the guest's interrupt controller sees it */
void hv_virq_line(unsigned int irq, int level);

#endif
