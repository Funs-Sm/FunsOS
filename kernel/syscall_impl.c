#include "stdint.h"
#include "stddef.h"
#include "string.h"
#include "syscall_impl.h"
#include "syscall.h"
#include "process.h"
#include "thread.h"
#include "sched.h"
#include "pmm.h"
#include "vmm.h"
#include "kheap.h"
#include "pipe.h"
#include "signal.h"
#include "version.h"
#include "../fs/vfs.h"
#include "../fs/tarfs.h"
#include "../fs/file_desc.h"
#include "../net/socket.h"
#include "fb_console.h"
#include "vga_text.h"
#include "../drivers/vesa.h"
#include "../drivers/rtc.h"
#include "system_services.h"
#include "sys_api.h"
#include "display_server.h"
#include "../audio/hdaudio.h"

/* Syscall numbers - must match syscall.c */
#define SYS_EXIT       1
#define SYS_FORK       2
#define SYS_READ       3
#define SYS_WRITE      4
#define SYS_OPEN       5
#define SYS_CLOSE      6
#define SYS_WAITPID    7
#define SYS_GETPID     8
#define SYS_EXEC       9
#define SYS_SLEEP      10
#define SYS_YIELD      11
#define SYS_PIPE       12
#define SYS_SIGNAL     13
#define SYS_KILL       14
#define SYS_MMAP       15
#define SYS_MUNMAP     16
#define SYS_IOCTL      17
#define SYS_READDIR    18
#define SYS_CHDIR      19
#define SYS_GETCWD     20
#define SYS_SOCKET     21
#define SYS_BIND       22
#define SYS_CONNECT    23
#define SYS_LISTEN     24
#define SYS_ACCEPT     25
#define SYS_SEND       26
#define SYS_RECV       27
#define SYS_SHUTDOWN   28
#define SYS_CLOSESOCK  29
#define SYS_SELECT     30
#define SYS_POLL       31
#define SYS_SENDTO     32
#define SYS_RECVFROM   33
#define SYS_GETSOCKNAME  34
#define SYS_GETPEERNAME  35
#define SYS_SETSOCKOPT   36
#define SYS_GETSOCKOPT   37
#define SYS_SENDFILE     38
#define SYS_LSEEK        39
#define SYS_GETPPID      40
#define SYS_NANOSLEEP    41
#define SYS_MOUNT        42
#define SYS_EXECVE       43
#define SYS_SIGACTION    44
#define SYS_SIGPROCMASK  45
#define SYS_ALARM        46
#define SYS_PAUSE        47
#define SYS_SIGRETURN    48

/* SDK Extended Syscall Numbers - Window Management */
#define SYS_CREATE_WINDOW  100
#define SYS_DESTROY_WINDOW 101
#define SYS_SET_TITLE      102
#define SYS_INVALIDATE     103
#define SYS_SHOW_WINDOW    104
#define SYS_HIDE_WINDOW    105
#define SYS_MOVE_WINDOW    106
#define SYS_RESIZE_WINDOW  107
#define SYS_GET_CONTEXT    108

/* SDK Extended Syscall Numbers - Graphics */
#define SYS_DRAW_RECT      110
#define SYS_DRAW_TEXT      111
#define SYS_DRAW_LINE      112
#define SYS_FILL_WINDOW    113
#define SYS_FILL_RECT      114

/* SDK Extended Syscall Numbers - Events */
#define SYS_POLL_EVENT     120
#define SYS_WAIT_EVENT     121
#define SYS_SET_TIMER      122
#define SYS_CANCEL_TIMER   123

/* SDK Extended Syscall Numbers - Audio */
#define SYS_AUDIO_INIT     130
#define SYS_AUDIO_PLAY     131
#define SYS_AUDIO_STOP     132
#define SYS_AUDIO_SET_VOL  133
#define SYS_AUDIO_GET_VOL  134
#define SYS_AUDIO_PLAY_WAV 135

/* SDK Extended Syscall Numbers - System Info */
#define SYS_GET_TICKS      140
#define SYS_GET_VERSION    141
#define SYS_GET_MEM_INFO   142
#define SYS_GET_SYSINFO    143
#define SYS_GET_TIME       144

/* SDK Extended Syscall Numbers - 3D Rendering */
#define SYS_3D_INIT        150
#define SYS_3D_RENDER      151
#define SYS_3D_CLEAR_DEPTH 152

/* SDK Extended Syscall Numbers - Application */
#define SYS_APP_INIT       160
#define SYS_APP_CLEANUP    161

/* SDK Extended Syscall Numbers - Dialogs & Cursor */
#define SYS_MESSAGE_BOX    170
#define SYS_SET_CURSOR     171

/* SDK Extended Syscall Numbers - Clipboard */
#define SYS_CLIPBOARD_SET     200
#define SYS_CLIPBOARD_GET     201
#define SYS_CLIPBOARD_CLEAR   202
#define SYS_CLIPBOARD_QUERY   203

/* SDK Extended Syscall Numbers - Window Extensions */
#define SYS_WINDOW_STATE    220
#define SYS_WINDOW_EX       221
#define SYS_FOCUS_WINDOW    222
#define SYS_RAISE_WINDOW    223
#define SYS_GET_WIN_RECT    224

extern void vga_print(const char *str);

typedef struct {
    pipe_t *pipe;
    uint32_t flags;
} pipe_fd_data_t;

static int32_t pipe_read_fd(file_t *file, void *buf, uint32_t count) {
    pipe_fd_data_t *data = (pipe_fd_data_t *)file->private_data;
    if (!data || !data->pipe) return -1;
    return pipe_read(data->pipe, buf, count);
}

