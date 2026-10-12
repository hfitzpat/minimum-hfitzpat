/*
 * Task 1: user-mode "hello world".
 *
 * This program runs in A32 USR mode, so it cannot touch the UART registers
 * itself (MMIO is mapped supervisor-only). It asks the kernel to print for
 * it with the ioctl system call: ioctl() puts fd/request/buffer/length in
 * r0-r3 and MINEMU_SYSCALL_IOCTL in r7, then executes `svc #0`.
 */
#include <stdint.h>

#include "minemu/user_abi.h"

void minemu_user_main(void) {
    static char message[] = "hello world\n";
    (void)ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE, (void *)message,
                (uint32_t)(sizeof(message) - 1U));

    /* Nothing else to do: stay in user mode. */
    for (;;) {
    }
}
