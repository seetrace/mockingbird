#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "kvm.h"

int kvm_backend_init(KVMBackend *kvm, GuestMemory *memory) {
    memset(kvm, 0, sizeof(*kvm));

    kvm->system_fd = -1;
    kvm->vm_fd = -1;
    kvm->vcpu_fd = -1;

    kvm->memory = memory;

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

#include "../devices/serial.h"

int kvm_backend_run(KVMBackend *kvm) {
    printf("[KVM] Starting guest execution...\n");

    for (;;) {
        if (ioctl(kvm->vcpu_fd, KVM_RUN, 0) < 0) {
            if (errno == EINTR)
                continue;

            perror("[KVM] KVM_RUN");
            return -1;
        }

        switch (kvm->run->exit_reason) {

            case KVM_EXIT_IO: {
                struct kvm_run *run = kvm->run;

                if (run->io.direction == KVM_EXIT_IO_OUT &&
                    run->io.port == 0x3F8 &&
                    run->io.size == 1) {

                    uint8_t *data =
                        (uint8_t *)run + run->io.data_offset;

                    for (uint32_t i = 0; i < run->io.count; i++) {
                        serial_write(data[i]);
                    }
                } else {
                    fprintf(stderr,
                        "\n[KVM] Unhandled I/O: port=0x%X\n",
                        run->io.port);
                }

                break;
            }

            case KVM_EXIT_HLT:
                printf("\n[KVM] Guest halted\n");
                return 0;

            case KVM_EXIT_SHUTDOWN:
                printf("\n[KVM] Guest shutdown\n");
                return 0;

            case KVM_EXIT_FAIL_ENTRY:
                fprintf(stderr,
                    "[KVM] VM entry failed: 0x%llX\n",
                    (unsigned long long)kvm->run->fail_entry.hardware_entry_failure_reason);
                return -1;

            case KVM_EXIT_INTERNAL_ERROR:
                fprintf(stderr, "[KVM] Internal KVM error\n");
                return -1;

            default:
                fprintf(stderr,
                    "\n[KVM] Unhandled exit reason: %u\n",
                    kvm->run->exit_reason);
                return -1;
        }
    }
}
