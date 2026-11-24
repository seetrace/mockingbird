#include "disk.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ATA_DATA 0x1F0
#define ATA_ERROR 0x1F1
#define ATA_SECTOR_COUNT 0x1F2
#define ATA_LBA_LOW 0x1F3
#define ATA_LBA_MID 0x1F4
#define ATA_LBA_HIGH 0x1F5
#define ATA_DRIVE 0x1F6
#define ATA_STATUS 0x1F7
#define ATA_ALT_STATUS 0x3F6

#define ATA_STATUS_BSY 0x80
#define ATA_STATUS_DRDY 0x40
#define ATA_STATUS_DF 0x20
#define ATA_STATUS_DRQ 0x08
#define ATA_STATUS_ERR 0x01

struct Disk {
    int fd;
    uint64_t capacity;
    uint32_t lba;
    uint16_t sector_count;
    uint8_t data[512];
    uint32_t transfer_offset;
    uint16_t transfer_remaining;
    uint8_t status;
    uint8_t error;
};

static void load_sector(Disk *disk) {
    memset(disk->data, 0, sizeof(disk->data));
    ssize_t read_size = pread(disk->fd, disk->data, sizeof(disk->data),
                              (off_t)((uint64_t)disk->lba * sizeof(disk->data)));
    if (read_size < 0)
        disk->status = ATA_STATUS_DRDY | ATA_STATUS_DF;
}

static void build_identify(Disk *disk) {
    uint16_t identify[256];
    memset(identify, 0, sizeof(identify));

    identify[0] = 0x0040;
    identify[3] = 16;
    identify[6] = 63;
    identify[20] = 0;
    identify[21] = 512;
    identify[23] = 3;
    identify[24] = 128;
    identify[27] = 0x2020;
    identify[28] = 0x4D4F;
    identify[29] = 0x434B;
    identify[30] = 0x494E;
    identify[31] = 0x4742;
    identify[32] = 0x4952;
    identify[33] = 0x4420;
    identify[34] = 0x4449;
    identify[35] = 0x534B;
    identify[47] = 16;
    identify[49] = 0x0200;
    identify[53] = 1;
    identify[54] = 16;
    identify[55] = 63;
    identify[56] = 16;
    identify[57] = 16;
    identify[58] = 63;

    uint32_t sectors = disk->capacity < (1ULL << 28)
                           ? (uint32_t)disk->capacity
                           : (uint32_t)((1ULL << 28) - 1);
    if (sectors == 0)
        sectors = 16;

    identify[60] = (uint16_t)sectors;
    identify[61] = (uint16_t)(sectors >> 16);
    identify[83] = 0;
    identify[85] = 0;

    memcpy(disk->data, identify, sizeof(disk->data));
}

Disk *disk_open(const char *path) {
    Disk *disk = calloc(1, sizeof(*disk));
    if (!disk)
        return NULL;

    disk->fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (disk->fd < 0) {
        perror("[DISK] Failed to open disk image");
        free(disk);
        return NULL;
    }

    struct stat st;
    if (fstat(disk->fd, &st) != 0) {
        perror("[DISK] Failed to inspect disk image");
        close(disk->fd);
        free(disk);
        return NULL;
    }

    disk->capacity = st.st_size >= 0
                         ? (uint64_t)st.st_size / 512ULL
                         : 0;
    disk->status = ATA_STATUS_DRDY;

    printf("[DISK] Opened disk image %s (%llu sectors)\n",
           path, (unsigned long long)disk->capacity);

    return disk;
}

void disk_destroy(Disk *disk) {
    if (!disk)
        return;

    if (disk->fd >= 0)
        close(disk->fd);

    free(disk);
}

int disk_read_range(Disk *disk, uint64_t lba, uint8_t *destination,
                    uint64_t size) {
    if (!disk)
        return -1;

    uint64_t position = lba * 512ULL;
    while (size > 0) {
        size_t chunk = size > (uint64_t)sizeof(disk->data)
                           ? sizeof(disk->data)
                           : (size_t)size;
        ssize_t read_size = pread(disk->fd, destination, chunk,
                                  (off_t)position);
        if (read_size < 0)
            return -1;

        if (read_size == 0)
            memset(destination, 0, chunk);

        position += (uint64_t)read_size;
        destination += read_size;
        size -= (uint64_t)read_size;
    }

    return 0;
}

