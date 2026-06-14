#ifndef SYSCALL_H
#define SYSCALL_H

#include "kernel_types.h"

void init_syscall(void);
int32_t syscall_register(uint32_t num, syscall_func_t func);
void syscall_handler(regs_t *regs);

#endif
