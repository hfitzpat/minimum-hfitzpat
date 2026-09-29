#ifndef KERNEL_IRQ_HANDLERS_H
#define KERNEL_IRQ_HANDLERS_H

#include <stdint.h>

typedef void (*irq_handler_t)(void);

/* Install (or replace) the handler for an interrupt controller source ID.
 * minemu_irq_dispatch calls it with IRQs masked, then writes EOI. */
void irq_register_handler(uint32_t source, irq_handler_t handler);

#endif
