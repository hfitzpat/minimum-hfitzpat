/*
 * System call dispatcher and the ioctl syscall.
 *
 * Path of a syscall:
 *   user code -> ioctl() wrapper (user/lib/src/ioctl.c) puts fd/op/args in
 *   r0-r3 and MINEMU_SYSCALL_IOCTL in r7, executes `svc #0`
 *   -> CPU enters SVC mode -> vector table -> minemu_svc_trampoline
 *   (svc_entry.S) saves a trap frame -> minemu_svc_dispatch(frame) below
 *   -> result written into frame->r[0] -> trampoline restores the frame and
 *   returns to the instruction after the svc, back in user mode.
 *
 * The verification idea for the dispatcher (check the SVC really came from
 * A32 user mode via `svc #0`) is adapted from
 * examples/user-mode-hello/kernel/main.c.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/ioctl.h"
#include "kernel/kprintf.h"
#include "kernel/loader.h"
#include "kernel/uart.h"
#include "minemu/irq.h"
#include "minemu/platform.h"
#include "minemu/syscall.h"
#include "minemu/trace.h"
#include "minemu/trap.h"
#include "minemu/user_abi.h"

#define CPSR_MODE_MASK UINT32_C(0x1f)
#define CPSR_MODE_USR UINT32_C(0x10)
#define CPSR_THUMB UINT32_C(0x20)
#define A32_SVC_ZERO UINT32_C(0xef000000) /* encoding of `svc #0`, cond AL */

#define IOCTL_ERROR ((int32_t)MINEMU_IOCTL_ERROR)

enum device {
    DEVICE_NONE,
    DEVICE_UART0,
    DEVICE_UART1,
    DEVICE_RNG,
    DEVICE_TRACE,
};

enum operation {
    OP_INVALID,
    OP_UART_READ,
    OP_UART_WRITE,
    OP_UART_STATUS,
    OP_RNG_SEED,
    OP_RNG_NEXT,
    OP_RNG_STATE,
    OP_TRACE_EVENT,
};

/* ---- decoding: accept both the handout and the user_abi.h encodings ---- */

static enum device device_from_fd(uint32_t fd) {
    switch (fd) {
    case IOCTL_FD_UART0:
    case MINEMU_FD_UART_IN:
    case MINEMU_FD_UART_OUT:
        return DEVICE_UART0;
    case IOCTL_FD_UART1:
    case MINEMU_FD_UART_DIAG:
        return DEVICE_UART1;
    case IOCTL_FD_RNG:
    case MINEMU_FD_RNG:
        return DEVICE_RNG;
    case IOCTL_FD_TRACE:
    case MINEMU_FD_TRACE:
        return DEVICE_TRACE;
    default:
        return DEVICE_NONE;
    }
}

static enum operation operation_for(enum device device, uint32_t op) {
    switch (device) {
    case DEVICE_UART0:
    case DEVICE_UART1:
        if (op == IOCTL_OP_UART_READ || op == MINEMU_IOCTL_UART_READ) {
            return OP_UART_READ;
        }
        if (op == IOCTL_OP_UART_WRITE || op == MINEMU_IOCTL_UART_WRITE) {
            return OP_UART_WRITE;
        }
        if (op == MINEMU_IOCTL_UART_STATUS) {
            return OP_UART_STATUS;
        }
        return OP_INVALID;
    case DEVICE_RNG:
        if (op == IOCTL_OP_RNG_SET_SEED || op == MINEMU_IOCTL_RNG_SEED) {
            return OP_RNG_SEED;
        }
        if (op == IOCTL_OP_RNG_GET_DATA || op == MINEMU_IOCTL_RNG_NEXT) {
            return OP_RNG_NEXT;
        }
        if (op == MINEMU_IOCTL_RNG_STATE) {
            return OP_RNG_STATE;
        }
        return OP_INVALID;
    case DEVICE_TRACE:
        if (op == IOCTL_OP_TRACE_EVENT || op == MINEMU_IOCTL_TRACE_EVENT) {
            return OP_TRACE_EVENT;
        }
        return OP_INVALID;
    case DEVICE_NONE:
    default:
        return OP_INVALID;
    }
}

/* ---- device operations (every user pointer is validated first) ---- */

static volatile struct minemu_uart_regs *uart_regs(enum device device) {
    return device == DEVICE_UART0 ? MINEMU_UART0 : MINEMU_UART1;
}

/* Turn the user's int length into a transfer size, or -1 if negative.
 * Large requests are clamped; the return value tells the caller how much
 * actually moved. */
static int32_t clamp_length(uint32_t raw_length) {
    int32_t length = (int32_t)raw_length;
    if (length < 0) {
        return -1;
    }
    if (length > MINEMU_IOCTL_MAX_TRANSFER) {
        length = MINEMU_IOCTL_MAX_TRANSFER;
    }
    return length;
}

static int32_t do_uart_read(enum device device, uint32_t dst, uint32_t raw_length) {
    int32_t length = clamp_length(raw_length);
    if (length < 0 || !user_range_ok(dst, (uint32_t)length, true)) {
        return IOCTL_ERROR; /* checked before any input is consumed */
    }
    char *out = (char *)(uintptr_t)dst;

    if (device == DEVICE_UART0) {
        /* UART0: ring buffer filled by the RX interrupt, plus whatever is
         * still waiting in the hardware FIFO (see uart_read_available). */
        return (int32_t)uart_read_available(out, (size_t)length);
    }

    /* UART1 has no interrupt driver: read straight from its hardware FIFO. */
    volatile struct minemu_uart_regs *uart = uart_regs(device);
    int32_t count = 0;
    while (count < length && (uart->status & MINEMU_UART_STATUS_RX_READY) != 0) {
        out[count++] = (char)(uint8_t)uart->rx_data;
    }
    return count;
}

