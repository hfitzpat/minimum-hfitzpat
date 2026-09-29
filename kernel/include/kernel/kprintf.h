#ifndef KERNEL_KPRINTF_H
#define KERNEL_KPRINTF_H

#include <stdarg.h>

/*
 * Minimal printf for the kernel console (UART0).
 *
 * Supported conversions: %c %s %d %i %u %x %X %p %%
 * Supported modifiers:   '0' flag and a field width (e.g. %08x), 'l' (ignored,
 *                        since int and long are both 32 bits here).
 *
 * Returns the number of bytes written.
 */
int kprintf(const char *format, ...) __attribute__((format(printf, 1, 2)));
int kvprintf(const char *format, va_list args);

#endif
