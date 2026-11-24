#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "kvm.h"
#include "../devices/serial.h"

int kvm_backend_init(KVMBackend *kvm, GuestMemory *memory, const char *disk_path) {
    memset(kvm, 0, sizeof(*kvm));

    kvm->system_fd = -1;
    kvm->vm_fd = -1;
    kvm->vcpu_fd = -1;

    kvm->memory = memory;
    if (disk_path && disk_path[0] != '\0') {
        kvm->disk = disk_open(disk_path);
        if (!kvm->disk)
            goto fail;

        if (disk_read_range(kvm->disk, 2048,
                            memory->data + 0x90000,
                            32ULL * 512ULL) == 0 &&
            disk_read_range(kvm->disk, 2176,
                            memory->data + 0x100000,
                            2765ULL * 512ULL) == 0) {
            printf("[DISK] Loaded direct bzImage setup and payload\n");
        }
    }

    // Open KVM
    kvm->system_fd = open("/dev/kvm", O_RDWR | O_CLOEXEC);

    if (kvm->system_fd < 0) {
        perror("[KVM] Failed to open /dev/kvm");
        goto fail;
    }

    // Check API version
    int version = ioctl(kvm->system_fd, KVM_GET_API_VERSION, 0);

    if (version != KVM_API_VERSION) {
        fprintf(stderr, "[KVM] Incompatible API version: %d\n", version);
        goto fail;
    }

    printf("[KVM] API version: %d\n", version);

    // Create VM
    kvm->vm_fd = ioctl(kvm->system_fd, KVM_CREATE_VM, 0);

    if (kvm->vm_fd < 0) {
        perror("[KVM] Failed to create VM");
        goto fail;
    }

    printf("[KVM] VM created\n");

    // Register guest RAM
    struct kvm_userspace_memory_region region = {
        .slot = 0,
        .guest_phys_addr = 0,
        .memory_size = memory->size,
        .userspace_addr = (unsigned long)memory->data,
        .flags = 0
    };

    if (ioctl(kvm->vm_fd, KVM_SET_USER_MEMORY_REGION, &region) < 0) {
        perror("[KVM] Failed to register guest memory");
        goto fail;
    }

    printf("[KVM] Registered %zu MB of guest RAM\n",
           memory->size / (1024 * 1024));

    // Create vCPU
    kvm->vcpu_fd = ioctl(kvm->vm_fd, KVM_CREATE_VCPU, 0);

    if (kvm->vcpu_fd < 0) {
        perror("[KVM] Failed to create vCPU");
        goto fail;
    }

    printf("[KVM] Created vCPU 0\n");

    // Get shared vCPU run structure size
    int mmap_size = ioctl(
        kvm->system_fd,
        KVM_GET_VCPU_MMAP_SIZE,
        0
    );

    if (mmap_size <= 0) {
        perror("[KVM] Failed to get vCPU mmap size");
        goto fail;
    }

    kvm->run_size = (size_t)mmap_size;

    // Map shared run structure
    kvm->run = mmap(
        NULL,
        kvm->run_size,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        kvm->vcpu_fd,
        0
    );

    if (kvm->run == MAP_FAILED) {
        kvm->run = NULL;
        perror("[KVM] Failed to map vCPU run structure");
        goto fail;
    }

    printf("[KVM] vCPU run structure mapped\n");

    return 0;

fail:
    kvm_backend_destroy(kvm);
    return -1;
}

void kvm_backend_destroy(KVMBackend *kvm) {
    if (kvm->disk)
        disk_destroy(kvm->disk);

    if (kvm->run)
        munmap(kvm->run, kvm->run_size);

    if (kvm->vcpu_fd >= 0)
        close(kvm->vcpu_fd);

    if (kvm->vm_fd >= 0)
        close(kvm->vm_fd);

    if (kvm->system_fd >= 0)
        close(kvm->system_fd);

    memset(kvm, 0, sizeof(*kvm));

    kvm->system_fd = -1;
    kvm->vm_fd = -1;
    kvm->vcpu_fd = -1;
}

