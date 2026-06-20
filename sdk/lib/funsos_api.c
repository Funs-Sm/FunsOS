/*
 * FUNSOS SDK 运行时库实现
 * ========================
 * 本文件封装了内核 sys_api 系统调用，为上层应用提供友好的 C 语言 API。
 * 所有 funsos_* 函数最终通过 int $0x80 系统调用与内核通信。
 *
 * 编译: gcc -m32 -ffreestanding -c funsos_api.c -o funsos_api.o
 */

#include "funsos.h"
#include "stddef.h"

/* ---- 系统调用号（与 apps/user_syscall.h 保持一致） ---- */
#define SYS_EXIT      1
#define SYS_FORK      2
#define SYS_READ      3
#define SYS_WRITE     4
#define SYS_OPEN      5
#define SYS_CLOSE     6
#define SYS_WAITPID   7
#define SYS_GETPID    8
#define SYS_EXEC      9
#define SYS_SLEEP     10
#define SYS_YIELD     11
#define SYS_PIPE      12
#define SYS_SIGNAL    13
#define SYS_KILL      14
#define SYS_MMAP      15
#define SYS_MUNMAP    16
#define SYS_IOCTL     17
#define SYS_READDIR   18
#define SYS_CHDIR     19
#define SYS_GETCWD    20
#define SYS_SOCKET    21
#define SYS_BIND      22
#define SYS_CONNECT   23
#define SYS_LISTEN    24
#define SYS_ACCEPT    25
#define SYS_SEND      26
#define SYS_RECV      27
#define SYS_SHUTDOWN  28
#define SYS_CLOSESOCK 29
#define SYS_SELECT    30
#define SYS_POLL      31
#define SYS_SENDTO    32
#define SYS_RECVFROM  33
#define SYS_GETSOCKNAME  34
#define SYS_GETPEERNAME  35
#define SYS_SETSOCKOPT   36
#define SYS_GETSOCKOPT   37
#define SYS_SENDFILE     38
#define SYS_EXECVE       39

/* ---- 窗口管理扩展系统调用号 ---- */
#define SYS_CREATE_WINDOW  100
#define SYS_DESTROY_WINDOW 101
#define SYS_SET_TITLE      102
#define SYS_INVALIDATE     103
#define SYS_SHOW_WINDOW    104
#define SYS_HIDE_WINDOW    105
#define SYS_MOVE_WINDOW    106
#define SYS_RESIZE_WINDOW  107
#define SYS_GET_CONTEXT    108

/* ---- 图形绘制扩展系统调用号 ---- */
#define SYS_DRAW_RECT      110
#define SYS_DRAW_TEXT      111
#define SYS_DRAW_LINE      112
#define SYS_FILL_WINDOW    113

/* ---- 事件系统扩展系统调用号 ---- */
#define SYS_POLL_EVENT     120
#define SYS_WAIT_EVENT     121
#define SYS_SET_TIMER      122
#define SYS_CANCEL_TIMER   123

/* ---- 音频扩展系统调用号 ---- */
#define SYS_AUDIO_INIT     130
#define SYS_AUDIO_PLAY     131
#define SYS_AUDIO_STOP     132
#define SYS_AUDIO_SET_VOL  133
#define SYS_AUDIO_GET_VOL  134
#define SYS_AUDIO_PLAY_WAV 135

/* ---- 系统信息扩展系统调用号 ---- */
#define SYS_GET_TICKS      140
#define SYS_GET_VERSION    141
#define SYS_GET_MEM_INFO   142
#define SYS_GET_SYSINFO    143
#define SYS_GET_TIME       144

/* ---- 3D 渲染扩展系统调用号 ---- */
#define SYS_3D_INIT        150
#define SYS_3D_RENDER      151
#define SYS_3D_CLEAR_DEPTH 152

/* ---- 底层系统调用包装 ---- */

static inline int syscall0(int num)
{
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num)
        : "memory"
    );
    return ret;
}

static inline int syscall1(int num, int a1)
{
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1)
        : "memory"
    );
    return ret;
}

static inline int syscall2(int num, int a1, int a2)
{
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2)
        : "memory"
    );
    return ret;
}

static inline int syscall3(int num, int a1, int a2, int a3)
{
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2), "d"(a3)
        : "memory"
    );
    return ret;
}

static inline int syscall4(int num, int a1, int a2, int a3, int a4)
{
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4)
        : "memory"
    );
    return ret;
}

static inline int syscall5(int num, int a1, int a2, int a3, int a4, int a5)
{
    int ret;
    __asm__ volatile (
        "push %%ebp\n"
        "mov %7, %%ebp\n"
        "int $0x80\n"
        "pop %%ebp\n"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5), "m"(a5)
        : "memory"
    );
    return ret;
}

/* ================================================================
 *  窗口管理 API 实现
 * ================================================================ */

funsos_window_t funsos_create_window(int x, int y, int w, int h, const char *title)
{
    funsos_window_t win = {0};
    win.handle = (uint32_t)syscall5(SYS_CREATE_WINDOW,
                                     x, y, w, h, (int)title);
    return win;
}

int funsos_destroy_window(funsos_window_t win)
{
    return syscall1(SYS_DESTROY_WINDOW, (int)win.handle);
}

int funsos_set_window_title(funsos_window_t win, const char *title)
{
    return syscall2(SYS_SET_TITLE, (int)win.handle, (int)title);
}

int funsos_invalidate_window(funsos_window_t win)
{
    return syscall1(SYS_INVALIDATE, (int)win.handle);
}

void funsos_show_window(funsos_window_t win)
{
    syscall1(SYS_SHOW_WINDOW, (int)win.handle);
}

