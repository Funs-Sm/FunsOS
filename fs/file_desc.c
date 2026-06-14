#include "file_desc.h"
#include "string.h"
#include "stddef.h"

int32_t fd_init(pcb_t *proc) {
    if (!proc) return -1;

    for (int32_t i = 0; i < MAX_PROCESS_FDS; i++) {
        proc->fd_table[i] = NULL;
    }

    proc->fd_count = 0;
    return 0;
}

int32_t fd_alloc(pcb_t *proc) {
    if (!proc) return -1;

    for (int32_t i = 0; i < MAX_PROCESS_FDS; i++) {
        if (proc->fd_table[i] == NULL) {
            return i;
        }
    }

    return -1;
}

int32_t fd_free(pcb_t *proc, int32_t fd) {
    if (!proc) return -1;
    if (fd < 0 || fd >= MAX_PROCESS_FDS) return -1;

    file_descriptor_t *file = proc->fd_table[fd];
    if (file) {
        if (file->ops && file->ops->close) {
            file->ops->close(file);
        }
        proc->fd_table[fd] = NULL;
        proc->fd_count--;
    }

    return 0;
}

file_descriptor_t *fd_get_file(pcb_t *proc, int32_t fd) {
    if (!proc) return NULL;
    if (fd < 0 || fd >= MAX_PROCESS_FDS) return NULL;

    return proc->fd_table[fd];
}

int32_t fd_install(pcb_t *proc, int32_t fd, file_descriptor_t *file) {
    if (!proc) return -1;
    if (fd < 0 || fd >= MAX_PROCESS_FDS) return -1;
    if (!file) return -1;

    proc->fd_table[fd] = file;
    proc->fd_count++;
    return 0;
}

int32_t fd_dup(pcb_t *proc, int32_t old_fd) {
    if (!proc) return -1;
    if (old_fd < 0 || old_fd >= MAX_PROCESS_FDS) return -1;
    if (!proc->fd_table[old_fd]) return -1;

    int32_t new_fd = fd_alloc(proc);
    if (new_fd < 0) return -1;

    proc->fd_table[new_fd] = proc->fd_table[old_fd];
    proc->fd_table[old_fd]->ref_count++;
    proc->fd_count++;

    return new_fd;
}

int32_t fd_dup2(pcb_t *proc, int32_t old_fd, int32_t new_fd) {
    if (!proc) return -1;
    if (old_fd < 0 || old_fd >= MAX_PROCESS_FDS) return -1;
    if (new_fd < 0 || new_fd >= MAX_PROCESS_FDS) return -1;
    if (!proc->fd_table[old_fd]) return -1;

    if (proc->fd_table[new_fd]) {
        fd_free(proc, new_fd);
    }

    proc->fd_table[new_fd] = proc->fd_table[old_fd];
    proc->fd_table[old_fd]->ref_count++;
    proc->fd_count++;

    return new_fd;
}