static int32_t pipe_write_fd(file_t *file, const void *buf, uint32_t count) {
    pipe_fd_data_t *data = (pipe_fd_data_t *)file->private_data;
    if (!data || !data->pipe) return -1;
    return pipe_write(data->pipe, buf, count);
}

static int32_t pipe_close_fd(file_t *file) {
    pipe_fd_data_t *data = (pipe_fd_data_t *)file->private_data;
    if (!data || !data->pipe) {
        kfree(data);
        return -1;
    }
    if (data->flags & 0x01) {
        pipe_close_read(data->pipe);
    }
    if (data->flags & 0x02) {
        pipe_close_write(data->pipe);
    }
    kfree(data);
    return 0;
}

static file_ops_t pipe_read_ops = {
    .open = NULL,
    .read = pipe_read_fd,
    .write = NULL,
    .close = pipe_close_fd,
    .seek = NULL,
    .ioctl = NULL
};

static file_ops_t pipe_write_ops = {
    .open = NULL,
    .read = NULL,
    .write = pipe_write_fd,
    .close = pipe_close_fd,
    .seek = NULL,
    .ioctl = NULL
};

int32_t sys_exit(uint32_t status, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    process_exit((int)status);
    return 0;
}

int32_t sys_fork(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    return process_fork();
}

int32_t sys_read(uint32_t fd, uint32_t buf, uint32_t count, uint32_t arg4, uint32_t arg5) {
    (void)arg4; (void)arg5;
    pcb_t *cur = sched_get_current();
    if (!cur) return -1;

    file_descriptor_t *fdesc = fd_get_file(cur, (int32_t)fd);
    if (!fdesc) return -1;

    file_t *file = (file_t *)fdesc->private_data;
    if (!file) return -1;

    return vfs_read(file, (void *)(uintptr_t)buf, count);
}

int32_t sys_write(uint32_t fd, uint32_t buf, uint32_t count, uint32_t arg4, uint32_t arg5) {
    (void)arg4; (void)arg5;
    if (fd == 1 || fd == 2) {
        const char *str = (const char *)(uintptr_t)buf;
        if (is_vbe_mode()) {
            fb_console_write(str);
        } else {
            vga_print(str);
        }
        return (int32_t)count;
    }

    pcb_t *cur = sched_get_current();
    if (!cur) return -1;

    file_descriptor_t *fdesc = fd_get_file(cur, (int32_t)fd);
    if (!fdesc) return -1;

    file_t *file = (file_t *)fdesc->private_data;
    if (!file) return -1;

    return vfs_write(file, (const void *)(uintptr_t)buf, count);
}

int32_t sys_open(uint32_t path, uint32_t flags, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg3; (void)arg4; (void)arg5;
    pcb_t *cur = sched_get_current();
    if (!cur) return -1;

    const char *path_str = (const char *)(uintptr_t)path;
    if (!path_str) return -1;

    file_t *file = NULL;
    int32_t ret = vfs_open(path_str, flags, &file);
    if (ret < 0 || !file) return -1;

    int32_t fd = fd_alloc(cur);
    if (fd < 0) {
        vfs_close(file);
        return -1;
    }

    file_descriptor_t *fdesc = (file_descriptor_t *)kmalloc(sizeof(file_descriptor_t));
    if (!fdesc) {
        vfs_close(file);
        return -1;
    }

    fdesc->fd = fd;
    fdesc->flags = flags;
    fdesc->ref_count = 1;
    fdesc->private_data = file;
    fdesc->ops = NULL;

    if (fd_install(cur, fd, fdesc) < 0) {
        kfree(fdesc);
        vfs_close(file);
        return -1;
    }

    return fd;
}

int32_t sys_close(uint32_t fd, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    pcb_t *cur = sched_get_current();
    if (!cur) return -1;

    file_descriptor_t *fdesc = fd_get_file(cur, (int32_t)fd);
    if (!fdesc) return -1;

    file_t *file = (file_t *)fdesc->private_data;
    if (file) {
        vfs_close(file);
    }
    fdesc->private_data = NULL;

    int32_t ret = fd_free(cur, (int32_t)fd);
    kfree(fdesc);
    return ret;
}

int32_t sys_waitpid(uint32_t pid, uint32_t status, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg3; (void)arg4; (void)arg5;
    process_wait((int *)(uintptr_t)status);
    return (int32_t)pid;
}

int32_t sys_getpid(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    pcb_t *cur = sched_get_current();
    return cur ? (int32_t)cur->pid : 0;
}

int32_t sys_exec(uint32_t path, uint32_t argv, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg3; (void)arg4; (void)arg5;
    const char *path_str = (const char *)(uintptr_t)path;
    if (!path_str) return -1;
    return process_exec(path_str, (char *const *)(uintptr_t)argv);
}

int32_t sys_sleep(uint32_t seconds, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    sched_sleep(seconds * 1000);
    return 0;
}

int32_t sys_yield(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    sched_yield();
    return 0;
}