void funsos_hide_window(funsos_window_t win)
{
    syscall1(SYS_HIDE_WINDOW, (int)win.handle);
}

void funsos_move_window(funsos_window_t win, int x, int y)
{
    syscall3(SYS_MOVE_WINDOW, (int)win.handle, x, y);
}

void funsos_resize_window(funsos_window_t win, int w, int h)
{
    syscall3(SYS_RESIZE_WINDOW, (int)win.handle, w, h);
}

void *funsos_get_window_context(uint32_t win_handle)
{
    return (void *)syscall1(SYS_GET_CONTEXT, (int)win_handle);
}

/* ================================================================
 *  图形绘制 API 实现
 * ================================================================ */

int funsos_draw_rect(uint32_t win_handle, int x, int y, int w, int h, funsos_color_t color)
{
    return syscall5(SYS_DRAW_RECT, (int)win_handle, x, y, w, (int)color);
}

int funsos_draw_text(uint32_t win_handle, int x, int y, const char *text, funsos_color_t fg)
{
    return syscall4(SYS_DRAW_TEXT, (int)win_handle, x, y, (int)text);
}

int funsos_draw_line(uint32_t win_handle, int x1, int y1, int x2, int y2, funsos_color_t color)
{
    return syscall5(SYS_DRAW_LINE, (int)win_handle, x1, y1, x2, (int)color);
}

int funsos_fill_window(uint32_t win_handle, funsos_color_t bg)
{
    return syscall2(SYS_FILL_WINDOW, (int)win_handle, (int)bg);
}

/* 以下 2D 绘图函数直接操作图形上下文缓冲区（用户态实现） */

void funsos_draw_circle(funsos_gfx_context_t *ctx, int cx, int cy, int r, funsos_color_t color)
{
    /* Bresenham 画圆算法 */
    if (ctx == NULL) return;
    int x = 0, y = r;
    int d = 3 - 2 * r;

    while (x <= y) {
        /* 绘制 8 个对称点 */
        if (cx + x < (int)ctx->width && cy + y < (int)ctx->height)
            ctx->buffer[(cy + y) * ctx->width + (cx + x)] = color;
        if (cx - x >= 0 && cy + y < (int)ctx->height)
            ctx->buffer[(cy + y) * ctx->width + (cx - x)] = color;
        if (cx + x < (int)ctx->width && cy - y >= 0)
            ctx->buffer[(cy - y) * ctx->width + (cx + x)] = color;
        if (cx - x >= 0 && cy - y >= 0)
            ctx->buffer[(cy - y) * ctx->width + (cx - x)] = color;
        if (cx + y < (int)ctx->width && cy + x < (int)ctx->height)
            ctx->buffer[(cy + x) * ctx->width + (cx + y)] = color;
        if (cx - y >= 0 && cy + x < (int)ctx->height)
            ctx->buffer[(cy + x) * ctx->width + (cx - y)] = color;
        if (cx + y < (int)ctx->width && cy - x >= 0)
            ctx->buffer[(cy - x) * ctx->width + (cx + y)] = color;
        if (cx - y >= 0 && cy - x >= 0)
            ctx->buffer[(cy - x) * ctx->width + (cx - y)] = color;

        if (d < 0) {
            d += 4 * x + 6;
        } else {
            d += 4 * (x - y) + 10;
            y--;
        }
        x++;
    }
}

void funsos_fill_circle(funsos_gfx_context_t *ctx, int cx, int cy, int r, funsos_color_t color)
{
    /* 填充圆 - 逐行填充 */
    if (ctx == NULL) return;
    for (int y = -r; y <= r; y++) {
        int width = 0;
        int r2 = r * r;
        int y2 = y * y;
        /* 计算该行的水平宽度 */
        while (width * width + y2 <= r2) width++;
        width--;
        /* 填充该行 */
        for (int x = -width; x <= width; x++) {
            int px = cx + x;
            int py = cy + y;
            if (px >= 0 && px < (int)ctx->width &&
                py >= 0 && py < (int)ctx->height) {
                ctx->buffer[py * ctx->width + px] = color;
            }
        }
    }
}

void funsos_draw_rounded_rect(funsos_gfx_context_t *ctx, funsos_rect_t rect, int radius, funsos_color_t color)
{
    /* 圆角矩形 = 4 条直线 + 4 个圆弧 */
    if (ctx == NULL) return;

    /* 4 条直线 */
    funsos_draw_line(0, rect.x + radius, rect.y,
                     rect.x + rect.w - radius, rect.y, color);
    funsos_draw_line(0, rect.x + radius, rect.y + rect.h,
                     rect.x + rect.w - radius, rect.y + rect.h, color);
    funsos_draw_line(0, rect.x, rect.y + radius,
                     rect.x, rect.y + rect.h - radius, color);
    funsos_draw_line(0, rect.x + rect.w, rect.y + radius,
                     rect.x + rect.w, rect.y + rect.h - radius, color);

    /* 4 个圆角 */
    funsos_draw_circle(ctx, rect.x + radius, rect.y + radius, radius, color);
    funsos_draw_circle(ctx, rect.x + rect.w - radius, rect.y + radius, radius, color);
    funsos_draw_circle(ctx, rect.x + radius, rect.y + rect.h - radius, radius, color);
    funsos_draw_circle(ctx, rect.x + rect.w - radius, rect.y + rect.h - radius, radius, color);
}

