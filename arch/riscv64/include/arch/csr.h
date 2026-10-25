#ifndef ARCH_RISCV64_CSR_H
#define ARCH_RISCV64_CSR_H

#define csr_read(csr) ({ unsigned long __v; \
	__asm__ volatile("csrr %0, " #csr : "=r"(__v) :: "memory"); __v; })
#define csr_write(csr, v) \
	__asm__ volatile("csrw " #csr ", %0" :: "rK"(v) : "memory")
#define csr_set(csr, v) \
	__asm__ volatile("csrs " #csr ", %0" :: "rK"(v) : "memory")
#define csr_clear(csr, v) \
	__asm__ volatile("csrc " #csr ", %0" :: "rK"(v) : "memory")

/* interrupt numbers: sip/sie bits and scause codes */
#define IRQ_S_SOFT	1
#define IRQ_VS_SOFT	2
#define IRQ_S_TIMER	5
#define IRQ_VS_TIMER	6
#define IRQ_S_EXT	9
#define IRQ_VS_EXT	10

#endif
