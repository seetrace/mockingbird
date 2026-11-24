#include <stdio.h>
#include <unistd.h>
#include <poll.h>
#include <termios.h>
#include "serial.h"

static struct termios original_termios;
static int terminal_mode_saved;
static int terminal_closed;

void serial_write(uint8_t value) {
    putchar(value);
    fflush(stdout);
}

int serial_init(void) {
    terminal_closed = 0;

    if (isatty(STDIN_FILENO) &&
        tcgetattr(STDIN_FILENO, &original_termios) == 0) {
        struct termios raw = original_termios;

        raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;

        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0)
            terminal_mode_saved = 1;
    }

    return 0;
}

void serial_destroy(void) {
    terminal_closed = 0;

    if (terminal_mode_saved) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
        terminal_mode_saved = 0;
    }
}

int serial_data_available(void) {
    struct pollfd fd = {
        .fd = STDIN_FILENO,
        .events = POLLIN,
        .revents = 0
    };

    return poll(&fd, 1, 0) > 0 && (fd.revents & POLLIN) != 0;
}

int serial_terminal_closed(void) {
    if (terminal_closed)
        return 1;

    struct pollfd fd = {
        .fd = STDIN_FILENO,
        .events = 0,
        .revents = 0
    };

    if (poll(&fd, 1, 0) <= 0)
        return 0;

    if ((fd.revents & POLLNVAL) != 0)
        return 1;

    if ((fd.revents & (POLLHUP | POLLERR)) != 0 &&
        (fd.revents & POLLIN) == 0)
        return 1;

    return 0;
}

uint8_t serial_read(void) {
    uint8_t value = 0;

    if (serial_data_available() &&
        read(STDIN_FILENO, &value, 1) == 1) {
        if (value == 4) {
            terminal_closed = 1;
            return 0;
        }

        return value;
    }

    if (serial_terminal_closed())
        return 0;

    return 0;
}

uint8_t serial_line_status(void) {
    return (uint8_t)(0x20 | (serial_data_available() ? 0x01 : 0));
}