void funsos_fill_rounded_rect(funsos_gfx_context_t *ctx, funsos_rect_t rect, int radius, funsos_color_t color)
{
    if (ctx == NULL) return;

    /* 填充中间矩形 */
    for (int y = rect.y + radius; y < rect.y + rect.h - radius; y++) {
        for (int x = rect.x; x < rect.x + rect.w; x++) {
            if (x >= 0 && x < (int)ctx->width && y >= 0 && y < (int)ctx->height) {
                ctx->buffer[y * ctx->width + x] = color;
            }
        }
    }

    /* 填充上下矩形 */
    for (int y = rect.y; y < rect.y + radius; y++) {
        for (int x = rect.x + radius; x < rect.x + rect.w - radius; x++) {
            if (x >= 0 && x < (int)ctx->width && y >= 0 && y < (int)ctx->height) {
                ctx->buffer[y * ctx->width + x] = color;
            }
        }
    }
    for (int y = rect.y + rect.h - radius; y < rect.y + rect.h; y++) {
        for (int x = rect.x + radius; x < rect.x + rect.w - radius; x++) {
            if (x >= 0 && x < (int)ctx->width && y >= 0 && y < (int)ctx->height) {
                ctx->buffer[y * ctx->width + x] = color;
            }
        }
    }

    /* 填充 4 个圆角 */
    funsos_fill_circle(ctx, rect.x + radius, rect.y + radius, radius, color);
    funsos_fill_circle(ctx, rect.x + rect.w - radius, rect.y + radius, radius, color);
    funsos_fill_circle(ctx, rect.x + radius, rect.y + rect.h - radius, radius, color);
    funsos_fill_circle(ctx, rect.x + rect.w - radius, rect.y + rect.h - radius, radius, color);
}

void funsos_blend_pixel(funsos_gfx_context_t *ctx, int x, int y, funsos_color_t color, uint8_t alpha)
{
    if (ctx == NULL) return;
    if (x < 0 || x >= (int)ctx->width || y < 0 || y >= (int)ctx->height) return;

    uint32_t *pixel = &ctx->buffer[y * ctx->width + x];
    uint32_t bg = *pixel;

    /* 提取背景色 RGB 分量 */
    uint8_t bg_r = (bg >> 16) & 0xFF;
    uint8_t bg_g = (bg >> 8) & 0xFF;
    uint8_t bg_b = bg & 0xFF;

    /* 提取前景色 RGB 分量 */
    uint8_t fg_r = (color >> 16) & 0xFF;
    uint8_t fg_g = (color >> 8) & 0xFF;
    uint8_t fg_b = color & 0xFF;

    /* Alpha 混合 */
    uint8_t inv_alpha = 255 - alpha;
    uint8_t r = (fg_r * alpha + bg_r * inv_alpha) / 255;
    uint8_t g = (fg_g * alpha + bg_g * inv_alpha) / 255;
    uint8_t b = (fg_b * alpha + bg_b * inv_alpha) / 255;

    *pixel = (r << 16) | (g << 8) | b;
}

void funsos_set_clip(funsos_gfx_context_t *ctx, funsos_rect_t clip)
{
    if (ctx == NULL) return;
    ctx->clip = clip;
}

void funsos_reset_clip(funsos_gfx_context_t *ctx)
{
    if (ctx == NULL) return;
    funsos_rect_t full = {0, 0, (int32_t)ctx->width, (int32_t)ctx->height};
    ctx->clip = full;
}

void funsos_blit(funsos_gfx_context_t *dst, int dx, int dy,
                 funsos_gfx_context_t *src, funsos_rect_t src_rect)
{
    if (dst == NULL || src == NULL) return;

    for (int y = 0; y < src_rect.h; y++) {
        for (int x = 0; x < src_rect.w; x++) {
            int sx = src_rect.x + x;
            int sy = src_rect.y + y;
            int tx = dx + x;
            int ty = dy + y;

            if (sx >= 0 && sx < (int)src->width &&
                sy >= 0 && sy < (int)src->height &&
                tx >= 0 && tx < (int)dst->width &&
                ty >= 0 && ty < (int)dst->height) {
                dst->buffer[ty * dst->width + tx] =
                    src->buffer[sy * src->width + sx];
            }
        }
    }
}

/* ================================================================
 *  3D 图形 API 实现
 * ================================================================ */

void funsos_3d_init(funsos_gfx_context_t *ctx)
{
    syscall1(SYS_3D_INIT, (int)ctx);
}

funsos_mat4_t funsos_3d_perspective(float fov, float aspect, float near_val, float far_val)
{
    funsos_mat4_t result;
    /* 透视投影矩阵计算 */
    float f = 1.0f;
    /* 简化: tan(fov/2) 的倒数 - 实际由内核计算 */
    result.m[0]  = f / aspect; result.m[1]  = 0; result.m[2]  = 0;                           result.m[3]  = 0;
    result.m[4]  = 0;          result.m[5]  = f; result.m[6]  = 0;                           result.m[7]  = 0;
    result.m[8]  = 0;          result.m[9]  = 0; result.m[10] = (far_val + near_val) / (near_val - far_val); result.m[11] = (2 * far_val * near_val) / (near_val - far_val);
    result.m[12] = 0;          result.m[13] = 0; result.m[14] = -1;                          result.m[15] = 0;
    return result;
}

