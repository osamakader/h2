#ifndef ARCH_ARM64_PSCI_H
#define ARCH_ARM64_PSCI_H

#include <hv/hv.h>

/* Handles a PSCI call from the guest; 0 if fid isn't one. */
int arm64_psci_call(struct hv_trap_frame *frame);

#endif
