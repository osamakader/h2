#ifndef ARCH_ARM64_PLATFORM_H
#define ARCH_ARM64_PLATFORM_H

/* Plain constants only: included from assembly. */

#if defined(PLATFORM_rpi4)

/*
 * Raspberry Pi 4 (BCM2711; qemu -machine raspi4b): RAM from 0, the
 * peripherals in the firmware's "low peripheral" layout, the console on
 * the PL011 (uart0), whose baud rate the firmware set; one cluster of 4
 * CPUs, MPIDR 0..3.
 */
#define PLATFORM_RAM_BASE	0x0
#define PLATFORM_UART0		0xfe201000

/* the firmware's spin table: CPU n (MPIDR Aff0) waits on 0xd8 + 8n */
#define PLATFORM_SPIN_TABLE	0xd8
#define PLATFORM_SPIN_CPUS	4

/* the GIC-400 (GICv2): CPU interface, its virtual one and its control; no GICv3 redistributors */
#define PLATFORM_GICD		0xff841000
#define PLATFORM_GICC		0xff842000
#define PLATFORM_GICH		0xff844000
#define PLATFORM_GICV		0xff846000
#define PLATFORM_GICC_SIZE	0x2000
#define PLATFORM_GICH_SIZE	0x2000

#else

/* qemu virt */
#define PLATFORM_RAM_BASE	0x40000000
#define PLATFORM_UART0		0x09000000

/* the GIC: gic-version=3 (redistributors), or 2 (GICC, GICV, GICH) */
#define PLATFORM_GICD		0x08000000
#define PLATFORM_GICR		0x080a0000
#define PLATFORM_GICC		0x08010000
#define PLATFORM_GICH		0x08030000
#define PLATFORM_GICV		0x08040000
#define PLATFORM_GICC_SIZE	0x10000
#define PLATFORM_GICH_SIZE	0x10000

#endif

#endif