funsos_mat4_t funsos_3d_lookat(funsos_vec3_t eye, funsos_vec3_t center, funsos_vec3_t up)
{
    funsos_mat4_t result;
    /* 观察矩阵计算 - 简化版本 */
    float fx = center.x - eye.x;
    float fy = center.y - eye.y;
    float fz = center.z - eye.z;

    /* 归一化前方向 */
    float len = fx * fx + fy * fy + fz * fz;
    if (len > 0) { len = 1.0f / len; fx *= len; fy *= len; fz *= len; }

    /* 右方向 = forward x up */
    float rx = fy * up.z - fz * up.y;
    float ry = fz * up.x - fx * up.z;
    float rz = fx * up.y - fy * up.x;
    len = rx * rx + ry * ry + rz * rz;
    if (len > 0) { len = 1.0f / len; rx *= len; ry *= len; rz *= len; }

    /* 真正的上方向 = right x forward */
    float ux = ry * fz - rz * fy;
    float uy = rz * fx - rx * fz;
    float uz = rx * fy - ry * fx;

    result.m[0]  = rx;   result.m[1]  = ry;   result.m[2]  = rz;   result.m[3]  = 0;
    result.m[4]  = ux;   result.m[5]  = uy;   result.m[6]  = uz;   result.m[7]  = 0;
    result.m[8]  = -fx;  result.m[9]  = -fy;  result.m[10] = -fz;  result.m[11] = 0;
    result.m[12] = -(rx * eye.x + ux * eye.y + (-fx) * eye.z);
    result.m[13] = -(ry * eye.x + uy * eye.y + (-fy) * eye.z);
    result.m[14] = -(rz * eye.x + uz * eye.y + (-fz) * eye.z);
    result.m[15] = 1;
    return result;
}

funsos_mat4_t funsos_3d_mul_matrix(funsos_mat4_t a, funsos_mat4_t b)
{
    funsos_mat4_t result;
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            result.m[col * 4 + row] =
                a.m[0 * 4 + row] * b.m[col * 4 + 0] +
                a.m[1 * 4 + row] * b.m[col * 4 + 1] +
                a.m[2 * 4 + row] * b.m[col * 4 + 2] +
                a.m[3 * 4 + row] * b.m[col * 4 + 3];
        }
    }
    return result;
}

funsos_mat4_t funsos_3d_rotate_y(float angle)
{
    funsos_mat4_t result;
    float c = 1, s = 0;  /* 简化: 实际应使用 cos/sin */
    result.m[0]  = c;  result.m[1]  = 0; result.m[2]  = -s; result.m[3]  = 0;
    result.m[4]  = 0;  result.m[5]  = 1; result.m[6]  = 0;  result.m[7]  = 0;
    result.m[8]  = s;  result.m[9]  = 0; result.m[10] = c;   result.m[11] = 0;
    result.m[12] = 0;  result.m[13] = 0; result.m[14] = 0;  result.m[15] = 1;
    return result;
}

funsos_mat4_t funsos_3d_rotate_x(float angle)
{
    funsos_mat4_t result;
    float c = 1, s = 0;
    result.m[0]  = 1; result.m[1]  = 0;  result.m[2]  = 0; result.m[3]  = 0;
    result.m[4]  = 0; result.m[5]  = c;   result.m[6]  = s; result.m[7]  = 0;
    result.m[8]  = 0; result.m[9]  = -s;  result.m[10] = c; result.m[11] = 0;
    result.m[12] = 0; result.m[13] = 0;   result.m[14] = 0; result.m[15] = 1;
    return result;
}

funsos_mat4_t funsos_3d_translate(float x, float y, float z)
{
    funsos_mat4_t result;
    result.m[0]  = 1; result.m[1]  = 0; result.m[2]  = 0; result.m[3]  = x;
    result.m[4]  = 0; result.m[5]  = 1; result.m[6]  = 0; result.m[7]  = y;
    result.m[8]  = 0; result.m[9]  = 0; result.m[10] = 1; result.m[11] = z;
    result.m[12] = 0; result.m[13] = 0; result.m[14] = 0; result.m[15] = 1;
    return result;
}

void funsos_3d_render(const funsos_vertex3d_t *vertices, uint32_t count,
                      funsos_mat4_t mvp, int mode)
{
    syscall4(SYS_3D_RENDER, (int)vertices, (int)count, (int)&mvp, mode);
}

void funsos_3d_clear_depth(void)
{
    syscall0(SYS_3D_CLEAR_DEPTH);
}

/* ================================================================
 *  事件系统 API 实现
 * ================================================================ */

int funsos_poll_event(funsos_event_t *event)
{
    return syscall1(SYS_POLL_EVENT, (int)event);
}

int funsos_wait_event(funsos_event_t *event)
{
    return syscall1(SYS_WAIT_EVENT, (int)event);
}

int funsos_set_timer(uint32_t interval_ms)
{
    return syscall1(SYS_SET_TIMER, (int)interval_ms);
}

int funsos_cancel_timer(int timer_id)
{
    return syscall1(SYS_CANCEL_TIMER, timer_id);
}

/* ================================================================
 *  文件系统 API 实现
 * ================================================================ */

int funsos_file_open(const char *path, uint32_t mode)
{
    return syscall2(SYS_OPEN, (int)path, (int)mode);
}

int funsos_file_read(int fd, void *buf, uint32_t count)
{
    return syscall3(SYS_READ, fd, (int)buf, (int)count);
}

int funsos_file_write(int fd, const void *buf, uint32_t count)
{
    return syscall3(SYS_WRITE, fd, (int)buf, (int)count);
}

int funsos_file_close(int fd)
{
    return syscall1(SYS_CLOSE, fd);
}

int funsos_file_seek(int fd, int offset, int whence)
{
    /* seek 通过 ioctl 实现 */
    return syscall3(SYS_IOCTL, fd, 0, (int)&offset);
}

int funsos_file_remove(const char *path)
{
    /* 删除文件通过 ioctl 实现 */
    return syscall3(SYS_IOCTL, -1, 1, (int)path);
}

int funsos_file_mkdir(const char *path)
{
    return syscall3(SYS_IOCTL, -1, 2, (int)path);
}

int funsos_file_chdir(const char *path)
{
    return syscall1(SYS_CHDIR, (int)path);
}

int funsos_file_getcwd(char *buf, uint32_t size)
{
    return syscall2(SYS_GETCWD, (int)buf, (int)size);
}

