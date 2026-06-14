#ifndef SYS_API_H
#define SYS_API_H

/* FUNSOS System API - 上层应用接口
 * 类似 Windows NT API 的设计，提供内核服务给用户态程序。
 * 第三方 UI 界面、应用框架等通过此 API 与内核交互。
 */

#include "stdint.h"

/* ---- 窗口管理 API ---- */
#ifndef SYSTEM_SERVICES_H
typedef void *sys_window_t;
#endif
sys_window_t sys_create_window(int x, int y, int w, int h, const char *title);
int sys_destroy_window(sys_window_t win);
int sys_set_window_title(sys_window_t win, const char *title);
int sys_invalidate_window(sys_window_t win);  /* 请求重绘 */

/* ---- 图形绘制 API ---- */
typedef struct { uint8_t r, g, b, a; } sys_color_t;
int sys_draw_rect(sys_window_t win, int x, int y, int w, int h, sys_color_t color);
int sys_draw_text(sys_window_t win, int x, int y, const char *text, sys_color_t fg);
int sys_draw_line(sys_window_t win, int x1, int y1, int x2, int y2, sys_color_t color);
int sys_fill_window(sys_window_t win, sys_color_t bg);

/* ---- 事件系统 API ---- */
#ifndef SYS_EVENT_KEY_PRESS
#define SYS_EVENT_KEY_PRESS   1
#define SYS_EVENT_KEY_RELEASE 2
#define SYS_EVENT_MOUSE_MOVE  3
#define SYS_EVENT_MOUSE_CLICK 4
#define SYS_EVENT_WINDOW_CLOSE 5
#define SYS_EVENT_TIMER       6
#endif

#ifndef SYSTEM_SERVICES_H
typedef struct {
    uint32_t type;
    uint32_t param1, param2;
    sys_window_t window;
} sys_event_t;
#endif

int sys_poll_event(sys_event_t *event);
int sys_wait_event(sys_event_t *event);

/* ---- 文件系统 API ---- */
#ifndef SYSTEM_SERVICES_H
int sys_file_open(const char *path, uint32_t mode);
int sys_file_read(int fd, void *buf, uint32_t count);
int sys_file_write(int fd, const void *buf, uint32_t count);
int sys_file_close(int fd);
#endif

/* ---- 进程管理 API ---- */
uint32_t sys_get_pid(void);
int sys_spawn(const char *path, const char *args);
int sys_terminate(uint32_t pid);

/* ---- 内存管理 API ---- */
void *sys_alloc(uint32_t size);
void sys_free(void *ptr);

/* ---- 系统信息 API ---- */
uint32_t sys_get_ticks(void);
const char *sys_get_version(void);
int sys_get_memory_info(uint32_t *total, uint32_t *used);

#endif
