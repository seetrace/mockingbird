#include <stdio.h>
#include <string.h>

#include "guest.h"

#define FIRMWARE_BASE 0xF0000
#define FIRMWARE_SIZE 0x10000

int guest_init(Guest *guest, size_t ram_size) {
    memset(guest, 0, sizeof(Guest));

    if (memory_init(&guest->memory, ram_size) != 0) {
        return -1;
    }

    if (vcpu_init(&guest->cpu) != 0) {
        memory_destroy(&guest->memory);
        return -1;
    }

    guest->initialized = 1;
    return 0;
}

int guest_load_firmware(Guest *guest, const char *path) {
    if (!guest->initialized)
        return -1;

    FILE *file = fopen(path, "rb");

    if (!file) {
        perror("Firmware");
        return -1;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }

    long size = ftell(file);

    if (size <= 0 || size > FIRMWARE_SIZE) {
        fprintf(stderr, "Invalid firmware size\n");
        fclose(file);
        return -1;
    }

    rewind(file);

    size_t read = fread(
        guest->memory.data + FIRMWARE_BASE,
        1,
        size,
        file
    );

    fclose(file);

    if (read != (size_t)size) {
        fprintf(stderr, "Failed to read firmware\n");
        return -1;
    }

    printf("[GUEST] Firmware loaded at 0x%X (%ld bytes)\n",
           FIRMWARE_BASE, size);

    return 0;
}

void guest_destroy(Guest *guest) {
    if (!guest->initialized)
        return;

    memory_destroy(&guest->memory);

    memset(&guest->cpu, 0, sizeof(VCPU));

    guest->initialized = 0;
}
