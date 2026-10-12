#ifndef KERNEL_IOCTL_H
#define KERNEL_IOCTL_H

/*
 * ioctl encodings shared by the kernel and user programs.
 *
 * The syscall numbers (MINEMU_SYSCALL_IOCTL = 1, passed in r7) and the
 * request codes used by the public tests come from minemu/user_abi.h, which
 * user programs and the kernel both include.
 *
 * The assignment handout also describes an equivalent encoding where `fd` is
 * the device's MMIO base address and `op` is a small per-device number. The
 * kernel accepts both; these names make the handout encoding usable too.
 *
 *   int ioctl(int fd, int op, ...);   r0 = fd, r1 = op, r2/r3 = variadic args
 *
 *   fd                         op                       args
 *   IOCTL_FD_RNG               IOCTL_OP_RNG_SET_SEED    unsigned int seed
 *   IOCTL_FD_RNG               IOCTL_OP_RNG_GET_DATA    unsigned int *data
 *   IOCTL_FD_UART0/UART1       IOCTL_OP_UART_READ       char *dst, int n
 *   IOCTL_FD_UART0/UART1       IOCTL_OP_UART_WRITE      char *src, int n
 *   IOCTL_FD_TRACE             IOCTL_OP_TRACE_EVENT     unsigned int value
 *
 * Equivalent user_abi.h names: MINEMU_FD_RNG (3), MINEMU_FD_UART_IN (0) and
 * MINEMU_FD_UART_OUT (1) for UART0, MINEMU_FD_UART_DIAG (2) for UART1,
 * MINEMU_FD_TRACE (4); MINEMU_IOCTL_RNG_SEED / _RNG_NEXT / _RNG_STATE,
 * MINEMU_IOCTL_UART_READ / _UART_WRITE / _UART_STATUS, MINEMU_IOCTL_TRACE_EVENT.
 *
 * Every request returns a negative value (MINEMU_IOCTL_ERROR) for an unknown
 * fd, an op the device does not support, a negative length, or a pointer
 * range that is not fully mapped for user access with the needed permission.
 * UART transfers move at most MINEMU_IOCTL_MAX_TRANSFER bytes per call and
 * return the number of bytes moved, so callers loop for longer buffers.
 */

#define IOCTL_FD_RNG 0x10003000
#define IOCTL_FD_UART0 0x10004000
#define IOCTL_FD_UART1 0x10005000
#define IOCTL_FD_TRACE 0x1000f000

#define IOCTL_OP_RNG_SET_SEED 0x0
#define IOCTL_OP_RNG_GET_DATA 0x1
#define IOCTL_OP_UART_READ 0x0
#define IOCTL_OP_UART_WRITE 0x1
#define IOCTL_OP_TRACE_EVENT 0x0

#endif
