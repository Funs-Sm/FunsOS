#ifndef PROCESS_H
#define PROCESS_H

#include "kernel_proc.h"

pcb_t *process_create(const char *name, uint8_t *elf_data, uint32_t elf_size);
void process_exit(int status);
pid_t process_wait(int *status);
pid_t process_fork(void);
int process_exec(const char *path, char *const argv[]);
pcb_t *process_get_pcb(pid_t pid);
void init_process(void);

#endif