int kvm_backend_set_firmware_entry(KVMBackend *kvm) {
    struct kvm_sregs sregs;

    if (ioctl(kvm->vcpu_fd, KVM_GET_SREGS, &sregs) < 0) {
        perror("[KVM] KVM_GET_SREGS");
        return -1;
    }

    // Configure real-mode segment state.
    sregs.cr0 &= ~(1ULL << 0);   // Disable protected mode
    sregs.cr0 &= ~(1ULL << 31);  // Disable paging

    sregs.cs.base = 0xF0000;
    sregs.cs.selector = 0xF000;

    sregs.ds.base = 0;
    sregs.ds.selector = 0;

    sregs.es.base = 0;
    sregs.es.selector = 0;

    sregs.ss.base = 0;
    sregs.ss.selector = 0;

    sregs.fs.base = 0;
    sregs.fs.selector = 0;

    sregs.gs.base = 0;
    sregs.gs.selector = 0;

    if (ioctl(kvm->vcpu_fd, KVM_SET_SREGS, &sregs) < 0) {
        perror("[KVM] KVM_SET_SREGS");
        return -1;
    }

    struct kvm_regs regs = {0};

    regs.rip = 0xFFF0;
    regs.rflags = 0x2;
    regs.rsp = 0x7C00;

    if (ioctl(kvm->vcpu_fd, KVM_SET_REGS, &regs) < 0) {
        perror("[KVM] KVM_SET_REGS");
        return -1;
    }

    printf("[KVM] Firmware entry configured: F000:FFF0\n");

    return 0;
}

