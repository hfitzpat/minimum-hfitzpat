/*
 * msh: the minimum shell, running as a user-mode program.
 *
 * The line editing and command logic is carried over from the HW1 kernel
 * shell (kernel/src/shell/msh.c). What changed is how it talks to the
 * console: user mode cannot touch the UART registers (they are mapped
 * supervisor-only), so every byte goes through the ioctl system call.
 */
#include <stddef.h>
#include <stdint.h>

#include "minemu/user_abi.h"

/*
 * Maximum number of bytes in one command line, excluding the '\n'. A longer
 * line is discarded in its entirety when '\n' arrives.
 *
 * NOTE: the handout says lines can be up to 20 bytes (HW1), but the HW2
 * public tests send 22- and 25-byte lines and expect them to work
 * (msh-editing, msh-echo), so the limit is raised here. Pending instructor
 * confirmation; this constant is the only thing to change.
 */
#define MSH_LINE_MAX 64

#define ASCII_BS 0x08
#define ASCII_DEL 0x7f

struct line {
    char data[MSH_LINE_MAX];
    size_t length;   /* bytes stored in data (<= MSH_LINE_MAX) */
    size_t typed;    /* logical length as the user sees it on screen */
    int overflowed;  /* typed > MSH_LINE_MAX when '\n' arrived */
};

/* ---- console I/O through ioctl ---- */

static char input_buffer[32];
static size_t input_length;
static size_t input_position;

/* Blocking read of one byte. UART_READ never waits: it returns whatever has
 * arrived (possibly 0 bytes), so we poll until something shows up. While we
 * poll in user mode, IRQs are enabled, so the kernel's UART interrupt
 * handler keeps moving received bytes into its buffer. */
static char read_char(void) {
    while (input_position == input_length) {
        int32_t count = ioctl(MINEMU_FD_UART_IN, MINEMU_IOCTL_UART_READ,
                              (void *)input_buffer, (uint32_t)sizeof(input_buffer));
        if (count > 0) {
            input_length = (size_t)count;
            input_position = 0;
        }
    }
    return input_buffer[input_position++];
}

/* Write all bytes; ioctl may move fewer than requested per call. */
static void write_bytes(const char *data, size_t length) {
    while (length > 0) {
        int32_t count = ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE,
                              (void *)data, (uint32_t)length);
        if (count <= 0) {
            return; /* nothing sensible to do if the console rejects output */
        }
        data += count;
        length -= (size_t)count;
    }
}

static void write_string(const char *text) {
    size_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    write_bytes(text, length);
}

static void write_char(char c) {
    write_bytes(&c, 1);
}

/* ---- line editing (unchanged from HW1) ---- */

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
        char c = read_char();

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

/* ---- command parsing and execution (unchanged from HW1) ---- */

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
            write_char(' ');
        }
        write_bytes(&line->data[i], end - i);
        first = 0;
        i = end;
    }
    write_char('\n');
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

    write_string("command not found: ");
    write_bytes(&line->data[start], end - start);
    write_char('\n');
}

void minemu_user_main(void) {
    struct line line;
    for (;;) {
        write_string("msh> ");
        read_line(&line);
        if (!line.overflowed) {
            execute(&line);
        }
    }
}
