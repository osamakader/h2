#ifndef HV_VUART_H
#define HV_VUART_H

#include <hv/types.h>

/*
 * The guest's console UART, emulated where its real one is (base), the
 * real one now h2's: what the guest sends goes out on it, what comes in
 * on it (its interrupt, irq, which the guest's one has too) is the
 * guest's to read. Before stage 2 is built.
 */
void hv_vuart_pl011_init(hv_addr_t base, hv_addr_t size, unsigned int irq);
void hv_vuart_16550_init(hv_addr_t base, unsigned int irq);

#endif
