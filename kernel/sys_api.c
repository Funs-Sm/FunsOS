/* sys_api.c - FUNSOS 系统级 API 实现
 * 上层应用接口，封装内核服务供第三方 UI 和应用框架调用。
 * 每个函数内部调用已有的内核函数作为简单包装器。
 */

#include "sys_api.h"
#include "kheap.h"
#include "pmm.h"
#include "vfs.h"
#include "process.h"
#include "timer.h"
#include "version.h"
#include "display_server.h"
#include "string.h"
#include "stdlib.h"

/* ---- 窗口管理 API 实现 ---- */

sys_window_t sys_create_window(int x, int y, int w, int h, const char *title) {
    uint32_t win_id = ds_create_window(x, y, (uint32_t)w, (uint32_t)h, title);
    return (sys_window_t)(uintptr_t)win_id;
}

int sys_destroy_window(sys_window_t win) {
    uint32_t win_id = (uint32_t)(uintptr_t)win;
    ds_destroy_window(win_id);
    return 0;
}

int sys_set_window_title(sys_window_t win, const char *title) {
    uint32_t win_id = (uint32_t)(uintptr_t)win;
    ds_set_title(win_id, title);
    return 0;
}

int sys_invalidate_window(sys_window_t win) {
    uint32_t win_id = (uint32_t)(uintptr_t)win;
    ds_invalidate(win_id, 0, 0, 0, 0);
    return 0;
}

/* ---- 图形绘制 API 实现 ---- */

static uint32_t sys_color_to_uint32(sys_color_t c) {
    return ((uint32_t)c.a << 24) | ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b;
}

int sys_draw_rect(sys_window_t win, int x, int y, int w, int h, sys_color_t color) {
    uint32_t win_id = (uint32_t)(uintptr_t)win;
    ds_draw_rect(win_id, (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, sys_color_to_uint32(color));
    return 0;
}

int sys_draw_text(sys_window_t win, int x, int y, const char *text, sys_color_t fg) {
    uint32_t win_id = (uint32_t)(uintptr_t)win;
    ds_draw_text(win_id, (uint32_t)x, (uint32_t)y, text, sys_color_to_uint32(fg), 0);
    return 0;
}

int sys_draw_line(sys_window_t win, int x1, int y1, int x2, int y2, sys_color_t color) {
    /* display_server 没有直接的画线 API，用逐像素实现 */
    uint32_t win_id = (uint32_t)(uintptr_t)win;
    uint32_t c = sys_color_to_uint32(color);
    /* Bresenham 画线算法 */
    int dx = x2 - x1; if (dx < 0) dx = -dx;
    int dy = y2 - y1; if (dy < 0) dy = -dy;
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;
    while (1) {
        ds_draw_pixel(win_id, (uint32_t)x1, (uint32_t)y1, c);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 < dx) { err += dx; y1 += sy; }
    }
    return 0;
}

int sys_fill_window(sys_window_t win, sys_color_t bg) {
    uint32_t win_id = (uint32_t)(uintptr_t)win;
    /* 用矩形填充整个窗口区域 - 使用较大尺寸 */
    ds_draw_rect(win_id, 0, 0, 4096, 4096, sys_color_to_uint32(bg));
    return 0;
}

/* ---- 事件系统 API 实现 ---- */

int sys_poll_event(sys_event_t *event) {
    if (!event) return -1;
    /* 尝试从 display server 获取事件 */
    ds_event_t ds_evt;
    int ret = ds_get_event(0, &ds_evt);  /* window_id=0: 任意窗口 */
    if (ret == 0) {
        event->type = ds_evt.type;
        event->param1 = ds_evt.param1;
        event->param2 = ds_evt.param2;
        event->window = (sys_window_t)(uintptr_t)ds_evt.window_id;
        return 0;
    }
    return -1;  /* 无事件 */
}

int sys_wait_event(sys_event_t *event) {
    if (!event) return -1;
    /* 轮询等待事件 */
    while (1) {
        if (sys_poll_event(event) == 0) return 0;
        /* 让出 CPU */
        asm volatile("hlt");
    }
}

/* ---- 文件系统 API 实现 ---- */

/* 简单的文件描述符表 */
#define SYS_MAX_FDS 16
static file_t *sys_fd_table[SYS_MAX_FDS];
static int sys_fd_initialized = 0;

static void sys_fd_init(void) {
    if (sys_fd_initialized) return;
    for (int i = 0; i < SYS_MAX_FDS; i++) {
        sys_fd_table[i] = 0;
    }
    sys_fd_initialized = 1;
}

static int sys_fd_alloc(void) {
    sys_fd_init();
    for (int i = 0; i < SYS_MAX_FDS; i++) {
        if (sys_fd_table[i] == 0) return i;
    }
    return -1;
}

int sys_file_open(const char *path, uint32_t mode) {
    if (!path) return -1;
    int fd = sys_fd_alloc();
    if (fd < 0) return -1;
    if (vfs_open(path, mode, &sys_fd_table[fd]) != 0 || !sys_fd_table[fd]) {
        sys_fd_table[fd] = 0;
        return -1;
    }
    return fd;
}

int sys_file_read(int fd, void *buf, uint32_t count) {
    if (fd < 0 || fd >= SYS_MAX_FDS || !sys_fd_table[fd] || !buf) return -1;
    return vfs_read(sys_fd_table[fd], buf, count);
}

int sys_file_write(int fd, const void *buf, uint32_t count) {
    if (fd < 0 || fd >= SYS_MAX_FDS || !sys_fd_table[fd] || !buf) return -1;
    return vfs_write(sys_fd_table[fd], buf, count);
}

int sys_file_close(int fd) {
    if (fd < 0 || fd >= SYS_MAX_FDS || !sys_fd_table[fd]) return -1;
    vfs_close(sys_fd_table[fd]);
    sys_fd_table[fd] = 0;
    return 0;
}

/* ---- 进程管理 API 实现 ---- */

uint32_t sys_get_pid(void) {
    /* 返回当前进程 PID - 简化实现 */
    pcb_t *current = process_get_pcb(0);
    return current ? (uint32_t)current->pid : 0;
}

int sys_spawn(const char *path, const char *args) {
    if (!path) return -1;
    /* 尝试通过 process_create 创建进程 - 简化实现 */
    (void)args;
    return -1;  /* 暂不支持完整进程创建 */
}

int sys_terminate(uint32_t pid) {
    pcb_t *proc = process_get_pcb((pid_t)pid);
    if (!proc || proc->state == 0) return -1;
    process_exit(0);
    return 0;
}

/* ---- 内存管理 API 实现 ---- */

void *sys_alloc(uint32_t size) {
    return kmalloc(size);
}

void sys_free(void *ptr) {
    kfree(ptr);
}

/* ---- 系统信息 API 实现 ---- */

uint32_t sys_get_ticks(void) {
    return timer_get_ticks();
}

const char *sys_get_version(void) {
    return OS_STRING;
}

int sys_get_memory_info(uint32_t *total, uint32_t *used) {
    if (!total || !used) return -1;
    *total = pmm_get_total_pages() * 4096;
    *used = pmm_get_used_pages() * 4096;
    return 0;
}