int funsos_file_readdir(int fd, void *buf, uint32_t count)
{
    return syscall3(SYS_READDIR, fd, (int)buf, (int)count);
}

/* ================================================================
 *  进程管理 API 实现
 * ================================================================ */

uint32_t funsos_get_pid(void)
{
    return (uint32_t)syscall0(SYS_GETPID);
}

int funsos_fork(void)
{
    return syscall0(SYS_FORK);
}

int funsos_exec(const char *path, char *const argv[])
{
    return syscall2(SYS_EXEC, (int)path, (int)argv);
}

int funsos_execve(const char *path, char *const argv[], char *const envp[])
{
    return syscall3(SYS_EXECVE, (int)path, (int)argv, (int)envp);
}

int funsos_wait(int *status)
{
    return syscall2(SYS_WAITPID, -1, (int)status);
}

int funsos_waitpid(int pid, int *status)
{
    return syscall2(SYS_WAITPID, pid, (int)status);
}

int funsos_spawn(const char *path, const char *args)
{
    return syscall2(SYS_EXEC, (int)path, (int)args);
}

int funsos_terminate(uint32_t pid)
{
    return syscall2(SYS_KILL, (int)pid, FUNSOS_SIGKILL);
}

void funsos_exit(int status)
{
    syscall1(SYS_EXIT, status);
    __builtin_unreachable();
}

int funsos_signal(int sig, funsos_sighandler_t handler)
{
    return syscall2(SYS_SIGNAL, sig, (int)handler);
}

int funsos_kill(int pid, int sig)
{
    return syscall2(SYS_KILL, pid, sig);
}

int funsos_pipe(int fd[2])
{
    return syscall1(SYS_PIPE, (int)fd);
}

int funsos_sleep(uint32_t seconds)
{
    return syscall1(SYS_SLEEP, (int)seconds);
}

void funsos_yield(void)
{
    syscall0(SYS_YIELD);
}

/* ================================================================
 *  内存管理 API 实现
 *  注: funsos_alloc/funsos_free 在 funsos_glue.c 中实现,
 *  直接使用内核的 kmalloc/kfree
 * ================================================================ */

void *funsos_mmap(void *addr, uint32_t len, int prot, int flags)
{
    return (void *)syscall4(SYS_MMAP, (int)addr, (int)len, prot, flags);
}

int funsos_munmap(void *addr, uint32_t len)
{
    return syscall2(SYS_MUNMAP, (int)addr, (int)len);
}

/* ================================================================
 *  网络通信 API 实现
 * ================================================================ */

int funsos_socket(int domain, int type, int protocol)
{
    return syscall3(SYS_SOCKET, domain, type, protocol);
}

int funsos_bind(int fd, const funsos_sockaddr_in_t *addr)
{
    return syscall2(SYS_BIND, fd, (int)addr);
}

int funsos_connect(int fd, const funsos_sockaddr_in_t *addr)
{
    return syscall2(SYS_CONNECT, fd, (int)addr);
}

int funsos_listen(int fd, int backlog)
{
    return syscall2(SYS_LISTEN, fd, backlog);
}

int funsos_accept(int fd, funsos_sockaddr_in_t *addr)
{
    return syscall2(SYS_ACCEPT, fd, (int)addr);
}

int funsos_send(int fd, const void *buf, uint32_t len, int flags)
{
    return syscall4(SYS_SEND, fd, (int)buf, (int)len, flags);
}

int funsos_recv(int fd, void *buf, uint32_t len, int flags)
{
    return syscall4(SYS_RECV, fd, (int)buf, (int)len, flags);
}

int funsos_sendto(int fd, const void *buf, uint32_t len, int flags,
                  const funsos_sockaddr_in_t *addr)
{
    return syscall5(SYS_SENDTO, fd, (int)buf, (int)len, flags, (int)addr);
}

int funsos_recvfrom(int fd, void *buf, uint32_t len, int flags,
                    funsos_sockaddr_in_t *addr)
{
    return syscall5(SYS_RECVFROM, fd, (int)buf, (int)len, flags, (int)addr);
}

int funsos_shutdown(int fd, int how)
{
    return syscall2(SYS_SHUTDOWN, fd, how);
}

int funsos_closesocket(int fd)
{
    return syscall1(SYS_CLOSESOCK, fd);
}

int funsos_setsockopt(int fd, int level, int optname,
                      const void *optval, uint32_t optlen)
{
    return syscall5(SYS_SETSOCKOPT, fd, level, optname, (int)optval, (int)optlen);
}

int funsos_getsockopt(int fd, int level, int optname,
                      void *optval, uint32_t *optlen)
{
    return syscall5(SYS_GETSOCKOPT, fd, level, optname, (int)optval, (int)optlen);
}

int funsos_select(int nfds, void *readfds, void *writefds,
                  void *exceptfds, uint32_t timeout_ms)
{
    return syscall5(SYS_SELECT, nfds, (int)readfds, (int)writefds,
                    (int)exceptfds, (int)timeout_ms);
}

int funsos_poll(void *fds, uint32_t nfds, int timeout_ms)
{
    return syscall3(SYS_POLL, (int)fds, (int)nfds, timeout_ms);
}

funsos_ipv4_t funsos_inet_addr(const char *str)
{
    funsos_ipv4_t result;
    result.addr = 0;

    /* 解析 "a.b.c.d" 格式的 IP 地址 */
    int parts[4] = {0, 0, 0, 0};
    int part = 0;
    int digit = 0;

    for (int i = 0; str[i] && part < 4; i++) {
        if (str[i] == '.') {
            parts[part++] = digit;
            digit = 0;
        } else if (str[i] >= '0' && str[i] <= '9') {
            digit = digit * 10 + (str[i] - '0');
        }
    }
    if (part < 4) parts[part] = digit;

    /* 组合成网络字节序的 32 位地址 */
    result.addr = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];

    return result;
}

