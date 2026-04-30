#ifndef ARCH_ARM64_TRAP_H
#define ARCH_ARM64_TRAP_H

#define ARM64_ESR_EC(esr)	(((esr) >> 26) & 0x3f)
#define ARM64_ESR_EC_HVC64	0x16

#endif
