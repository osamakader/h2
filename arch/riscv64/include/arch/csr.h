#ifndef ARCH_RISCV64_CSR_H
#define ARCH_RISCV64_CSR_H

/* csr: a name, or a macro for its number (expanded) */
#define CSR_STR(csr)	#csr
#define csr_read(csr) ({ unsigned long __v; \
	__asm__ volatile("csrr %0, " CSR_STR(csr) : "=r"(__v) :: "memory"); __v; })
#define csr_write(csr, v) \
	__asm__ volatile("csrw " CSR_STR(csr) ", %0" :: "rK"(v) : "memory")
#define csr_set(csr, v) \
	__asm__ volatile("csrs " CSR_STR(csr) ", %0" :: "rK"(v) : "memory")
#define csr_clear(csr, v) \
	__asm__ volatile("csrc " CSR_STR(csr) ", %0" :: "rK"(v) : "memory")

/* CSRs the assembler may not know by name */
#define CSR_STIMECMP	0x14d		/* Sstc */
#define CSR_VSTIMECMP	0x24d
#define CSR_HTIMEDELTA	0x605
#define CSR_HENVCFG	0x60a
#define HENVCFG_STCE	(1UL << 63)	/* VS-mode's stimecmp: vstimecmp */

/* interrupt numbers: sip/sie bits and scause codes */
#define IRQ_S_SOFT	1
#define IRQ_VS_SOFT	2
#define IRQ_S_TIMER	5
#define IRQ_VS_TIMER	6
#define IRQ_S_EXT	9
#define IRQ_VS_EXT	10

#endif