int32_t sys_pipe(uint32_t fd_ptr, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    pcb_t *cur = sched_get_current();
    if (!cur) return -1;

    int32_t *fds = (int32_t *)(uintptr_t)fd_ptr;
    if (!fds) return -1;

    pipe_t *p = NULL;
    if (pipe_create(&p) < 0 || !p) {
        return -1;
    }

    int32_t fd_read = fd_alloc(cur);
    if (fd_read < 0) {
        pipe_destroy(p);
        return -1;
    }

    int32_t fd_write = fd_alloc(cur);
    if (fd_write < 0) {
        fd_free(cur, fd_read);
        pipe_destroy(p);
        return -1;
    }

    file_t *read_file = (file_t *)kmalloc(sizeof(file_t));
    file_t *write_file = (file_t *)kmalloc(sizeof(file_t));
    if (!read_file || !write_file) {
        kfree(read_file);
        kfree(write_file);
        fd_free(cur, fd_read);
        fd_free(cur, fd_write);
        pipe_destroy(p);
        return -1;
    }
    memset(read_file, 0, sizeof(file_t));
    memset(write_file, 0, sizeof(file_t));

    pipe_fd_data_t *read_data = (pipe_fd_data_t *)kmalloc(sizeof(pipe_fd_data_t));
    pipe_fd_data_t *write_data = (pipe_fd_data_t *)kmalloc(sizeof(pipe_fd_data_t));
    if (!read_data || !write_data) {
        kfree(read_data);
        kfree(write_data);
        kfree(read_file);
        kfree(write_file);
        fd_free(cur, fd_read);
        fd_free(cur, fd_write);
        pipe_destroy(p);
        return -1;
    }
    read_data->pipe = p;
    read_data->flags = 0x01;
    write_data->pipe = p;
    write_data->flags = 0x02;

    read_file->ops = &pipe_read_ops;
    read_file->private_data = read_data;
    read_file->flags = FILE_MODE_READ;
    read_file->ref_count = 1;

    write_file->ops = &pipe_write_ops;
    write_file->private_data = write_data;
    write_file->flags = FILE_MODE_WRITE;
    write_file->ref_count = 1;

    file_descriptor_t *read_fdesc = (file_descriptor_t *)kmalloc(sizeof(file_descriptor_t));
    file_descriptor_t *write_fdesc = (file_descriptor_t *)kmalloc(sizeof(file_descriptor_t));
    if (!read_fdesc || !write_fdesc) {
        kfree(read_fdesc);
        kfree(write_fdesc);
        kfree(read_data);
        kfree(write_data);
        kfree(read_file);
        kfree(write_file);
        fd_free(cur, fd_read);
        fd_free(cur, fd_write);
        pipe_destroy(p);
        return -1;
    }
    read_fdesc->fd = fd_read;
    read_fdesc->flags = FILE_MODE_READ;
    read_fdesc->ref_count = 1;
    read_fdesc->private_data = read_file;
    read_fdesc->ops = NULL;

    write_fdesc->fd = fd_write;
    write_fdesc->flags = FILE_MODE_WRITE;
    write_fdesc->ref_count = 1;
    write_fdesc->private_data = write_file;
    write_fdesc->ops = NULL;

    if (fd_install(cur, fd_read, read_fdesc) < 0 || fd_install(cur, fd_write, write_fdesc) < 0) {
        kfree(read_fdesc);
        kfree(write_fdesc);
        kfree(read_data);
        kfree(write_data);
        kfree(read_file);
        kfree(write_file);
        fd_free(cur, fd_read);
        fd_free(cur, fd_write);
        pipe_destroy(p);
        return -1;
    }

    fds[0] = fd_read;
    fds[1] = fd_write;
    return 0;
}

int32_t sys_signal(uint32_t sig, uint32_t handler, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg3; (void)arg4; (void)arg5;
    pcb_t *proc = sched_get_current();
    signal_register(proc, (int)sig, (void (*)(int))(uintptr_t)handler);
    return 0;
}

int32_t sys_kill(uint32_t pid, uint32_t sig, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg3; (void)arg4; (void)arg5;
    signal_send((int32_t)pid, (int32_t)sig);
    return 0;
}

int32_t sys_mmap(uint32_t addr, uint32_t length, uint32_t prot, uint32_t flags, uint32_t arg5) {
    (void)flags; (void)arg5;
    uint32_t page_addr = addr & 0xFFFFF000;
    uint32_t num_pages = (length + 4095) / 4096;
    uint32_t map_flags = 0x001;
    if (prot & 0x02) map_flags |= 0x002;
    if (prot & 0x04) map_flags |= 0x004;

    page_directory_t *dir = vmm_get_current_dir();
    for (uint32_t i = 0; i < num_pages; i++) {
        void *phys = pmm_alloc_page();
        if (!phys) return -1;
        vmm_map_page(dir, page_addr + i * 4096, (uint32_t)(uintptr_t)phys, map_flags);
    }
    return (int32_t)page_addr;
}

int32_t sys_munmap(uint32_t addr, uint32_t length, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg3; (void)arg4; (void)arg5;
    uint32_t page_addr = addr & 0xFFFFF000;
    uint32_t num_pages = (length + 4095) / 4096;
    page_directory_t *dir = vmm_get_current_dir();
    for (uint32_t i = 0; i < num_pages; i++) {
        vmm_unmap_page(dir, page_addr + i * 4096);
    }
    return 0;
}

int32_t sys_ioctl(uint32_t fd, uint32_t cmd, uint32_t arg, uint32_t arg4, uint32_t arg5) {
    (void)arg4; (void)arg5;

    if (cmd == 0) {
        int32_t *off_ptr = (int32_t *)(uintptr_t)arg;
        if (fd == (uint32_t)-1 || !off_ptr) return -1;
        pcb_t *cur = sched_get_current();
        if (!cur) return -1;
        file_descriptor_t *fdesc = fd_get_file(cur, (int32_t)fd);
        if (!fdesc) return -1;
        file_t *file = (file_t *)fdesc->private_data;
        if (!file) return -1;
        int32_t offset = *off_ptr;
        return vfs_seek(file, offset, SEEK_SET);
    }

    if (cmd == 1) {
        const char *path = (const char *)(uintptr_t)arg;
        if (!path) return -1;
        return vfs_unlink(path);
    }

    if (cmd == 2) {
        const char *path = (const char *)(uintptr_t)arg;
        if (!path) return -1;
        return vfs_mkdir(path, 0755);
    }

    pcb_t *cur = sched_get_current();
    if (!cur) return -1;
    file_descriptor_t *fdesc = fd_get_file(cur, (int32_t)fd);
    if (!fdesc) return -1;
    file_t *file = (file_t *)fdesc->private_data;
    if (!file || !file->ops || !file->ops->ioctl) return -1;
    return file->ops->ioctl(file, cmd, (void *)(uintptr_t)arg);
}

