/*
 * Student test fixture: user mode must not be able to bypass the syscall
 * interface. Writing a UART0 register directly from user mode has to fault
 * (MMIO pages are supervisor-only), and the kernel reports the data abort
 * on the console and stops instead of letting the byte through.
 */
#include <stdint.h>

#include "minemu/user_abi.h"

void minemu_user_main(void) {
    static char before[] = "before\n";
    (void)ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE, (void *)before, UINT32_C(7));

    /* UART0 tx_data register (0x10004004): should raise a data abort. */
    *(volatile uint32_t *)(uintptr_t)0x10004004u = (uint32_t)'!';

    static char after[] = "after\n"; /* must never be printed */
    (void)ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE, (void *)after, UINT32_C(6));
    while (1) {
    }
}
