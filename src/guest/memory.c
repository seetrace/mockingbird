#include <sys/mman.h>
#include <unistd.h>

#include "memory.h"

int memory_init(GuestMemory *mem, size_t size) {
    long page_size = sysconf(_SC_PAGESIZE);

    if (page_size <= 0 || size % (size_t)page_size != 0)
        return -1;

    mem->data = mmap(
        NULL,
        size,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0
    );

    if (mem->data == MAP_FAILED) {
        mem->data = NULL;
        return -1;
    }

    mem->size = size;

    return 0;
}

void memory_destroy(GuestMemory *mem) {
    if (mem->data)
        munmap(mem->data, mem->size);

    mem->data = NULL;
    mem->size = 0;
}
