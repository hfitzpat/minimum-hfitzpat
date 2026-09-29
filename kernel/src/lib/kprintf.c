#include "kernel/kprintf.h"

#include <stdint.h>

#include "kernel/uart.h"

static int emit_unsigned(uint32_t value, unsigned base, int upper, int width,
                         char pad) {
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char buffer[32];
    int length = 0;

    do {
        buffer[length++] = digits[value % base];
        value /= base;
    } while (value != 0);

    int written = 0;
    while (length + written < width) {
        uart_putc(pad);
        ++written;
    }
    while (length > 0) {
        uart_putc(buffer[--length]);
        ++written;
    }
    return written;
}

static int emit_string(const char *s, int width) {
    if (s == 0) {
        s = "(null)";
    }
    int length = 0;
    while (s[length] != '\0') {
        ++length;
    }
    int written = 0;
    while (length + written < width) {
        uart_putc(' ');
        ++written;
    }
    uart_write(s, (size_t)length);
    return written + length;
}

int kvprintf(const char *format, va_list args) {
    int written = 0;

    for (const char *p = format; *p != '\0'; ++p) {
        if (*p != '%') {
            uart_putc(*p);
            ++written;
            continue;
        }

        ++p;
        char pad = ' ';
        int width = 0;
        if (*p == '0') {
            pad = '0';
            ++p;
        }
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            ++p;
        }
        while (*p == 'l') {
            ++p;
        }

        switch (*p) {
        case 'c':
            uart_putc((char)va_arg(args, int));
            ++written;
            break;
        case 's':
            written += emit_string(va_arg(args, const char *), width);
            break;
        case 'd':
        case 'i': {
            int32_t value = va_arg(args, int32_t);
            uint32_t magnitude = (uint32_t)value;
            if (value < 0) {
                uart_putc('-');
                ++written;
                magnitude = 0u - magnitude;
                if (width > 0) {
                    --width;
                }
            }
            written += emit_unsigned(magnitude, 10, 0, width, pad);
            break;
        }
        case 'u':
            written += emit_unsigned(va_arg(args, uint32_t), 10, 0, width, pad);
            break;
        case 'x':
        case 'X':
            written += emit_unsigned(va_arg(args, uint32_t), 16, *p == 'X',
                                     width, pad);
            break;
        case 'p':
            uart_puts("0x");
            written += 2 + emit_unsigned((uint32_t)(uintptr_t)va_arg(args, void *),
                                         16, 0, 8, '0');
            break;
        case '%':
            uart_putc('%');
            ++written;
            break;
        case '\0':
            /* Trailing lone '%': stop. */
            return written;
        default:
            /* Unknown conversion: print it verbatim. */
            uart_putc('%');
            uart_putc(*p);
            written += 2;
            break;
        }
    }
    return written;
}

int kprintf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int written = kvprintf(format, args);
    va_end(args);
    return written;
}
