#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

void serial_write(uint8_t value);
int serial_init(void);
void serial_destroy(void);
int serial_data_available(void);
int serial_terminal_closed(void);
uint8_t serial_read(void);
uint8_t serial_line_status(void);

#endif
