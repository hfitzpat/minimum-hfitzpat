#include "kernel/msh.h"

#include <stddef.h>

#include "kernel/kprintf.h"
#include "kernel/uart.h"

#define ASCII_BS 0x08
#define ASCII_DEL 0x7f

struct line {
    char data[MSH_LINE_MAX];
    size_t length;   /* bytes stored in data (<= MSH_LINE_MAX) */
    size_t typed;    /* logical length as the user sees it on screen */
    int overflowed;  /* typed > MSH_LINE_MAX when '\n' arrived */
};

/*
 * Read one '\n'-terminated line into `line`.
 *
 * The TUI already mirrors typed characters (including backspace) to the
 * console, so we never echo input back.
 *
 * - 0x08 and 0x7f delete the previous character; ignored on an empty line.
 * - Only the first MSH_LINE_MAX bytes are stored, but we keep counting how
 *   many characters are logically on the line. If the user types past the
 *   limit and then backspaces back under it, the (visible) line is valid and
 *   is accepted. If the line is still too long when '\n' arrives, the whole
 *   line is discarded and the shell simply prompts again.
 */
static void read_line(struct line *line) {
    line->length = 0;
    line->typed = 0;
    line->overflowed = 0;

    for (;;) {
        char c = uart_getc();

        if (c == '\n') {
            line->overflowed = line->typed > MSH_LINE_MAX;
            return;
        }
        if (c == ASCII_BS || c == ASCII_DEL) {
            if (line->typed > 0) {
                --line->typed;
                if (line->typed < line->length) {
                    line->length = line->typed;
                }
            }
            continue;
        }
        if (line->length < MSH_LINE_MAX && line->typed == line->length) {
            line->data[line->length++] = c;
        }
        ++line->typed;
    }
}

/* Words are separated by one or more ASCII spaces (0x20) only. */
static size_t skip_spaces(const struct line *line, size_t i) {
    while (i < line->length && line->data[i] == ' ') {
        ++i;
    }
    return i;
}

static size_t word_end(const struct line *line, size_t i) {
    while (i < line->length && line->data[i] != ' ') {
        ++i;
    }
    return i;
}

static int word_equals(const struct line *line, size_t start, size_t end,
                       const char *name) {
    size_t i = start;
    while (i < end && *name != '\0' && line->data[i] == *name) {
        ++i;
        ++name;
    }
    return i == end && *name == '\0';
}

/* echo: print the arguments separated by single spaces, then '\n'. */
static void cmd_echo(const struct line *line, size_t i) {
    int first = 1;
    for (i = skip_spaces(line, i); i < line->length; i = skip_spaces(line, i)) {
        size_t end = word_end(line, i);
        if (!first) {
            uart_putc(' ');
        }
        uart_write(&line->data[i], end - i);
        first = 0;
        i = end;
    }
    uart_putc('\n');
}

static void execute(const struct line *line) {
    size_t start = skip_spaces(line, 0);
    if (start == line->length) {
        return; /* empty or all-space line */
    }
    size_t end = word_end(line, start);

    if (word_equals(line, start, end, "echo")) {
        cmd_echo(line, end);
        return;
    }

    uart_puts("command not found: ");
    uart_write(&line->data[start], end - start);
    uart_putc('\n');
}

void msh_run(void) {
    struct line line;
    for (;;) {
        uart_puts("msh> ");
        read_line(&line);
        if (!line.overflowed) {
            execute(&line);
        }
    }
}
