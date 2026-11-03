#include <arch/csr.h>
#include <arch/trap.h>
#include <arch/virq.h>
#include <hv/hv.h>
#include <hv/mmio.h>

/*
 * External interrupts for the guest. The hart's supervisor external
 * interrupt (sip.SEIP) comes from its PLIC context and stays up until the
 * guest claims the interrupt, so h2 needs to see the claims: the PLIC's
 * per-context registers (threshold, claim/complete) are an emulated
 * device: h2 does the guest's accesses on the real PLIC (its S contexts;
 * the M ones read as zero). After each claim or completion, the guest's
 * external interrupt (hvip.VSEIP) follows the hart's.
 */

#define QEMU_VIRT_PLIC		0x0c000000UL
#define PLIC_CONTEXT_BASE	(QEMU_VIRT_PLIC + 0x200000)
#define PLIC_CONTEXT_END	(QEMU_VIRT_PLIC + 0x400000)
#define PLIC_CONTEXT_SIZE	0x1000UL
#define PLIC_CLAIM		4	/* claim on read, complete on write */

/* qemu virt: hart h has context 2h (M-mode) and 2h + 1 (S-mode) */
#define PLIC_CONTEXT_S(ctx)	((ctx) & 1)

/* hvip.VSEIP = sip.SEIP; while it is up, h2 needn't hear about it again */
void riscv64_external_sync(void)
{
	if (csr_read(sip) & (1UL << IRQ_S_EXT)) {
		csr_set(hvip, 1UL << IRQ_VS_EXT);
		csr_clear(sie, 1UL << IRQ_S_EXT);
	} else {
		csr_clear(hvip, 1UL << IRQ_VS_EXT);
		csr_set(sie, 1UL << IRQ_S_EXT);
	}
}

/* a context page, as the guest sees it: its S contexts are the real ones */
static uint64_t context_read(void *ctx, hv_addr_t offset, int size)
{
	volatile uint32_t *reg = (volatile uint32_t *)(PLIC_CONTEXT_BASE + offset);
	uint32_t value;

	(void)ctx;
	if (size != 4 || !PLIC_CONTEXT_S(offset / PLIC_CONTEXT_SIZE))
		return 0;
	value = *reg;
	if ((offset & (PLIC_CONTEXT_SIZE - 1)) == PLIC_CLAIM)
		riscv64_external_sync();
	return value;
}

static void context_write(void *ctx, hv_addr_t offset, int size, uint64_t value)
{
	volatile uint32_t *reg = (volatile uint32_t *)(PLIC_CONTEXT_BASE + offset);

	(void)ctx;
	if (size != 4 || !PLIC_CONTEXT_S(offset / PLIC_CONTEXT_SIZE))
		return;
	*reg = (uint32_t)value;
	if ((offset & (PLIC_CONTEXT_SIZE - 1)) == PLIC_CLAIM)
		riscv64_external_sync();
}

static const struct hv_mmio_dev contexts = {
	.base = PLIC_CONTEXT_BASE,
	.size = PLIC_CONTEXT_END - PLIC_CONTEXT_BASE,
	.read = context_read,
	.write = context_write,
};

void riscv64_vplic_init(void)
{
	hv_mmio_register(&contexts);
}
