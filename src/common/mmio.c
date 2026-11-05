#include <hv/hv.h>
#include <hv/mmio.h>
#include <hv/stage2.h>
#include <hv/vcpu.h>

#define MMIO_MAX	8
#define PAGE_SIZE	0x1000UL
#define PAGE_DOWN(a)	((a) & ~(PAGE_SIZE - 1))
#define PAGE_UP(a)	PAGE_DOWN((a) + PAGE_SIZE - 1)

static const struct hv_mmio_dev *devs[MMIO_MAX];
static int ndevs;

int hv_mmio_register(const struct hv_mmio_dev *dev)
{
	if (ndevs == MMIO_MAX) {
		hv_console_write("h2: mmio: too many devices\n");
		return -1;
	}
	devs[ndevs++] = dev;
	hv_stage2_hole(PAGE_DOWN(dev->base), PAGE_UP(dev->base + dev->size));
	if (dev->phys_irq)
		arch_irq_own(dev->phys_irq);
	return 0;
}

/* the real thing, for what shares an emulated device's pages */
static uint64_t phys_read(hv_addr_t pa, int size)
{
	switch (size) {
	case 1:
		return *(volatile uint8_t *)pa;
	case 2:
		return *(volatile uint16_t *)pa;
	case 4:
		return *(volatile uint32_t *)pa;
	default:
		return *(volatile uint64_t *)pa;
	}
}

static void phys_write(hv_addr_t pa, int size, uint64_t value)
{
	switch (size) {
	case 1:
		*(volatile uint8_t *)pa = (uint8_t)value;
		break;
	case 2:
		*(volatile uint16_t *)pa = (uint16_t)value;
		break;
	case 4:
		*(volatile uint32_t *)pa = (uint32_t)value;
		break;
	default:
		*(volatile uint64_t *)pa = value;
		break;
	}
}

int hv_mmio_emulate(hv_addr_t gpa, int size, int write, uint64_t *value)
{
	const struct hv_mmio_dev *dev;
	int i;

	int other = hv_vcpu_current()->guest->id != 0;

	for (i = 0; i < ndevs; i++) {
		dev = devs[i];
		/* most are guest 0's */
		if (other && !dev->all_guests)
			continue;
		if (gpa < PAGE_DOWN(dev->base) ||
		    gpa >= PAGE_UP(dev->base + dev->size))
			continue;

		if (gpa < dev->base || gpa + size > dev->base + dev->size) {
			if (write)
				phys_write(gpa, size, *value);
			else
				*value = phys_read(gpa, size);
		} else if (write) {
			dev->write(dev->ctx, gpa - dev->base, size, *value);
		} else {
			*value = dev->read(dev->ctx, gpa - dev->base, size);
		}
		return 1;
	}
	/* another guest finds nothing there */
	if (other) {
		if (!write)
			*value = 0;
		return 1;
	}
	return 0;
}

int hv_mmio_phys_irq(unsigned int irq)
{
	int i;

	for (i = 0; i < ndevs; i++) {
		if (devs[i]->phys_irq && devs[i]->phys_irq == irq) {
			devs[i]->irq(devs[i]->ctx);
			return 1;
		}
	}
	return 0;
}
