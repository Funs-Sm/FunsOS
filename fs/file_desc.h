#ifndef FILE_DESC_H
#define FILE_DESC_H

#include "vfs.h"
#include "kernel_proc.h"

#define MAX_PROCESS_FDS MAX_OPEN_FILES

int32_t fd_init(pcb_t *proc);
int32_t fd_alloc(pcb_t *proc);
int32_t fd_free(pcb_t *proc, int32_t fd);
file_descriptor_t *fd_get_file(pcb_t *proc, int32_t fd);
int32_t fd_install(pcb_t *proc, int32_t fd, file_descriptor_t *file);
int32_t fd_dup(pcb_t *proc, int32_t old_fd);
int32_t fd_dup2(pcb_t *proc, int32_t old_fd, int32_t new_fd);

/* 高级FD操作 */
int32_t fd_close_range(pcb_t *proc, int32_t first, int32_t last);
int32_t fd_count(pcb_t *proc);
int32_t fd_is_valid(pcb_t *proc, int32_t fd);

#endif