uint16_t funsos_htons(uint16_t port)
{
    /* 小端转大端（网络字节序） */
    return ((port & 0xFF) << 8) | ((port >> 8) & 0xFF);
}

/* ================================================================
 *  音频 API 实现
 * ================================================================ */

int funsos_audio_init(void)
{
    return syscall0(SYS_AUDIO_INIT);
}

int funsos_audio_get_device(uint32_t index, audio_device_t *info)
{
    /* 通过 ioctl 获取设备信息 */
    return syscall3(SYS_IOCTL, -1, 10, (int)info);
}

int funsos_audio_play(uint32_t dev_id, const void *data, uint32_t size)
{
    return syscall3(SYS_AUDIO_PLAY, (int)dev_id, (int)data, (int)size);
}

int funsos_audio_stop(uint32_t dev_id)
{
    return syscall1(SYS_AUDIO_STOP, (int)dev_id);
}

int funsos_audio_set_volume(uint32_t dev_id, uint8_t left, uint8_t right)
{
    uint8_t vol[2] = {left, right};
    return syscall3(SYS_AUDIO_SET_VOL, (int)dev_id, (int)vol, 2);
}

int funsos_audio_get_volume(uint32_t dev_id, uint8_t *left, uint8_t *right)
{
    uint8_t vol[2] = {0, 0};
    int ret = syscall3(SYS_AUDIO_GET_VOL, (int)dev_id, (int)vol, 2);
    if (ret == 0) {
        *left = vol[0];
        *right = vol[1];
    }
    return ret;
}

int funsos_audio_play_wav(const char *path)
{
    return syscall1(SYS_AUDIO_PLAY_WAV, (int)path);
}

/* ================================================================
 *  系统信息 API 实现
 * ================================================================ */

uint32_t funsos_get_ticks(void)
{
    return (uint32_t)syscall0(SYS_GET_TICKS);
}

const char *funsos_get_version(void)
{
    return (const char *)syscall0(SYS_GET_VERSION);
}

int funsos_get_memory_info(uint32_t *total, uint32_t *used)
{
    return syscall2(SYS_GET_MEM_INFO, (int)total, (int)used);
}

int funsos_get_sysinfo(funsos_sysinfo_t *info)
{
    return syscall1(SYS_GET_SYSINFO, (int)info);
}

uint32_t funsos_get_time(void)
{
    return (uint32_t)syscall0(SYS_GET_TIME);
}

/* ================================================================
 *  Extended System & Utility API Implementation
 * ================================================================ */

/* ---- Extended syscall numbers ---- */
#define SYS_APP_INIT          160
#define SYS_APP_CLEANUP       161
#define SYS_MESSAGE_BOX       170
#define SYS_SET_CURSOR        171
#define SYS_REGISTER_HOTKEY   180
#define SYS_UNREGISTER_HOTKEY 181
#define SYS_CREATE_TIMER_EXT  190
#define SYS_RESET_TIMER       191
#define SYS_GET_TIMER_REMAIN  192
#define SYS_CLIPBOARD_SET     200
#define SYS_CLIPBOARD_GET     201
#define SYS_CLIPBOARD_CLEAR   202
#define SYS_CLIPBOARD_QUERY   203
#define SYS_INSTALL_FILTER    210
#define SYS_REMOVE_FILTER     211
#define SYS_BYPASS_FILTERS    212
#define SYS_WINDOW_STATE      220
#define SYS_WINDOW_EX         221
#define SYS_FOCUS_WINDOW      222
#define SYS_RAISE_WINDOW      223
#define SYS_GET_WIN_RECT      224

/*
 * funs_get_system_info() - Query comprehensive system information
 * Wraps the existing funsos_get_sysinfo() with a convenience layer.
 */
int funs_get_system_info(funsos_sysinfo_t *info)
{
    if (info == NULL) return FUNSOS_ERR_INVAL;
    return funsos_get_sysinfo(info);
}

/*
 * funs_app_init() - Initialize application with configuration
 */
int funs_app_init(const funsos_app_config_t *config)
{
    /* If no config provided, use defaults via simple init call */
    if (config == NULL) {
        return syscall0(SYS_APP_INIT);
    }
    /* Pass config pointer to kernel for structured initialization */
    return syscall1(SYS_APP_INIT, (int)config);
}

/*
 * funs_app_cleanup() - Clean up application resources
 */
int funs_app_cleanup(void)
{
    return syscall0(SYS_APP_CLEANUP);
}

/* ================================================================
 *  Global Hotkey Registration API Implementation
 * ================================================================ */

/* Hotkey callback and ID types */
typedef void (*funsos_hotkey_callback_t)(uint32_t key, uint32_t mods, void *user_data);
typedef int funsos_hotkey_id_t;

#define FUNSOS_HOTKEY_EXISTS (-2)
#define FUNSOS_HOTKEY_FULL   (-3)

/* Maximum number of hotkeys the SDK can track simultaneously */
#define MAX_HOTKEYS 16

/* Hotkey slot table (static storage) */
static struct {
    int               in_use;
    uint32_t          key;
    uint32_t          mods;
    funsos_hotkey_callback_t callback;
    void             *user_data;
} hotkey_table[MAX_HOTKEYS];

/*
 * funs_register_hotkey() - Register a global hotkey combination
 */
