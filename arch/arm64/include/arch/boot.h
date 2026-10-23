#ifndef ARCH_ARM64_BOOT_H
#define ARCH_ARM64_BOOT_H

void arm64_el2_init(void);
void arm64_enter_guest(unsigned long entry, unsigned long stack,
		       unsigned long arg);

#endif
