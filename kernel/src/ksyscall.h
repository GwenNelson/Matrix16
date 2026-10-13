#pragma once

#include "8086.h"

#define SYS_WRITE 1

void k_syscall_callback(registers_t *regs);
