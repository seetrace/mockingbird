#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t *data;
    size_t size;
} GuestMemory;

int memory_init(GuestMemory *mem, size_t size);
void memory_destroy(GuestMemory *mem);

#endif
