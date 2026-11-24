#include <stdio.h>
#include "serial.h"

void serial_write(uint8_t value) {
    putchar(value);
    fflush(stdout);
}
