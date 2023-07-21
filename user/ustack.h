#include "kernel/types.h"
#include "kernel/riscv.h"

void* ustack_malloc(uint len);
int ustack_free(void);