int32_t sys_readdir(uint32_t fd, uint32_t buf, uint32_t count, uint32_t arg4, uint32_t arg5) {
    (void)arg4; (void)arg5;
    (void)count;
    pcb_t *cur = sched_get_current();
    if (!cur) return -1;

    file_descriptor_t *fdesc = fd_get_file(cur, (int32_t)fd);
    if (!fdesc) return -1;

    file_t *dir = (file_t *)fdesc->private_data;
    if (!dir) return -1;

    vfs_dirent_t *entry = (vfs_dirent_t *)(uintptr_t)buf;
    if (!entry) return -1;

    return vfs_readdir(dir, entry);
}

int32_t sys_chdir(uint32_t path, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    const char *path_str = (const char *)(uintptr_t)path;
    if (!path_str) return -1;
    return vfs_chdir(path_str);
}

int32_t sys_getcwd(uint32_t buf, uint32_t size, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg3; (void)arg4; (void)arg5;
    char *dst = (char *)(uintptr_t)buf;
    if (!dst || size == 0) return -1;
    const char *cwd = vfs_getcwd();
    if (!cwd) return -1;
    size_t len = strlen(cwd);
    if (len >= size) len = size - 1;
    memcpy(dst, cwd, len);
    dst[len] = '\0';
    return (int32_t)len;
}

int32_t sys_lseek(uint32_t fd, uint32_t offset, uint32_t whence, uint32_t arg4, uint32_t arg5) {
    (void)arg4; (void)arg5;
    pcb_t *cur = sched_get_current();
    if (!cur) return -1;

    file_descriptor_t *fdesc = fd_get_file(cur, (int32_t)fd);
    if (!fdesc) return -1;

    file_t *file = (file_t *)fdesc->private_data;
    if (!file) return -1;

    return vfs_seek(file, (int32_t)offset, (int32_t)whence);
}

int32_t sys_getppid(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    pcb_t *cur = sched_get_current();
    return cur ? (int32_t)cur->parent_pid : 0;
}

int32_t sys_nanosleep(uint32_t ms, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    sched_sleep(ms);
    return 0;
}

int32_t sys_mount_call(uint32_t path, uint32_t fs_type_str, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg3; (void)arg4; (void)arg5;
    const char *path_str = (const char *)(uintptr_t)path;
    const char *type_str = (const char *)(uintptr_t)fs_type_str;
    if (!path_str || !type_str) return -1;

    uint32_t fs_type = 0;
    if (strcmp(type_str, "ramfs") == 0) fs_type = FS_TYPE_RAMFS;
    else if (strcmp(type_str, "fat32") == 0) fs_type = FS_TYPE_FAT32;
    else if (strcmp(type_str, "ext2") == 0) fs_type = FS_TYPE_EXT2;
    else if (strcmp(type_str, "ext4") == 0) fs_type = FS_TYPE_EXT4;
    else if (strcmp(type_str, "devfs") == 0) fs_type = FS_TYPE_DEVFS;
    else if (strcmp(type_str, "tarfs") == 0) fs_type = FS_TYPE_TARFS;
    else if (strcmp(type_str, "proc") == 0) fs_type = FS_TYPE_RAMFS;
    else if (strcmp(type_str, "sysfs") == 0) fs_type = FS_TYPE_RAMFS;
    else return -1;

    return vfs_mount(path_str, fs_type, NULL);
}

int32_t sys_execve_call(uint32_t path, uint32_t argv, uint32_t envp, uint32_t arg4, uint32_t arg5) {
    (void)envp; (void)arg4; (void)arg5;
    const char *path_str = (const char *)(uintptr_t)path;
    if (!path_str) return -1;
    return process_exec(path_str, (char *const *)(uintptr_t)argv);
}

int32_t sys_sigaction(uint32_t sig, uint32_t act_ptr, uint32_t oact_ptr, uint32_t arg4, uint32_t arg5) {
    (void)arg4; (void)arg5;
    pcb_t *cur = sched_get_current();
    if (!cur) return -1;

    const struct sigaction *act = (const struct sigaction *)(uintptr_t)act_ptr;
    struct sigaction *oact = (struct sigaction *)(uintptr_t)oact_ptr;

    return signal_sigaction(cur, (int)sig, act, oact);
}

int32_t sys_sigprocmask(uint32_t how, uint32_t set_ptr, uint32_t oset_ptr, uint32_t arg4, uint32_t arg5) {
    (void)arg4; (void)arg5;
    pcb_t *cur = sched_get_current();
    if (!cur) return -1;

    const sigset_t *set = (const sigset_t *)(uintptr_t)set_ptr;
    sigset_t *oset = (sigset_t *)(uintptr_t)oset_ptr;

    return signal_sigprocmask(cur, (int)how, set, oset);
}

int32_t sys_alarm(uint32_t seconds, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    pcb_t *cur = sched_get_current();
    if (!cur) return -1;

    return signal_alarm(cur, seconds);
}

int32_t sys_pause(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    pcb_t *cur = sched_get_current();
    if (!cur) return -1;

    return signal_pause(cur);
}

int32_t sys_sigreturn(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    pcb_t *cur = sched_get_current();
    if (!cur) return -1;

    signal_sigreturn(cur);
    return 0;
}

/* Socket system calls */

int32_t sys_socket_call(uint32_t domain, uint32_t type, uint32_t protocol, uint32_t arg4, uint32_t arg5) {
    (void)arg4; (void)arg5;
    return sys_socket((int)domain, (int)type, (int)protocol);
}

