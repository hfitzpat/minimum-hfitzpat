#ifndef KERNEL_MSH_H
#define KERNEL_MSH_H

/* Maximum number of bytes in one command line, excluding the '\n'. A line
 * longer than this is discarded in its entirety when '\n' arrives. */
#define MSH_LINE_MAX 20

/* Run the minimum kernel shell forever. Requires uart_init() and IRQs on. */
void msh_run(void) __attribute__((noreturn));

#endif