static int32_t do_uart_write(enum device device, uint32_t src, uint32_t raw_length) {
    int32_t length = clamp_length(raw_length);
    if (length < 0 || !user_range_ok(src, (uint32_t)length, false)) {
        return IOCTL_ERROR;
    }
    const char *in = (const char *)(uintptr_t)src;

    if (device == DEVICE_UART0) {
        uart_write(in, (size_t)length);
        return length;
    }

    volatile struct minemu_uart_regs *uart = uart_regs(device);
    for (int32_t index = 0; index < length; ++index) {
        while ((uart->status & MINEMU_UART_STATUS_TX_READY) == 0) {
        }
        uart->tx_data = (uint32_t)(uint8_t)in[index];
    }
    return length;
}

static int32_t do_uart_status(enum device device) {
    volatile struct minemu_uart_regs *uart = uart_regs(device);
    uint32_t status = uart->status;
    int32_t result = 0;
    bool rx_ready = device == DEVICE_UART0
                        ? uart_rx_pending() != 0
                        : (status & MINEMU_UART_STATUS_RX_READY) != 0;
    if (rx_ready) {
        result |= MINEMU_IOCTL_UART_RX_READY;
    }
    if ((status & MINEMU_UART_STATUS_TX_READY) != 0) {
        result |= MINEMU_IOCTL_UART_TX_READY;
    }
    return result;
}

/* Store a 32-bit value at a validated user address one byte at a time, so an
 * unaligned user pointer cannot cause an alignment fault in the kernel. */
static void store_user_u32(uint32_t vaddr, uint32_t value) {
    uint8_t *out = (uint8_t *)(uintptr_t)vaddr;
    for (unsigned index = 0; index < 4U; ++index) {
        out[index] = (uint8_t)(value >> (8U * index));
    }
}

static int32_t do_rng_read(enum operation operation, uint32_t data_ptr) {
    /* Validate before touching the RNG: reading DATA advances it. */
    if (!user_range_ok(data_ptr, sizeof(uint32_t), true)) {
        return IOCTL_ERROR;
    }
    uint32_t value = operation == OP_RNG_NEXT ? MINEMU_RNG->data : MINEMU_RNG->state;
    store_user_u32(data_ptr, value);
    return 0;
}

static int32_t sys_ioctl(uint32_t fd, uint32_t op, uint32_t arg0, uint32_t arg1) {
    enum device device = device_from_fd(fd);
    enum operation operation = operation_for(device, op);
    switch (operation) {
    case OP_UART_READ:
        return do_uart_read(device, arg0, arg1);
    case OP_UART_WRITE:
        return do_uart_write(device, arg0, arg1);
    case OP_UART_STATUS:
        return do_uart_status(device);
    case OP_RNG_SEED:
        MINEMU_RNG->seed = arg0;
        return 0;
    case OP_RNG_NEXT:
    case OP_RNG_STATE:
        return do_rng_read(operation, arg0);
    case OP_TRACE_EVENT:
        minemu_trace_event(arg0);
        return 0;
    case OP_INVALID:
    default:
        return IOCTL_ERROR;
    }
}

/* ---- dispatcher ---- */

/* True only for `svc #0` executed in A32 user mode. */
static bool is_user_svc_zero(const struct minemu_trap_frame *frame) {
    if (frame->exception_id != MINEMU_EXCEPTION_SVC ||
        (frame->spsr & CPSR_MODE_MASK) != CPSR_MODE_USR ||
        (frame->spsr & CPSR_THUMB) != 0 ||
        !user_range_ok(frame->fault_pc, 4U, false)) {
        return false;
    }
    return *(const uint32_t *)(uintptr_t)frame->fault_pc == A32_SVC_ZERO;
}

struct minemu_trap_frame *minemu_svc_dispatch(struct minemu_trap_frame *frame) {
    if (!is_user_svc_zero(frame)) {
        frame->r[0] = (uint32_t)IOCTL_ERROR;
        return frame;
    }

    switch (frame->r[7]) {
    case MINEMU_SYSCALL_IOCTL:
        frame->r[0] = (uint32_t)sys_ioctl(frame->r[0], frame->r[1], frame->r[2], frame->r[3]);
        break;
    case MINEMU_SYSCALL_EXIT:
        /* Only one user program exists, so when it exits there is nothing
         * left to run: stop the machine. */
        minemu_fail_stop();
    default:
        frame->r[0] = (uint32_t)IOCTL_ERROR; /* unknown or unimplemented */
        break;
    }
    return frame;
}

/* ---- faults: a crashing user program stops the system with a message ---- */

static void report_fault(const char *kind, const struct minemu_trap_frame *frame) {
    kprintf("\nkernel: %s at pc=0x%08x (dfsr=0x%08x dfar=0x%08x); stopping\n", kind,
            (unsigned)frame->fault_pc, (unsigned)frame->dfsr, (unsigned)frame->dfar);
}

void minemu_undefined_dispatch(struct minemu_trap_frame *frame) {
    report_fault("undefined instruction", frame);
    minemu_fail_stop();
}

void minemu_abort_dispatch(struct minemu_trap_frame *frame) {
    report_fault(frame->exception_id == MINEMU_EXCEPTION_DATA_ABORT ? "data abort"
                                                                    : "prefetch abort",
                 frame);
    minemu_fail_stop();
}
