#ifndef HV_VIRTIO_H
#define HV_VIRTIO_H

#include <hv/types.h>

/* virtio-mmio, version 2 (virtio 1.x) */
#define VIRTIO_MMIO_MAGIC		0x000
#define VIRTIO_MMIO_VERSION		0x004
#define VIRTIO_MMIO_DEVICE_ID		0x008
#define VIRTIO_MMIO_VENDOR_ID		0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES	0x010
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL	0x014
#define VIRTIO_MMIO_DRIVER_FEATURES	0x020
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL	0x024
#define VIRTIO_MMIO_QUEUE_SEL		0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX	0x034
#define VIRTIO_MMIO_QUEUE_NUM		0x038
#define VIRTIO_MMIO_QUEUE_READY		0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY	0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS	0x060
#define VIRTIO_MMIO_INTERRUPT_ACK	0x064
#define VIRTIO_MMIO_STATUS		0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW	0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH	0x084
#define VIRTIO_MMIO_QUEUE_DRIVER_LOW	0x090
#define VIRTIO_MMIO_QUEUE_DRIVER_HIGH	0x094
#define VIRTIO_MMIO_QUEUE_DEVICE_LOW	0x0a0
#define VIRTIO_MMIO_QUEUE_DEVICE_HIGH	0x0a4
#define VIRTIO_MMIO_CONFIG_GENERATION	0x0fc
#define VIRTIO_MMIO_CONFIG		0x100
#define VIRTIO_MMIO_SIZE		0x200

#define VIRTIO_MAGIC			0x74726976	/* "virt" */
#define VIRTIO_DEV_BLOCK		2

#define VIRTIO_STATUS_ACKNOWLEDGE	1
#define VIRTIO_STATUS_DRIVER		2
#define VIRTIO_STATUS_DRIVER_OK		4
#define VIRTIO_STATUS_FEATURES_OK	8
#define VIRTIO_STATUS_FAILED		0x80

#define VIRTIO_F_VERSION_1		32
#define VIRTIO_INT_USED			1

/* split virtqueues */
struct virtq_desc {
	uint64_t addr;
	uint32_t len;
	uint16_t flags;
	uint16_t next;
};

#define VIRTQ_DESC_F_NEXT		1
#define VIRTQ_DESC_F_WRITE		2
#define VIRTQ_DESC_F_INDIRECT		4
#define VIRTQ_AVAIL_F_NO_INTERRUPT	1

struct virtq_used_elem {
	uint32_t id;
	uint32_t len;
};

/* virtio-blk requests: a header, data, a status byte */
struct virtio_blk_req {
	uint32_t type;
	uint32_t reserved;
	uint64_t sector;
};

#define VIRTIO_BLK_T_IN			0
#define VIRTIO_BLK_T_OUT		1
#define VIRTIO_BLK_T_FLUSH		4
#define VIRTIO_BLK_S_OK			0
#define VIRTIO_BLK_S_IOERR		1
#define VIRTIO_BLK_S_UNSUPP		2
#define VIRTIO_BLK_SECTOR		512

/*
 * The guest's disk: an emulated virtio-blk where the real one is (base,
 * irq), which h2 drives. Before stage 2 is built; 0, or -1 if there is no
 * virtio-blk there.
 */
int hv_vblk_init(hv_addr_t base, unsigned int irq);

#endif
