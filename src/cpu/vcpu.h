#ifndef VCPU_H
#define VCPU_H

#include <stdint.h>

typedef struct {
    uint64_t rflags;
} VCPU;

int vcpu_init(VCPU *cpu);

#endif