int32_t sys_bind_call(uint32_t fd, uint32_t addr_ptr, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg3; (void)arg4; (void)arg5;
    return sys_bind((int)fd, (const sockaddr_in_t *)(uintptr_t)addr_ptr);
}

int32_t sys_connect_call(uint32_t fd, uint32_t addr_ptr, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg3; (void)arg4; (void)arg5;
    return sys_connect((int)fd, (const sockaddr_in_t *)(uintptr_t)addr_ptr);
}

int32_t sys_listen_call(uint32_t fd, uint32_t backlog, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg3; (void)arg4; (void)arg5;
    return sys_listen((int)fd, (int)backlog);
}

int32_t sys_accept_call(uint32_t fd, uint32_t addr_ptr, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg3; (void)arg4; (void)arg5;
    socket_t *s = sys_accept((int)fd, (sockaddr_in_t *)(uintptr_t)addr_ptr);
    return s ? (int32_t)(intptr_t)s : -1;
}

int32_t sys_send_call(uint32_t fd, uint32_t buf, uint32_t len, uint32_t flags, uint32_t arg5) {
    (void)arg5;
    return sys_send((int)fd, (const void *)(uintptr_t)buf, len, (int)flags);
}

int32_t sys_recv_call(uint32_t fd, uint32_t buf, uint32_t len, uint32_t flags, uint32_t arg5) {
    (void)arg5;
    return sys_recv((int)fd, (void *)(uintptr_t)buf, len, (int)flags);
}

int32_t sys_shutdown_call(uint32_t fd, uint32_t how, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg3; (void)arg4; (void)arg5;
    return sys_shutdown((int)fd, (int)how);
}

int32_t sys_closesock_call(uint32_t fd, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    return sys_closesocket((int)fd);
}

int32_t sys_select_call(uint32_t nfds, uint32_t rfds, uint32_t wfds, uint32_t efds, uint32_t timeout) {
    return sys_select((int)nfds, (void *)(uintptr_t)rfds, (void *)(uintptr_t)wfds, (void *)(uintptr_t)efds, timeout);
}

int32_t sys_poll_call(uint32_t fds, uint32_t nfds, uint32_t timeout, uint32_t arg4, uint32_t arg5) {
    (void)arg4; (void)arg5;
    return sys_poll((void *)(uintptr_t)fds, nfds, (int32_t)timeout);
}

int32_t sys_sendto_call(uint32_t fd, uint32_t buf, uint32_t len, uint32_t flags, uint32_t addr_ptr) {
    return sys_sendto((int)fd, (const void *)(uintptr_t)buf, len, (int)flags,
                      (const sockaddr_in_t *)(uintptr_t)addr_ptr);
}

int32_t sys_recvfrom_call(uint32_t fd, uint32_t buf, uint32_t len, uint32_t flags, uint32_t addr_ptr) {
    return sys_recvfrom((int)fd, (void *)(uintptr_t)buf, len, (int)flags,
                        (sockaddr_in_t *)(uintptr_t)addr_ptr);
}

int32_t sys_getsockname_call(uint32_t fd, uint32_t addr_ptr, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg3; (void)arg4; (void)arg5;
    return sys_getsockname((int)fd, (sockaddr_in_t *)(uintptr_t)addr_ptr);
}

int32_t sys_getpeername_call(uint32_t fd, uint32_t addr_ptr, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg3; (void)arg4; (void)arg5;
    return sys_getpeername((int)fd, (sockaddr_in_t *)(uintptr_t)addr_ptr);
}

int32_t sys_setsockopt_call(uint32_t fd, uint32_t level, uint32_t optname,
                            uint32_t optval, uint32_t optlen) {
    return sys_setsockopt((int)fd, (int)level, (int)optname,
                          (const void *)(uintptr_t)optval, optlen);
}

int32_t sys_getsockopt_call(uint32_t fd, uint32_t level, uint32_t optname,
                            uint32_t optval, uint32_t optlen) {
    return sys_getsockopt((int)fd, (int)level, (int)optname,
                          (void *)(uintptr_t)optval, (uint32_t *)(uintptr_t)optlen);
}

int32_t sys_sendfile_call(uint32_t out_fd, uint32_t in_fd,
                          uint32_t offset_ptr, uint32_t count, uint32_t arg5) {
    (void)arg5;
    return sys_sendfile((int)out_fd, (int)in_fd,
                        (uint64_t *)(uintptr_t)offset_ptr, count);
}

/* ===============================================
 * SDK Extended Syscall Implementations
 * =============================================== */

/* Window Management Syscalls */
int32_t sys_create_window_call(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t title) {
    const char *title_str = (const char *)(uintptr_t)title;
    uint32_t win_id = ds_create_window((int32_t)x, (int32_t)y, w, h, title_str);
    return (int32_t)win_id;
}

int32_t sys_destroy_window_call(uint32_t win, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    ds_destroy_window(win);
    return 0;
}

int32_t sys_set_title_call(uint32_t win, uint32_t title, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg3; (void)arg4; (void)arg5;
    const char *title_str = (const char *)(uintptr_t)title;
    ds_set_title(win, title_str);
    return 0;
}

int32_t sys_invalidate_call(uint32_t win, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    (void)x; (void)y; (void)w; (void)h;
    ds_invalidate(win, 0, 0, 4096, 4096);
    return 0;
}

int32_t sys_show_window_call(uint32_t win, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    ds_show_window(win);
    return 0;
}

int32_t sys_hide_window_call(uint32_t win, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    ds_hide_window(win);
    return 0;
}

int32_t sys_move_window_call(uint32_t win, uint32_t x, uint32_t y, uint32_t arg4, uint32_t arg5) {
    (void)arg4; (void)arg5;
    ds_move_window(win, (int32_t)x, (int32_t)y);
    return 0;
}

