#ifndef GUEST_H
#define GUEST_H

#include "memory.h"
#include "../cpu/vcpu.h"

typedef struct {
    GuestMemory memory;
    VCPU cpu;
    int initialized;
} Guest;

int guest_init(Guest *guest, size_t ram_size);
int guest_load_firmware(Guest *guest, const char *path);
void guest_destroy(Guest *guest);

#endif
