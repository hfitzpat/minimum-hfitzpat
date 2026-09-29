#ifndef KERNEL_UART_H
#define KERNEL_UART_H

#include <stddef.h>
#include <stdint.h>

/*
 * UART0 console driver.
 *
 * Output is synchronous: each byte waits for TX_READY and is then written to
 * tx_data.
 *
 * Input is interrupt-driven: the RX interrupt handler drains every byte out
 * of the hardware FIFO into a software ring buffer. The main program then
 * reads bytes out of the ring buffer with uart_getc()/uart_try_getc(). The
 * ring buffer is the only state shared between the IRQ handler and the main
 * program, and every main-program access to it happens with IRQs masked.
 */

/* Size of the software receive ring buffer (must be a power of two). */
#define UART_RX_BUFFER_SIZE 256u

/* Reset driver state and enable the UART0 RX interrupt at the device and the
 * interrupt controller. Does NOT unmask IRQs on the CPU; the caller does that
 * once all handlers are installed. */
void uart_init(void);

/* Blocking output. */
void uart_putc(char c);
void uart_puts(const char *s);
void uart_write(const char *data, size_t length);

/* Non-blocking input: returns the next received byte (0-255), or -1 if the
 * receive buffer is currently empty. */
int uart_try_getc(void);

/* Blocking input: waits (with IRQs periodically unmasked so the RX handler
 * can run) until a byte is available, then returns it. */
char uart_getc(void);

/* IRQ handler for MINEMU_IRQ_UART0. Called by minemu_irq_dispatch with IRQs
 * masked. Drains the hardware RX FIFO into the ring buffer; if the ring is
 * full it pauses the device RX interrupt (no bytes are dropped) until the
 * reader frees space. */
void uart_irq_handler(void);

#endif
