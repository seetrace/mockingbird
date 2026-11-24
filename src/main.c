#include <stdio.h>

#include "guest/guest.h"
#include "kvm/kvm.h"

#define GUEST_RAM_SIZE (64 * 1024 * 1024)

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        printf("Usage: %s <firmware.bin> [disk.img]\n", argv[0]);
        return 1;
    }

    Guest guest;
    KVMBackend kvm;

    printf("[HOST] Initializing guest...\n");

    if (guest_init(&guest, GUEST_RAM_SIZE) != 0) {
        fprintf(stderr, "[HOST] Guest initialization failed\n");
        return 1;
    }

    printf("[HOST] Guest initialized\n");

    if (guest_load_firmware(&guest, argv[1]) != 0) {
        guest_destroy(&guest);
        return 1;
    }

    printf("[HOST] Initializing KVM...\n");

    const char *disk_path = argc == 3 ? argv[2] : NULL;

    if (kvm_backend_init(&kvm, &guest.memory, disk_path) != 0) {
        guest_destroy(&guest);
        return 1;
    }

    if (kvm_backend_set_firmware_entry(&kvm) != 0) {
        kvm_backend_destroy(&kvm);
        guest_destroy(&guest);
        return 1;
    }

    printf("[HOST] Starting Mockingbird...\n\n");

    int result = kvm_backend_run(&kvm);

    kvm_backend_destroy(&kvm);
    guest_destroy(&guest);

    return result != 0;
}
