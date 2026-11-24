#include <string.h>
#include "vcpu.h"

int vcpu_init(VCPU *cpu) {
    memset(cpu, 0, sizeof(VCPU));

    cpu->rflags = 0x2;

    return 0;
}