int32_t sys_resize_window_call(uint32_t win, uint32_t w, uint32_t h, uint32_t arg4, uint32_t arg5) {
    (void)arg4; (void)arg5;
    ds_resize_window(win, w, h);
    return 0;
}

int32_t sys_get_context_call(uint32_t win, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    (void)win;
    extern uint32_t *vbe_get_framebuffer(void);
    return (int32_t)(uintptr_t)vbe_get_framebuffer();
}

/* Graphics Syscalls */
int32_t sys_draw_rect_call(uint32_t win, uint32_t x, uint32_t y, uint32_t w, uint32_t color) {
    uint32_t h = (color >> 16) & 0xFFFF;
    uint32_t c = color & 0x0000FFFF;
    ds_draw_rect(win, x, y, w, h, c);
    return 0;
}

int32_t sys_draw_text_call(uint32_t win, uint32_t x, uint32_t y, uint32_t text, uint32_t fg) {
    const char *text_str = (const char *)(uintptr_t)text;
    ds_draw_text(win, x, y, text_str, fg, 0);
    return 0;
}

int32_t sys_draw_line_call(uint32_t win, uint32_t x1, uint32_t y1, uint32_t x2, uint32_t color) {
    uint32_t y2 = (color >> 16) & 0xFFFF;
    uint32_t c = color & 0x0000FFFF;
    int dx = (int)x2 - (int)x1; if (dx < 0) dx = -dx;
    int dy = (int)y2 - (int)y1; if (dy < 0) dy = -dy;
    int sx = ((int)x1 < (int)x2) ? 1 : -1;
    int sy = ((int)y1 < (int)y2) ? 1 : -1;
    int err = dx - dy;
    int cx = (int)x1, cy = (int)y1;
    while (1) {
        ds_draw_pixel(win, (uint32_t)cx, (uint32_t)cy, c);
        if (cx == (int)x2 && cy == (int)y2) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; cx += sx; }
        if (e2 < dx) { err += dx; cy += sy; }
    }
    return 0;
}

int32_t sys_fill_window_call(uint32_t win, uint32_t color, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg3; (void)arg4; (void)arg5;
    ds_draw_rect(win, 0, 0, 4096, 4096, color);
    return 0;
}

int32_t sys_fill_rect_call(uint32_t win, uint32_t x, uint32_t y, uint32_t w, uint32_t color) {
    uint32_t h = (color >> 16) & 0xFFFF;
    uint32_t c = color & 0x0000FFFF;
    ds_draw_rect(win, x, y, w, h, c);
    return 0;
}

/* Event Syscalls */
int32_t sys_poll_event_call(uint32_t event_ptr, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    sys_event_t *event = (sys_event_t *)(uintptr_t)event_ptr;
    return sys_event_poll(event);
}

int32_t sys_wait_event_call(uint32_t event_ptr, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    sys_event_t *event = (sys_event_t *)(uintptr_t)event_ptr;
    return sys_event_wait(event, 0);
}

int32_t sys_set_timer_call(uint32_t interval_ms, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    return (int32_t)sys_timer_create(interval_ms, NULL, NULL);
}

int32_t sys_cancel_timer_call(uint32_t timer_id, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    sys_timer_stop(timer_id);
    return sys_timer_destroy(timer_id);
}

/* Audio Syscalls */
int32_t sys_audio_init_call(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    hdaudio_init();
    return 0;
}

int32_t sys_audio_play_call(uint32_t freq, uint32_t duration_ms, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg3; (void)arg4; (void)arg5;
    extern void pit_beep(uint32_t freq, uint32_t duration_ms);
    if (freq == 0) freq = 440;
    if (duration_ms == 0) duration_ms = 100;
    pit_beep(freq, duration_ms);
    return 0;
}

int32_t sys_audio_stop_call(uint32_t dev_id, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)dev_id; (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    hdaudio_stop();
    return 0;
}

int32_t sys_audio_set_vol_call(uint32_t vol, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    uint8_t hda_vol = (uint8_t)(vol & 0x7F);
    return hdaudio_set_volume(hda_vol, hda_vol);
}

int32_t sys_audio_get_vol_call(uint32_t vol_ptr, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    uint8_t left = 0, right = 0;
    hdaudio_get_volume(&left, &right);
    if (vol_ptr) {
        uint8_t *out = (uint8_t *)(uintptr_t)vol_ptr;
        out[0] = left;
    }
    return left;
}

int32_t sys_audio_play_wav_call(uint32_t path, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    const char *path_str = (const char *)(uintptr_t)path;
    if (!path_str) return -1;
    file_t *file = NULL;
    if (vfs_open(path_str, FILE_MODE_READ, &file) < 0 || !file) return -1;

    uint8_t wav_header[44];
    if (vfs_read(file, wav_header, 44) != 44) {
        vfs_close(file);
        return -1;
    }

    uint32_t data_size = *(uint32_t *)&wav_header[40];
    uint16_t channels = *(uint16_t *)&wav_header[22];
    uint32_t sample_rate = *(uint32_t *)&wav_header[24];
    uint16_t bits_per_sample = *(uint16_t *)&wav_header[34];

    if (bits_per_sample != 16) {
        vfs_close(file);
        return -1;
    }

    uint8_t *audio_buf = (uint8_t *)kmalloc(data_size);
    if (!audio_buf) {
        vfs_close(file);
        return -1;
    }

    int32_t read_bytes = vfs_read(file, audio_buf, data_size);
    vfs_close(file);

    if (read_bytes <= 0) {
        kfree(audio_buf);
        return -1;
    }

    int32_t ret = hdaudio_play(audio_buf, (uint32_t)read_bytes, sample_rate, channels);
    kfree(audio_buf);
    return ret;
}

/* System Info Syscalls */
int32_t sys_get_ticks_call(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    extern uint64_t timer_get_ticks(void);
    return (int32_t)timer_get_ticks();
}

