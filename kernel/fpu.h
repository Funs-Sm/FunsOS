#ifndef FPU_H
#define FPU_H

#include "stdint.h"
#include "kernel_proc.h"

void fpu_init(void);
void fpu_save(pcb_t *proc);
void fpu_restore(pcb_t *proc);
void fpu_handler(regs_t *regs);

#endif
