#ifndef ARCH_ARM64_BOOT_H
#define ARCH_ARM64_BOOT_H

/* the MPIDR bits that name a CPU: Aff3, Aff2, Aff1, Aff0 */
#define MPIDR_AFFINITY		0xff00ffffffUL

void arm64_el2_init(void);
void arm64_spin_take(unsigned long boot_mpidr);
void arm64_run_guest(int slot, unsigned long entry, unsigned long arg);

#endif
