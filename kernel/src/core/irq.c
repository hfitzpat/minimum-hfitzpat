#include <stdint.h>

#include "kernel/irq_handlers.h"
#include "kernel/uart.h"
#include "minemu/irq.h"
#include "minemu/platform.h"

#define IRQ_SOURCE_COUNT 4u

/* Per-source handler table, indexed by interrupt controller source ID. */
static irq_handler_t irq_handlers[IRQ_SOURCE_COUNT] = {
    [MINEMU_IRQ_UART0] = uart_irq_handler,
};

void irq_register_handler(uint32_t source, irq_handler_t handler) {
    if (source < IRQ_SOURCE_COUNT) {
        minemu_irq_disable();
        irq_handlers[source] = handler;
        minemu_irq_enable();
    }
}

struct minemu_trap_frame *minemu_irq_dispatch(struct minemu_trap_frame *frame) {
    uint32_t source = (uint32_t)frame->exception_id;

    if (source == MINEMU_IRQ_NONE || source >= IRQ_SOURCE_COUNT) {
        /* Spurious/unknown claim: nothing to acknowledge. */
        return frame;
    }

    irq_handler_t handler = irq_handlers[source];
    if (handler != 0) {
        handler();
    } else {
        /* Unexpected source with no handler: stop it from firing again. */
        MINEMU_INTERRUPT->enable =
            MINEMU_INTERRUPT->enable & ~(UINT32_C(1) << source);
    }

    MINEMU_INTERRUPT->eoi = source;
    return frame;
}