funsos_hotkey_id_t funs_register_hotkey(uint32_t key, uint32_t mods,
                                         funsos_hotkey_callback_t callback,
                                         void *user_data)
{
    /* Find a free slot in our local table */
    int slot = -1;
    for (int i = 0; i < MAX_HOTKEYS; i++) {
        if (!hotkey_table[i].in_use) {
            slot = i;
            break;
        }
        /* Also check for duplicate registration */
        if (hotkey_table[i].in_use &&
            hotkey_table[i].key == key && hotkey_table[i].mods == mods) {
            return FUNSOS_HOTKEY_EXISTS;
        }
    }

    if (slot < 0) return FUNSOS_HOTKEY_FULL;

    /* Register with kernel via syscall */
    int ret = syscall3(SYS_REGISTER_HOTKEY, (int)key, (int)mods, (int)callback);
    if (ret < 0) return (funsos_hotkey_id_t)ret;

    /* Store in local table */
    hotkey_table[slot].in_use = 1;
    hotkey_table[slot].key = key;
    hotkey_table[slot].mods = mods;
    hotkey_table[slot].callback = callback;
    hotkey_table[slot].user_data = user_data;

    return (funsos_hotkey_id_t)slot;
}

/*
 * funs_unregister_hotkey() - Unregister a previously registered hotkey
 */
int funs_unregister_hotkey(funsos_hotkey_id_t hotkey_id)
{
    if (hotkey_id < 0 || hotkey_id >= MAX_HOTKEYS) return -1;
    if (!hotkey_table[hotkey_id].in_use) return -1;

    int ret = syscall1(SYS_UNREGISTER_HOTKEY, (int)hotkey_id);
    if (ret == 0) {
        hotkey_table[hotkey_id].in_use = 0;
        hotkey_table[hotkey_id].callback = NULL;
        hotkey_table[hotkey_id].user_data = NULL;
    }
    return ret;
}

/*
 * funs_is_hotkey_registered() - Check if a hotkey combo is registered
 */
int funs_is_hotkey_registered(uint32_t key, uint32_t mods)
{
    for (int i = 0; i < MAX_HOTKEYS; i++) {
        if (hotkey_table[i].in_use &&
            hotkey_table[i].key == key && hotkey_table[i].mods == mods) {
            return 1;
        }
    }
    return 0;
}

/* ================================================================
 *  Dialog / Message Box API Implementation
 * ================================================================ */

/* Message box type constants */
#define FUNSOS_MB_YESNO        0x04
#define FUNSOS_MB_ICON_QUESTION 0x20
#define FUNSOS_IDYES           6

/*
 * funs_message_box() - Show a modal message dialog box
 */
int funs_message_box(funsos_window_t parent, const char *title,
                     const char *message, uint32_t type)
{
    /* Pack parameters: parent | type in param1, title in param2, message via buffer */
    /* Simplified: pass type as primary parameter */
    return syscall3(SYS_MESSAGE_BOX, (int)parent.handle, (int)type, (int)message);
}

/*
 * funs_confirm_dialog() - Show a yes/no confirmation dialog
 */
int funs_confirm_dialog(funsos_window_t parent, const char *title,
                        const char *question)
{
    int ret = funs_message_box(parent, title, question,
                              FUNSOS_MB_YESNO | FUNSOS_MB_ICON_QUESTION);
    return (ret == FUNSOS_IDYES) ? 1 : 0;
}

/*
 * funs_input_dialog() - Show an input dialog for text entry
 */
int funs_input_dialog(funsos_window_t parent, const char *title,
                      const char *prompt, char *buf, uint32_t bufsize)
{
    if (buf == NULL || bufsize == 0) return -1;
    /* The kernel fills buf with user input; returns length or negative error */
    int ret = syscall4(SYS_MESSAGE_BOX, (int)parent.handle,
                       (int)prompt, (int)buf, (int)bufsize);
    return ret;
}

/* ================================================================
 *  Cursor Management API Implementation
 * ================================================================ */

/* Cursor shape constants */
#define CURSOR_ARROW     0
#define CURSOR_IBEAM     1
#define CURSOR_HAND      2
#define CURSOR_CROSS     3
#define CURSOR_WAIT      4
#define CURSOR_RESIZE_NS 5
#define CURSOR_RESIZE_EW 6
#define CURSOR_RESIZE_NWSE 7
#define CURSOR_RESIZE_NESW 8
#define CURSOR_FORBIDDEN 9
#define CURSOR_MOVE      10

/*
 * funs_set_cursor() - Change the mouse cursor shape
 * Parameters: cursor_type - one of CURSOR_* constants above
 * Returns: 0 on success, -1 on failure
 */
int funs_set_cursor(int cursor_type)
{
    return syscall1(SYS_SET_CURSOR, cursor_type);
}

/* ================================================================
 *  Extended Timer Management Implementation
 * ================================================================ */

/*
 * funsos_create_timer() - Create timer with mode and callback support
 */
int funsos_create_timer(uint32_t interval_ms, int mode,
                        funsos_timer_callback_t callback, void *user_data)
{
    /* Pack mode and callback into syscall parameters */
    (void)callback; (void)user_data;
    return syscall4(SYS_CREATE_TIMER_EXT, (int)interval_ms, mode,
                    (int)(uintptr_t)callback, (int)(uintptr_t)user_data);
}

/*
 * funsos_reset_timer() - Reset a running timer back to its interval
 */
int funsos_reset_timer(int timer_id)
{
    return syscall1(SYS_RESET_TIMER, timer_id);
}

/*
 * funsos_get_timer_remaining() - Query remaining time before next trigger
 */
int funsos_get_timer_remaining(int timer_id)
{
    return syscall1(SYS_GET_TIMER_REMAIN, timer_id);
}

/* ================================================================
 *  Clipboard API Implementation
 * ================================================================ */

/*
 * funs_set_clipboard_text() - Copy text to system clipboard
 */