int disk_io_in(Disk *disk, uint16_t port, uint8_t *data, uint32_t size) {
    if (!disk)
        return -1;

    if (port == ATA_DATA) {
        for (uint32_t i = 0; i < size; i++) {
            if (disk->transfer_offset < 512)
                data[i] = disk->data[disk->transfer_offset++];
            else
                data[i] = 0;

            if (disk->transfer_offset == 512) {
                disk->transfer_offset = 0;

                if (disk->transfer_remaining > 0)
                    disk->transfer_remaining--;

                if (disk->transfer_remaining > 0)
                    load_sector(disk);
                else
                    disk->status = ATA_STATUS_DRDY;
            }
        }

        return 0;
    }

    if (port == ATA_ERROR) {
        for (uint32_t i = 0; i < size; i++)
            data[i] = disk->error;
        return 0;
    }

    if (port == ATA_SECTOR_COUNT) {
        for (uint32_t i = 0; i < size; i++)
            data[i] = (uint8_t)disk->sector_count;
        return 0;
    }

    if (port == ATA_LBA_LOW) {
        for (uint32_t i = 0; i < size; i++)
            data[i] = (uint8_t)disk->lba;
        return 0;
    }

    if (port == ATA_LBA_MID) {
        for (uint32_t i = 0; i < size; i++)
            data[i] = (uint8_t)(disk->lba >> 8);
        return 0;
    }

    if (port == ATA_LBA_HIGH) {
        for (uint32_t i = 0; i < size; i++)
            data[i] = (uint8_t)(disk->lba >> 16);
        return 0;
    }

    if (port == ATA_STATUS || port == ATA_ALT_STATUS) {
        for (uint32_t i = 0; i < size; i++)
            data[i] = disk->status;
        return 0;
    }

    return -1;
}

int disk_io_out(Disk *disk, uint16_t port, const uint8_t *data, uint32_t size) {
    if (!disk)
        return -1;

    if (port == ATA_DATA) {
        for (uint32_t i = 0; i < size; i++) {
            disk->data[disk->transfer_offset++] = data[i];

            if (disk->transfer_offset == 512) {
                disk->transfer_offset = 0;

                if (pwrite(disk->fd, disk->data, sizeof(disk->data),
                           (off_t)((uint64_t)disk->lba * sizeof(disk->data))) < 0)
                    disk->status = ATA_STATUS_DRDY | ATA_STATUS_DF;
                else {
                    disk->capacity =
                        disk->lba + 1 > disk->capacity
                            ? disk->lba + 1
                            : disk->capacity;
                    disk->lba++;
                }

                if (disk->transfer_remaining > 1) {
                    disk->transfer_remaining--;
                    memset(disk->data, 0, sizeof(disk->data));
                } else {
                    disk->transfer_remaining = 0;
                    disk->status = ATA_STATUS_DRDY;
                }
            }
        }

        return 0;
    }

    if (port == ATA_SECTOR_COUNT) {
        disk->sector_count = data[0] == 0 ? 256 : data[0];
        return 0;
    }

    if (port == ATA_LBA_LOW) {
        disk->lba = (disk->lba & 0xFFFFF00) | data[0];
        return 0;
    }

    if (port == ATA_LBA_MID) {
        disk->lba = (disk->lba & 0xFFF00FF) | ((uint32_t)data[0] << 8);
        return 0;
    }

    if (port == ATA_LBA_HIGH) {
        disk->lba = (disk->lba & 0xF00FFFF) | ((uint32_t)data[0] << 16);
        return 0;
    }

    if (port == ATA_DRIVE) {
        disk->lba = (disk->lba & 0x0FFFFFFF) |
                    ((uint32_t)(data[0] & 0x0F) << 24);
        return 0;
    }

    if (port == ATA_STATUS) {
        if (size == 0)
            return 0;

        uint8_t command = data[0];
        disk->status = ATA_STATUS_DRDY;
        disk->error = 0;

        if (command == 0x20) {
            disk->transfer_offset = 0;
            disk->transfer_remaining = disk->sector_count;
            load_sector(disk);
            disk->status = ATA_STATUS_DRDY | ATA_STATUS_DRQ;
        } else if (command == 0x30) {
            disk->transfer_offset = 0;
            disk->transfer_remaining = disk->sector_count;
            memset(disk->data, 0, sizeof(disk->data));
            disk->status = ATA_STATUS_DRDY | ATA_STATUS_DRQ;
        } else if (command == 0xEC) {
            disk->transfer_offset = 0;
            disk->transfer_remaining = 1;
            build_identify(disk);
            disk->status = ATA_STATUS_DRDY | ATA_STATUS_DRQ;
        } else {
            disk->error = 0x04;
            disk->status = ATA_STATUS_DRDY | ATA_STATUS_ERR;
        }

        return 0;
    }

    return -1;
}
