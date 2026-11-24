#ifndef KVM_H
#define KVM_H

#include <stddef.h>
#include <linux/kvm.h>

#include "../guest/memory.h"
#include "../devices/disk.h"

typedef struct {
    int system_fd;
    int vm_fd;
    int vcpu_fd;

    struct kvm_run *run;
    size_t run_size;

    GuestMemory *memory;
    Disk *disk;
} KVMBackend;

int kvm_backend_init(KVMBackend *kvm, GuestMemory *memory, const char *disk_path);
int kvm_backend_set_firmware_entry(KVMBackend *kvm);
int kvm_backend_run(KVMBackend *kvm);
void kvm_backend_destroy(KVMBackend *kvm);

#endif