int funs_set_clipboard_text(const char *text)
{
    if (text == NULL) return FUNSOS_ERR_INVAL;
    return syscall1(SYS_CLIPBOARD_SET, (int)text);
}

/*
 * funs_get_clipboard_text() - Retrieve text from system clipboard
 */
int funs_get_clipboard_text(char *buf, uint32_t bufsize)
{
    if (buf == NULL || bufsize == 0) return FUNSOS_ERR_INVAL;
    return syscall2(SYS_CLIPBOARD_GET, (int)buf, (int)bufsize);
}

/*
 * funs_clear_clipboard() - Clear all clipboard contents
 */
int funs_clear_clipboard(void)
{
    return syscall0(SYS_CLIPBOARD_CLEAR);
}

/*
 * funs_clipboard_has_data() - Check if clipboard contains data
 */
uint32_t funs_clipboard_has_data(void)
{
    return (uint32_t)syscall0(SYS_CLIPBOARD_QUERY);
}

/* ================================================================
 *  Event Filter / Bypass Mechanism Implementation
 * ================================================================ */

/* Maximum event filters that can be installed */
#define MAX_EVENT_FILTERS 8

static struct {
    int                    active;
    event_filter_t         filter_fn;
    void                  *filter_data;
    int                   priority;
} filter_table[MAX_EVENT_FILTERS];

static int filters_bypassed = 0;  /* Global bypass flag */

/*
 * funs_install_event_filter() - Install an event filter function
 */
int funs_install_event_filter(event_filter_t filter,
                               void *user_data, int priority)
{
    if (filter == NULL) return -1;

    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < MAX_EVENT_FILTERS; i++) {
        if (!filter_table[i].active) { slot = i; break; }
    }
    if (slot < 0) return -1;  /* No room */

    filter_table[slot].active = 1;
    filter_table[slot].filter_fn = filter;
    filter_table[slot].filter_data = user_data;
    filter_table[slot].priority = priority;

    /* Notify kernel */
    return syscall3(SYS_INSTALL_FILTER, (int)filter, (int)user_data, priority);
}

/*
 * funs_remove_event_filter() - Remove a previously installed filter
 */
int funs_remove_event_filter(int filter_id)
{
    if (filter_id < 0 || filter_id >= MAX_EVENT_FILTERS) return -1;
    if (!filter_table[filter_id].active) return -1;

    filter_table[filter_id].active = 0;
    filter_table[filter_id].filter_fn = NULL;
    filter_table[filter_id].filter_data = NULL;

    return syscall1(SYS_REMOVE_FILTER, filter_id);
}

/*
 * funs_bypass_filters() - Temporarily enable/disable filter bypass
 */
int funs_bypass_filters(int bypass)
{
    int old = filters_bypassed;
    filters_bypassed = bypass;
    return syscall1(SYS_BYPASS_FILTERS, bypass);
    /* Return previous state (syscall may override, use local value) */
    return old;
}

/*
 * funs_clear_all_filters() - Remove all installed event filters
 */
int funs_clear_all_filters(void)
{
    int count = 0;
    for (int i = 0; i < MAX_EVENT_FILTERS; i++) {
        if (filter_table[i].active) {
            filter_table[i].active = 0;
            filter_table[i].filter_fn = NULL;
            filter_table[i].filter_data = NULL;
            count++;
        }
    }
    /* Kernel-side clear */
    syscall0(0);  /* Best-effort: we've cleared local state */
    return count;
}

/* ================================================================
 *  Extended Window Management API Implementation
 * ================================================================ */

/* syscall6 helper - 6 argument syscall using stack */
static inline int syscall6(int num, int a1, int a2, int a3, int a4, int a5, int a6)
{
    int ret;
    __asm__ volatile (
        "push %%ebp\n"
        "mov %7, %%ebp\n"
        "push %%ebx\n"
        "mov %3, %%ebx\n"
        "int $0x80\n"
        "pop %%ebx\n"
        "pop %%ebp\n"
        : "=a"(ret)
        : "a"(num), "c"(a2), "d"(a3), "S"(a4), "D"(a5), "m"(a6), "m"(a6)
        : "memory"
    );
    return ret;
}

/*
 * funsos_create_window_ex() - Create window with style options
 */
funsos_window_t funsos_create_window_ex(int x, int y, int w, int h,
                                        const char *title, uint32_t style)
{
    /* Pass style as additional parameter to extended create syscall */
    funsos_window_t win = {0};
    win.handle = (uint32_t)syscall6(SYS_WINDOW_EX, x, y, w, h,
                                     (int)title, (int)style);
    return win;
}

/*
 * funsos_set_window_state() - Set window to a specific state
 */
int funsos_set_window_state(funsos_window_t win, int state)
{
    return syscall2(SYS_WINDOW_STATE, (int)win.handle, state);
}

/*
 * funsos_get_window_state() - Get current window state
 */
int funsos_get_window_state(funsos_window_t win)
{
    return syscall2(SYS_WINDOW_STATE, (int)win.handle, -1);  /* -1 = query mode */
}

/*
 * funsos_focus_window() - Give input focus to a window
 */
int funsos_focus_window(funsos_window_t win)
{
    return syscall1(SYS_FOCUS_WINDOW, (int)win.handle);
}

/*
 * funsos_raise_window() - Bring window to front of Z-order
 */
void funsos_raise_window(funsos_window_t win)
{
    syscall1(SYS_RAISE_WINDOW, (int)win.handle);
}

/*
 * funsos_get_window_rect() - Query window position and dimensions
 */
int funsos_get_window_rect(funsos_window_t win, funsos_rect_t *rect)
{
    if (rect == NULL) return -1;
    return syscall2(SYS_GET_WIN_RECT, (int)win.handle, (int)rect);
}
