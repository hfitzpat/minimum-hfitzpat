#include "kernel/uart.h"

#include "minemu/irq.h"
#include "minemu/platform.h"

_Static_assert((UART_RX_BUFFER_SIZE & (UART_RX_BUFFER_SIZE - 1u)) == 0,
               "UART_RX_BUFFER_SIZE must be a power of two");

/*
 * Receive ring buffer shared between the UART IRQ handler (producer) and the
 * main program (consumer).
 *
 * head: index of the next slot the IRQ handler writes.
 * tail: index of the next slot the main program reads.
 * The buffer is empty when head == tail and full when head - tail == SIZE
 * (indices are free-running and masked on access).
 *
 * The producer runs in IRQ mode, where the CPU has already masked further
 * IRQs, so it cannot be interrupted. The consumer disables IRQs around every
 * access so the handler can never observe or modify a half-updated state.
 * `volatile` keeps the compiler from caching these values in registers across
 * the IRQ-enable points.
 */
static volatile uint8_t rx_buffer[UART_RX_BUFFER_SIZE];
static volatile uint32_t rx_head;
static volatile uint32_t rx_tail;
static volatile uint32_t rx_paused; /* RX IRQ disabled because ring is full */

void uart_init(void) {
    minemu_irq_disable();
    rx_head = 0;
    rx_tail = 0;
    rx_paused = 0;

    /* Discard anything that arrived before we were ready. */
    while (MINEMU_UART0->status & MINEMU_UART_STATUS_RX_READY) {
        (void)MINEMU_UART0->rx_data;
    }

    MINEMU_UART0->control = MINEMU_UART_CONTROL_RX_IRQ_ENABLE;
    MINEMU_INTERRUPT->enable =
        MINEMU_INTERRUPT->enable | (UINT32_C(1) << MINEMU_IRQ_UART0);
}

void uart_putc(char c) {
    while ((MINEMU_UART0->status & MINEMU_UART_STATUS_TX_READY) == 0) {
        /* spin until the transmitter can accept another byte */
    }
    MINEMU_UART0->tx_data = (uint32_t)(uint8_t)c;
}

void uart_write(const char *data, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        uart_putc(data[i]);
    }
}

void uart_puts(const char *s) {
    while (*s != '\0') {
        uart_putc(*s++);
    }
}

/* Must be called with IRQs masked. */
static int rx_pop_locked(void) {
    if (rx_head == rx_tail) {
        return -1;
    }
    uint8_t byte = rx_buffer[rx_tail & (UART_RX_BUFFER_SIZE - 1u)];
    rx_tail = rx_tail + 1u;
    if (rx_paused) {
        /* There is room again: let the device interrupt us for the bytes that
         * have been waiting in its hardware FIFO. */
        rx_paused = 0;
        MINEMU_UART0->control = MINEMU_UART_CONTROL_RX_IRQ_ENABLE;
    }
    return byte;
}

int uart_try_getc(void) {
    minemu_irq_disable();
    int c = rx_pop_locked();
    minemu_irq_enable();
    return c;
}

char uart_getc(void) {
    int c;
    minemu_irq_disable();
    while ((c = rx_pop_locked()) < 0) {
        /*
         * Buffer is empty: open a brief window with IRQs unmasked so a pending
         * UART interrupt can run its handler, then mask again before
         * re-checking the buffer. The buffer is only ever inspected with IRQs
         * masked.
         *
         * On real hardware this is where a WFI (wait-for-interrupt) would go
         * to save power. minemu 0.2.x does not resume from WFI when an IRQ
         * becomes pending (the test harness never sees further output), so we
         * busy-wait instead.
         */
        minemu_irq_enable();
        __asm__ volatile("nop" : : : "memory");
        minemu_irq_disable();
    }
    minemu_irq_enable();
    return (char)c;
}

void uart_irq_handler(void) {
    /*
     * Drain the hardware FIFO into the ring buffer. The RX interrupt stays
     * asserted as long as unread bytes remain AND the RX interrupt is enabled,
     * so before EOI we must either empty the FIFO or turn the interrupt off.
     *
     * Flow control: if the ring buffer fills up, we stop reading and disable
     * the device's RX interrupt instead of throwing bytes away. The remaining
     * bytes wait safely in the device's own 4 KiB FIFO, and rx_pop_locked()
     * re-enables the interrupt as soon as the main program frees a slot.
     */
    while (MINEMU_UART0->status & MINEMU_UART_STATUS_RX_READY) {
        if (rx_head - rx_tail >= UART_RX_BUFFER_SIZE) {
            rx_paused = 1;
            MINEMU_UART0->control = 0;
            return;
        }
        rx_buffer[rx_head & (UART_RX_BUFFER_SIZE - 1u)] =
            (uint8_t)MINEMU_UART0->rx_data;
        rx_head = rx_head + 1u;
    }
}
