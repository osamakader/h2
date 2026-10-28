#ifndef ARCH_ARM64_PLATFORM_H
#define ARCH_ARM64_PLATFORM_H

/* Plain constants only: included from assembly. */

#if defined(PLATFORM_rpi4)

/*
 * Raspberry Pi 4 (BCM2711; qemu -machine raspi4b): RAM from 0, the
 * peripherals in the firmware's "low peripheral" layout, the console on
 * the PL011 (uart0), whose baud rate the firmware set.
 */
#define PLATFORM_RAM_BASE	0x0
#define PLATFORM_UART0		0xfe201000

#else

/* qemu virt */
#define PLATFORM_RAM_BASE	0x40000000
#define PLATFORM_UART0		0x09000000

#endif

#endif
