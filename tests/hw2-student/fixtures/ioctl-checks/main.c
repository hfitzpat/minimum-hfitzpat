/*
 * Student test fixture: ioctl error handling, pointer validation, UART1 and
 * the handout's (base address, op) encoding.
 *
 * Each check emits its ioctl return value as a TRACE event; the manifest
 * (tests/hw2-student/ioctl-checks.toml) compares the full list of events
 * against the expected values.
 */
#include <stdint.h>

#include "kernel/ioctl.h"
#include "minemu/user_abi.h"

#define END_MARKER UINT32_C(0x600dd0e5)

static char message[] = "x";
static char big[300];
static char small[8];
static uint8_t bytes[8];

static void trace(uint32_t value) {
    (void)ioctl(MINEMU_FD_TRACE, MINEMU_IOCTL_TRACE_EVENT, value);
}

/* Raw `svc #0` so we can use syscall numbers and request codes that the
 * supplied ioctl() wrapper does not know how to pass arguments for. */
static int32_t raw_svc(uint32_t number, uint32_t a0, uint32_t a1, uint32_t a2,
                       uint32_t a3) {
    register uint32_t r0 __asm__("r0") = a0;
    register uint32_t r1 __asm__("r1") = a1;
    register uint32_t r2 __asm__("r2") = a2;
    register uint32_t r3 __asm__("r3") = a3;
    register uint32_t r7 __asm__("r7") = number;
    __asm__ volatile("svc #0"
                     : "+r"(r0)
                     : "r"(r1), "r"(r2), "r"(r3), "r"(r7)
                     : "memory", "cc");
    return (int32_t)r0;
}

static int32_t handout_ioctl(uint32_t fd, uint32_t op, uint32_t a0, uint32_t a1) {
    return raw_svc(MINEMU_SYSCALL_IOCTL, fd, op, a0, a1);
}

#define PTR(p) ((uint32_t)(uintptr_t)(p))

void minemu_user_main(void) {
    uint32_t value = 0;

    /* 1-3: bad fd, unknown op, op that belongs to another device */
    trace((uint32_t)ioctl(99, MINEMU_IOCTL_UART_WRITE, (void *)message, UINT32_C(1)));
    trace((uint32_t)ioctl(MINEMU_FD_UART_OUT, UINT32_C(0x999)));
    trace((uint32_t)ioctl(MINEMU_FD_RNG, MINEMU_IOCTL_UART_WRITE, (void *)message, UINT32_C(1)));

    /* 4-9: bad buffers (NULL, kernel memory, MMIO, negative length, range
     * running off the end of the stack page, wrap-around) */
    trace((uint32_t)ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE, (void *)0, UINT32_C(5)));
    trace((uint32_t)ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE,
                          (void *)(uintptr_t)0xc0030000u, UINT32_C(4)));
    trace((uint32_t)ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE,
                          (void *)(uintptr_t)0x10004000u, UINT32_C(4)));
    trace((uint32_t)ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE, (void *)message,
                          UINT32_C(0xffffffff)));
    trace((uint32_t)ioctl(MINEMU_FD_UART_IN, MINEMU_IOCTL_UART_READ,
                          (void *)(uintptr_t)0x007ffffeu, UINT32_C(4)));
    trace((uint32_t)ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE,
                          (void *)(uintptr_t)0xfffffff0u, UINT32_C(0x20)));

    /* 10-11: a bad RNG pointer must fail WITHOUT advancing the RNG, so the
     * state afterwards is still the reset seed 0x4d454d55. */
    trace((uint32_t)ioctl(MINEMU_FD_RNG, MINEMU_IOCTL_RNG_NEXT, (uint32_t *)0));
    trace((uint32_t)ioctl(MINEMU_FD_RNG, MINEMU_IOCTL_RNG_STATE, &value));
    trace(value);

    /* 12-14: zero-length write, clamped large write, UART status */
    trace((uint32_t)ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE, (void *)0, UINT32_C(0)));
    for (unsigned i = 0; i < sizeof(big); ++i) {
        big[i] = '.';
    }
    trace((uint32_t)ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE, (void *)big,
                          (uint32_t)sizeof(big)));
    trace((uint32_t)ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_STATUS));

    /* 15: unknown syscall number */
    trace((uint32_t)raw_svc(42, 0, 0, 0, 0));

    /* 16-17: handout encoding for RNG: set_seed (op 0), get_data (op 1) */
    trace((uint32_t)handout_ioctl(IOCTL_FD_RNG, IOCTL_OP_RNG_SET_SEED, 0x12345678u, 0));
    value = 0;
    trace((uint32_t)handout_ioctl(IOCTL_FD_RNG, IOCTL_OP_RNG_GET_DATA, PTR(&value), 0));
    trace(value);

    /* 18: unaligned output pointer is fine (kernel stores byte by byte) */
    trace((uint32_t)ioctl(MINEMU_FD_RNG, MINEMU_IOCTL_RNG_NEXT, (uint32_t *)(void *)&bytes[1]));

    /* 19: handout encoding for TRACE (emits 0xcafef00d itself, returns 0) */
    trace((uint32_t)handout_ioctl(IOCTL_FD_TRACE, IOCTL_OP_TRACE_EVENT, 0xcafef00du, 0));

    /* 20: UART1 write through the handout encoding */
    static char uart1_text[] = "u1 ok\n";
    trace((uint32_t)handout_ioctl(IOCTL_FD_UART1, IOCTL_OP_UART_WRITE, PTR(uart1_text), 6));

    /* 21: UART1 read: the manifest sends "ping" to UART1; poll until it is
     * here, then send it back on UART1 as "got:ping". */
    int32_t count = 0;
    for (unsigned tries = 0; tries < 100000u && count == 0; ++tries) {
        count = ioctl(MINEMU_FD_UART_DIAG, MINEMU_IOCTL_UART_READ, (void *)small,
                      (uint32_t)sizeof(small));
    }
    trace((uint32_t)count);
    if (count > 0) {
        static char prefix[] = "got:";
        (void)ioctl(MINEMU_FD_UART_DIAG, MINEMU_IOCTL_UART_WRITE, (void *)prefix, UINT32_C(4));
        (void)ioctl(MINEMU_FD_UART_DIAG, MINEMU_IOCTL_UART_WRITE, (void *)small, (uint32_t)count);
    }

    /* 22-23: handout encoding for UART0 write and (empty) read */
    static char uart0_text[] = "h0\n";
    trace((uint32_t)handout_ioctl(IOCTL_FD_UART0, IOCTL_OP_UART_WRITE, PTR(uart0_text), 3));
    trace((uint32_t)handout_ioctl(IOCTL_FD_UART0, IOCTL_OP_UART_READ, PTR(small), 8));

    /* 24: TRACE has no op 1 */
    trace((uint32_t)handout_ioctl(IOCTL_FD_TRACE, 1, 0, 0));

    trace(END_MARKER);
    __asm__ volatile("bkpt #0");
    while (1) {
    }
}
