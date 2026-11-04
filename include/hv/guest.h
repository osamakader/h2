#ifndef HV_GUEST_H
#define HV_GUEST_H

#include <hv/config.h>
#include <hv/types.h>

/*
 * A guest. Guest 0 owns the machine's devices and sees memory as it is,
 * but for the holes in its stage 2. A second guest sees only a window of
 * RAM (at the same guest physical address, elsewhere in fact) and no
 * devices: they read as zero, writes go nowhere, it has one vCPU on the
 * boot CPU, and a fault stops it alone.
 */
struct hv_guest {
	int id;				/* its VMID, too */
	hv_addr_t ram_base, ram_size;	/* a second guest's window */
	hv_addr_t pa_offset;		/* guest physical + this = physical */
	hv_addr_t entry, dtb;		/* guest physical */
	uint64_t stage2;		/* VTTBR_EL2, hgatp */
};

extern struct hv_guest hv_guests[HV_MAX_GUESTS];

/*
 * Before guest 0's stage 2 is built: splits RAM, loads the second guest
 * if one is embedded, returns guest 0's device tree (it may have moved).
 */
hv_addr_t hv_guest2_prepare(hv_addr_t dtb);
/* after guest 0's vCPU on this CPU, before it runs */
void hv_guest2_start(int cpu, hv_addr_t hwid);

/* the architecture's part */
int arch_stage2_window(struct hv_guest *guest);
void arch_stage2_load(const struct hv_guest *guest);

#endif
