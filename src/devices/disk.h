#ifndef DISK_H
#define DISK_H

#include <stddef.h>
#include <stdint.h>

typedef struct Disk Disk;

Disk *disk_open(const char *path);
void disk_destroy(Disk *disk);
int disk_io_in(Disk *disk, uint16_t port, uint8_t *data, uint32_t size);
int disk_io_out(Disk *disk, uint16_t port, const uint8_t *data, uint32_t size);
int disk_read_range(Disk *disk, uint64_t lba, uint8_t *destination, uint64_t size);

#endif