int kvm_backend_run(KVMBackend *kvm) {
    printf("[KVM] Starting guest execution...\n");
    serial_init();
    int serial_dlab = 0;
    uint8_t serial_divisor_low = 0;
    uint8_t serial_divisor_high = 0;
    int a20_enabled = 0;

    for (;;) {
        if (serial_terminal_closed()) {
            printf("\n[KVM] Serial terminal closed\n");
            serial_destroy();
            return 0;
        }

        if (ioctl(kvm->vcpu_fd, KVM_RUN, 0) < 0) {
            if (errno == EINTR)
                continue;

            perror("[KVM] KVM_RUN");
            serial_destroy();
            return -1;
        }

        if (serial_terminal_closed()) {
            printf("\n[KVM] Serial terminal closed\n");
            serial_destroy();
            return 0;
        }

        switch (kvm->run->exit_reason) {

            case KVM_EXIT_IO: {
                struct kvm_run *run = kvm->run;
                uint8_t *data =
                    (uint8_t *)run + run->io.data_offset;

                if (run->io.port <= 0x0F) {
                    if (run->io.direction == KVM_EXIT_IO_IN &&
                        run->io.size == 1) {
                        for (uint32_t i = 0; i < run->io.count; i++)
                            data[i] = 0;
                    }
                    break;
                }

                if (run->io.port == 0x92) {
                    if (run->io.direction == KVM_EXIT_IO_OUT &&
                        run->io.size == 1) {
                        for (uint32_t i = 0; i < run->io.count; i++)
                            a20_enabled =
                                (data[i] & 0x02) != 0;
                    } else if (run->io.direction == KVM_EXIT_IO_IN &&
                               run->io.size == 1) {
                        for (uint32_t i = 0; i < run->io.count; i++)
                            data[i] = a20_enabled ? 0x02 : 0x00;
                    }
                    break;
                }

                if ((run->io.port >= 0x40 && run->io.port <= 0x43) ||
                    (run->io.port >= 0x20 && run->io.port <= 0x21) ||
                    (run->io.port >= 0xA0 && run->io.port <= 0xA1) ||
                    run->io.port == 0x80 ||
                    run->io.port == 0x380 ||
                    run->io.port == 0x1A ||
                    run->io.port == 0x82 ||
                    run->io.port == 0x61 ||
                    run->io.port == 0x92 ||
                    (run->io.port >= 0x70 && run->io.port <= 0x71) ||
                    (run->io.port >= 0x4D0 && run->io.port <= 0x4D1) ||
                    run->io.port == 0xE2 ||
                    run->io.port == 0xE9 ||
                    run->io.port == 0xF0 ||
                    run->io.port == 0xF1) {
                    if (run->io.direction == KVM_EXIT_IO_IN &&
                        run->io.size == 1) {
                        for (uint32_t i = 0; i < run->io.count; i++)
                            data[i] = 0;
                    }
                    break;
                }

                if (run->io.port == 0xCF8 && run->io.size == 4 &&
                    run->io.direction == KVM_EXIT_IO_IN) {
                    for (uint32_t i = 0; i < run->io.count; i++)
                        *((uint32_t *)(data + i * run->io.size)) = 0;
                    break;
                }

                if (run->io.port == 0xCFB &&
                    run->io.direction == KVM_EXIT_IO_OUT &&
                    run->io.size == 1) {
                    break;
                }

                if (run->io.port >= 0xCFC && run->io.port <= 0xCFF) {
                    if (run->io.direction == KVM_EXIT_IO_IN &&
                        run->io.size <= 4) {
                        for (uint32_t i = 0;
                             i < run->io.size * run->io.count;
                             i++)
                            data[i] = 0xFF;
                    }
                    break;
                }

                if ((run->io.port >= 0x1F0 && run->io.port <= 0x1F7) ||
                    run->io.port == 0x3F6) {
                    uint8_t *data =
                        (uint8_t *)run + run->io.data_offset;

                    if (!kvm->disk) {
                        if (run->io.direction == KVM_EXIT_IO_IN) {
                            for (uint32_t i = 0;
                                 i < run->io.size * run->io.count;
                                 i++)
                                data[i] = 0;
                        }
                    } else if (run->io.direction == KVM_EXIT_IO_IN)
                        disk_io_in(kvm->disk, run->io.port,
                                   data, run->io.size * run->io.count);
                    else
                        disk_io_out(kvm->disk, run->io.port,
                                    data, run->io.size * run->io.count);

                    break;
                }

                if (run->io.port >= 0x3F8 && run->io.port <= 0x3FF) {
                    uint8_t *data =
                        (uint8_t *)run + run->io.data_offset;

                    if (run->io.direction == KVM_EXIT_IO_IN &&
                        run->io.size == 1) {
                        for (uint32_t i = 0; i < run->io.count; i++) {
                            switch (run->io.port) {
                                case 0x3F8:
                                    data[i] = serial_dlab
                                                  ? serial_divisor_low
                                                  : serial_read();
                                    break;
                                case 0x3FA:
                                    data[i] = 0x01;
                                    break;
                                case 0x3FB:
                                    data[i] = 0x03;
                                    break;
                                case 0x3FC:
                                    data[i] = 0x03;
                                    break;
                                case 0x3FD:
                                    data[i] = serial_line_status();
                                    break;
                                case 0x3FE:
                                    data[i] = 0x60;
                                    break;
                                case 0x3FF:
                                    data[i] = 0;
                                    break;
                                case 0x3F9:
                                    data[i] = serial_dlab
                                                  ? serial_divisor_high
                                                  : 0x01;
                                    break;
                                default:
                                    data[i] = 0;
                                    break;
                            }
                        }
                    }

                    if (run->io.direction == KVM_EXIT_IO_OUT &&
                        run->io.size == 1) {
                        for (uint32_t i = 0; i < run->io.count; i++) {
                            if (run->io.port == 0x3F8) {
                                if (serial_dlab)
                                    serial_divisor_low = data[i];
                                else
                                    serial_write(data[i]);
                            } else if (run->io.port == 0x3F9) {
                                if (serial_dlab)
                                    serial_divisor_high = data[i];
                            } else if (run->io.port == 0x3FB) {
                                serial_dlab = (data[i] & 0x80) != 0;
                            } else if (run->io.port == 0x3FA ||
                                       run->io.port == 0x3FC) {
                                continue;
                            }
                        }
                    }

                    break;
                }

                if (run->io.direction == KVM_EXIT_IO_OUT &&
                    run->io.port == 0x3F8 &&
                    run->io.size == 1) {

                    uint8_t *data =
                        (uint8_t *)run + run->io.data_offset;

                    for (uint32_t i = 0; i < run->io.count; i++) {
                        serial_write(data[i]);
                    }
                } else if (run->io.direction == KVM_EXIT_IO_IN &&
                           run->io.port == 0x3F8 &&
                           run->io.size == 1) {
                    uint8_t *data =
                        (uint8_t *)run + run->io.data_offset;

                    for (uint32_t i = 0; i < run->io.count; i++)
                        data[i] = serial_read();
                } else if (run->io.direction == KVM_EXIT_IO_IN &&
                           run->io.port == 0x3FD &&
                           run->io.size == 1) {
                    uint8_t *data =
                        (uint8_t *)run + run->io.data_offset;

                    for (uint32_t i = 0; i < run->io.count; i++)
                        data[i] = serial_line_status();
                } else {
                    for (uint32_t i = 0;
                         i < run->io.size * run->io.count;
                         i++)
                        data[i] = 0;
                }

                break;
            }

            case KVM_EXIT_HLT:
                usleep(1000);
                break;

            case KVM_EXIT_SHUTDOWN:
                printf("\n[KVM] Guest shutdown\n");
                serial_destroy();
                return 0;

            case KVM_EXIT_FAIL_ENTRY:
                fprintf(stderr,
                    "[KVM] VM entry failed: 0x%llX\n",
                    (unsigned long long)kvm->run->fail_entry.hardware_entry_failure_reason);
                serial_destroy();
                return -1;

            case KVM_EXIT_INTERNAL_ERROR:
                fprintf(stderr, "[KVM] Internal KVM error\n");
                serial_destroy();
                return -1;

            default:
                fprintf(stderr,
                    "\n[KVM] Unhandled exit reason: %u\n",
                    kvm->run->exit_reason);
                serial_destroy();
                return -1;
        }
    }
}
