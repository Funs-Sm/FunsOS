#ifndef THREAD_H
#define THREAD_H

#include "kernel_proc.h"

pcb_t *thread_create(func_t func, void *arg, const char *name);
void thread_exit(void);
void thread_join(pcb_t *thread);
void thread_yield(void);

#endif