int32_t sys_get_version_call(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    return (int32_t)(uintptr_t)KERNEL_STRING;
}

int32_t sys_get_mem_info_call(uint32_t total_ptr, uint32_t used_ptr, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg3; (void)arg4; (void)arg5;
    uint32_t *total = (uint32_t *)(uintptr_t)total_ptr;
    uint32_t *used = (uint32_t *)(uintptr_t)used_ptr;

    uint32_t total_pages = pmm_get_total_pages();
    uint32_t used_pages = pmm_get_used_pages();
    uint32_t page_size = 4096;

    if (total) *total = total_pages * page_size;
    if (used) *used = used_pages * page_size;
    return 0;
}

int32_t sys_get_sysinfo_call(uint32_t info_ptr, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    sys_info_t *info = (sys_info_t *)(uintptr_t)info_ptr;
    return sys_info_get(info);
}

int32_t sys_get_time_call(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    return (int32_t)rtc_get_timestamp();
}

/* 3D Rendering Syscalls */
int32_t sys_3d_init_call(uint32_t ctx, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)ctx; (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    return 0;
}

int32_t sys_3d_render_call(uint32_t vertices, uint32_t count, uint32_t mvp, uint32_t mode, uint32_t arg5) {
    (void)vertices; (void)count; (void)mvp; (void)mode; (void)arg5;
    return 0;
}

int32_t sys_3d_clear_depth_call(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    return 0;
}

/* Application Syscalls */
int32_t sys_app_init_call(uint32_t config, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)config; (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    return 0;
}

int32_t sys_app_cleanup_call(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    return 0;
}

/* Dialog & Cursor Syscalls */
int32_t sys_message_box_call(uint32_t parent, uint32_t type, uint32_t message, uint32_t buf, uint32_t bufsize) {
    (void)parent; (void)type; (void)message; (void)buf; (void)bufsize;
    return 0;
}

int32_t sys_set_cursor_call(uint32_t cursor_type, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)cursor_type; (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    return 0;
}

/* Clipboard Syscalls */
int32_t sys_clipboard_set_call(uint32_t text, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    const char *text_str = (const char *)(uintptr_t)text;
    if (!text_str) return -1;
    return sys_clipboard_set_text(text_str);
}

int32_t sys_clipboard_get_call(uint32_t buf, uint32_t bufsize, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg3; (void)arg4; (void)arg5;
    char *dst = (char *)(uintptr_t)buf;
    if (!dst || bufsize == 0) return -1;
    return sys_clipboard_get_text(dst, bufsize);
}

int32_t sys_clipboard_clear_call(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    return sys_clipboard_clear();
}

int32_t sys_clipboard_query_call(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    return sys_clipboard_is_empty() ? 0 : 1;
}

/* Extended Window Management Syscalls */
int32_t sys_window_state_call(uint32_t win, uint32_t state, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)win; (void)state; (void)arg3; (void)arg4; (void)arg5;
    return 0;
}

int32_t sys_window_ex_call(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t title) {
    const char *title_str = (const char *)(uintptr_t)title;
    uint32_t win_id = ds_create_window((int32_t)x, (int32_t)y, w, h, title_str);
    return (int32_t)win_id;
}

int32_t sys_focus_window_call(uint32_t win, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    (void)win;
    return 0;
}

int32_t sys_raise_window_call(uint32_t win, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)win; (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    return 0;
}

int32_t sys_get_win_rect_call(uint32_t win, uint32_t rect_ptr, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg3; (void)arg4; (void)arg5;
    (void)win;
    typedef struct { int32_t x, y; uint32_t w, h; } rect_t;
    rect_t *rect = (rect_t *)(uintptr_t)rect_ptr;
    if (!rect) return -1;
    rect->x = 0;
    rect->y = 0;
    rect->w = 0;
    rect->h = 0;
    return 0;
}

void init_syscall_impl(void) {
    syscall_register(SYS_EXIT, sys_exit);
    syscall_register(SYS_FORK, sys_fork);
    syscall_register(SYS_READ, sys_read);
    syscall_register(SYS_WRITE, sys_write);
    syscall_register(SYS_OPEN, sys_open);
    syscall_register(SYS_CLOSE, sys_close);
    syscall_register(SYS_WAITPID, sys_waitpid);
    syscall_register(SYS_GETPID, sys_getpid);
    syscall_register(SYS_EXEC, sys_exec);
    syscall_register(SYS_SLEEP, sys_sleep);
    syscall_register(SYS_YIELD, sys_yield);
    syscall_register(SYS_PIPE, sys_pipe);
    syscall_register(SYS_SIGNAL, sys_signal);
    syscall_register(SYS_KILL, sys_kill);
    syscall_register(SYS_MMAP, sys_mmap);
    syscall_register(SYS_MUNMAP, sys_munmap);
    syscall_register(SYS_IOCTL, sys_ioctl);
    syscall_register(SYS_READDIR, sys_readdir);
    syscall_register(SYS_CHDIR, sys_chdir);
    syscall_register(SYS_GETCWD, sys_getcwd);
    syscall_register(SYS_SOCKET,    sys_socket_call);
    syscall_register(SYS_BIND,      sys_bind_call);
    syscall_register(SYS_CONNECT,   sys_connect_call);
    syscall_register(SYS_LISTEN,    sys_listen_call);
    syscall_register(SYS_ACCEPT,    sys_accept_call);
    syscall_register(SYS_SEND,      sys_send_call);
    syscall_register(SYS_RECV,      sys_recv_call);
    syscall_register(SYS_SHUTDOWN,  sys_shutdown_call);
    syscall_register(SYS_CLOSESOCK, sys_closesock_call);
    syscall_register(SYS_SELECT,    sys_select_call);
    syscall_register(SYS_POLL,      sys_poll_call);
    syscall_register(SYS_SENDTO,        sys_sendto_call);
    syscall_register(SYS_RECVFROM,      sys_recvfrom_call);
    syscall_register(SYS_GETSOCKNAME,   sys_getsockname_call);
    syscall_register(SYS_GETPEERNAME,   sys_getpeername_call);
    syscall_register(SYS_SETSOCKOPT,    sys_setsockopt_call);
    syscall_register(SYS_GETSOCKOPT,    sys_getsockopt_call);
    syscall_register(SYS_SENDFILE,      sys_sendfile_call);
    syscall_register(SYS_LSEEK,        sys_lseek);
    syscall_register(SYS_GETPPID,      sys_getppid);
    syscall_register(SYS_NANOSLEEP,    sys_nanosleep);
    syscall_register(SYS_MOUNT,        sys_mount_call);
    syscall_register(SYS_EXECVE,       sys_execve_call);
    syscall_register(SYS_SIGACTION,    sys_sigaction);
    syscall_register(SYS_SIGPROCMASK,  sys_sigprocmask);
    syscall_register(SYS_ALARM,        sys_alarm);
    syscall_register(SYS_PAUSE,        sys_pause);
    syscall_register(SYS_SIGRETURN,    sys_sigreturn);
    
    /* Register SDK extended syscalls - Window Management */
    syscall_register(SYS_CREATE_WINDOW,  sys_create_window_call);
    syscall_register(SYS_DESTROY_WINDOW, sys_destroy_window_call);
    syscall_register(SYS_SET_TITLE,      sys_set_title_call);
    syscall_register(SYS_INVALIDATE,     sys_invalidate_call);
    syscall_register(SYS_SHOW_WINDOW,    sys_show_window_call);
    syscall_register(SYS_HIDE_WINDOW,    sys_hide_window_call);
    syscall_register(SYS_MOVE_WINDOW,    sys_move_window_call);
    syscall_register(SYS_RESIZE_WINDOW,  sys_resize_window_call);
    syscall_register(SYS_GET_CONTEXT,    sys_get_context_call);
    
    /* Register SDK extended syscalls - Graphics */
    syscall_register(SYS_DRAW_RECT,      sys_draw_rect_call);
    syscall_register(SYS_DRAW_TEXT,      sys_draw_text_call);
    syscall_register(SYS_DRAW_LINE,      sys_draw_line_call);
    syscall_register(SYS_FILL_WINDOW,    sys_fill_window_call);
    syscall_register(SYS_FILL_RECT,      sys_fill_rect_call);
    
    /* Register SDK extended syscalls - Events */
    syscall_register(SYS_POLL_EVENT,     sys_poll_event_call);
    syscall_register(SYS_WAIT_EVENT,     sys_wait_event_call);
    syscall_register(SYS_SET_TIMER,      sys_set_timer_call);
    syscall_register(SYS_CANCEL_TIMER,   sys_cancel_timer_call);
    
    /* Register SDK extended syscalls - Audio */
    syscall_register(SYS_AUDIO_INIT,     sys_audio_init_call);
    syscall_register(SYS_AUDIO_PLAY,     sys_audio_play_call);
    syscall_register(SYS_AUDIO_STOP,     sys_audio_stop_call);
    syscall_register(SYS_AUDIO_SET_VOL,  sys_audio_set_vol_call);
    syscall_register(SYS_AUDIO_GET_VOL,  sys_audio_get_vol_call);
    syscall_register(SYS_AUDIO_PLAY_WAV, sys_audio_play_wav_call);
    
    /* Register SDK extended syscalls - System Info */
    syscall_register(SYS_GET_TICKS,      sys_get_ticks_call);
    syscall_register(SYS_GET_VERSION,    sys_get_version_call);
    syscall_register(SYS_GET_MEM_INFO,   sys_get_mem_info_call);
    syscall_register(SYS_GET_SYSINFO,    sys_get_sysinfo_call);
    syscall_register(SYS_GET_TIME,       sys_get_time_call);
    
    /* Register SDK extended syscalls - 3D Rendering */
    syscall_register(SYS_3D_INIT,        sys_3d_init_call);
    syscall_register(SYS_3D_RENDER,      sys_3d_render_call);
    syscall_register(SYS_3D_CLEAR_DEPTH, sys_3d_clear_depth_call);
    
    /* Register SDK extended syscalls - Application */
    syscall_register(SYS_APP_INIT,       sys_app_init_call);
    syscall_register(SYS_APP_CLEANUP,    sys_app_cleanup_call);
    
    /* Register SDK extended syscalls - Dialogs & Cursor */
    syscall_register(SYS_MESSAGE_BOX,    sys_message_box_call);
    syscall_register(SYS_SET_CURSOR,     sys_set_cursor_call);
    
    /* Register SDK extended syscalls - Clipboard */
    syscall_register(SYS_CLIPBOARD_SET,   sys_clipboard_set_call);
    syscall_register(SYS_CLIPBOARD_GET,   sys_clipboard_get_call);
    syscall_register(SYS_CLIPBOARD_CLEAR, sys_clipboard_clear_call);
    syscall_register(SYS_CLIPBOARD_QUERY, sys_clipboard_query_call);
    
    /* Register SDK extended syscalls - Window Extensions */
    syscall_register(SYS_WINDOW_STATE,   sys_window_state_call);
    syscall_register(SYS_WINDOW_EX,      sys_window_ex_call);
    syscall_register(SYS_FOCUS_WINDOW,   sys_focus_window_call);
    syscall_register(SYS_RAISE_WINDOW,   sys_raise_window_call);
    syscall_register(SYS_GET_WIN_RECT,   sys_get_win_rect_call);
}
