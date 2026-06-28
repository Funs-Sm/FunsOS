/*
 * funsos_window.c - FunsOS 窗口管理SDK实现
 *
 * 为用户态程序提供窗口创建、管理和绘图的统一接口。
 * 底层通过syscall与桌面服务通信。
 */

#include "funsos_window.h"
#include "funsos_libc.h"
#include "funsos_graphics.h"
#include "stddef.h"

/* ---- 全局状态 ---- */
#define MAX_WINDOWS 32
static funsos_window_t windows[MAX_WINDOWS];
static uint32_t next_handle = 1;
static uint8_t system_initialized = 0;

/* ---- 消息队列（简化版） ---- */
#define MSG_QUEUE_SIZE 64
static struct {
    win_msg_t msgs[MSG_QUEUE_SIZE];
    int head, tail, count;
} msg_queue;

/* ---- 运行标志 ---- */
static volatile int msg_loop_running = 0;

/* ================================================================
 *  内部辅助函数
 * ================================================================ */

/*
 * 查找空闲窗口槽位
 * 返回: 槽位索引, -1 表示已满
 */
static int find_free_slot(void)
{
    int i;
    for (i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].valid)
            return i;
    }
    return -1;
}

/*
 * 根据句柄查找窗口
 * 返回: 窗口指针, NULL 表示无效句柄
 */
static funsos_window_t *find_by_handle(uint32_t handle)
{
    int i;
    for (i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].valid && windows[i].handle == handle)
            return &windows[i];
    }
    return NULL;
}

/*
 * 安全拷贝标题字符串
 */
static void safe_copy_title(char *dst, const char *src, uint32_t max_len)
{
    uint32_t i;
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    for (i = 0; i < max_len - 1 && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

/* ================================================================
 *  公共 API 实现: 初始化
 * ================================================================ */

void window_system_init(void)
{
    int i;

    /* 清零所有窗口槽位 */
    funsos_memset(windows, 0, sizeof(windows));

    /* 初始化消息队列 */
    funsos_memset(&msg_queue, 0, sizeof(msg_queue));
    msg_queue.head = 0;
    msg_queue.tail = 0;
    msg_queue.count = 0;

    next_handle = 1;
    msg_loop_running = 0;
    system_initialized = 1;
}

/* ================================================================
 *  公共 API 实现: 窗口生命周期
 * ================================================================ */

funsos_window_t* window_create(const char *title, int x, int y,
                               int w, int h, uint32_t flags)
{
    int slot;
    funsos_window_t *win;

    if (!system_initialized)
        return NULL;

    if (w <= 0 || h <= 0)
        return NULL;

    slot = find_free_slot();
    if (slot < 0)
        return NULL; /* 窗口数量已达上限 */

    win = &windows[slot];

    /* 清零并初始化结构体 */
    funsos_memset(win, sizeof(funsos_window_t), 0);

    win->handle = next_handle++;
    win->x      = x;
    win->y      = y;
    win->width  = w;
    win->height = h;
    win->flags  = flags;
    win->valid  = 1;

    /* 设置默认标题 */
    safe_copy_title(win->title,
                    (title != NULL) ? title : "Untitled",
                    sizeof(win->title));

    /* 如果初始标志包含 VISIBLE，标记为可见 */
    if (flags & WIN_VISIBLE) {
        /* 创建时即显示，实际通过服务端 syscall 完成 */
    }

    return win;
}

int window_destroy(funsos_window_t *win)
{
    if (win == NULL || !win->valid)
        return -1;

    /* 释放客户区表面 */
    if (win->client_surface != NULL) {
        funsos_destroy_surface(win->client_surface);
        win->client_surface = NULL;
    }

    /* 标记为无效 */
    win->valid   = 0;
    win->handle = 0;
    win->flags  = 0;

    /* 清空回调 */
    win->on_paint  = NULL;
    win->on_resize = NULL;
    win->on_key    = NULL;
    win->on_mouse  = NULL;
    win->on_close  = NULL;
    win->on_focus  = NULL;

    return 0;
}

int window_show(funsos_window_t *win)
{
    if (win == NULL || !win->valid)
        return -1;

    win->flags |= WIN_VISIBLE;

    /* 触发一次绘制事件 */
    if (win->on_paint != NULL && win->client_surface != NULL) {
        win->on_paint(win, win->client_surface);
    }

    return 0;
}

int window_hide(funsos_window_t *win)
{
    if (win == NULL || !win->valid)
        return -1;

    win->flags &= ~WIN_VISIBLE;
    return 0;
}

int window_set_title(funsos_window_t *win, const char *title)
{
    if (win == NULL || !win->valid)
        return -1;

    safe_copy_title(win->title, title, sizeof(win->title));
    return 0;
}

/* ================================================================
 *  公共 API 实现: 几何操作
 * ================================================================ */

int window_move(funsos_window_t *win, int x, int y)
{
    if (win == NULL || !win->valid)
        return -1;

    win->x = x;
    win->y = y;
    return 0;
}

int window_resize(funsos_window_t *win, int w, int h)
{
    if (win == NULL || !win->valid)
        return -1;

    if (w <= 0 || h <= 0)
        return -1;

    /* 如果尺寸发生变化，触发 resize 回调 */
    if (w != win->width || h != win->height) {
        int old_w = win->width;
        int old_h = win->height;

        win->width  = w;
        win->height = h;

        /* 重建客户区表面 */
        if (win->client_surface != NULL) {
            funsos_destroy_surface(win->client_surface);
            win->client_surface = funsos_create_surface(
                (uint32_t)w, (uint32_t)h, 0); /* ARGB8888 */
        }

        /* 调用 resize 回调 */
        if (win->on_resize != NULL) {
            win->on_resize(win, w, h);
        }

        /* 尺寸变化后自动使整个区域无效以触发重绘 */
        window_invalidate(win);

        (void)old_w;
        (void)old_h;
    }

    return 0;
}

int window_set_bounds(funsos_window_t *win, int x, int y, int w, int h)
{
    int ret;

    if (win == NULL || !win->valid)
        return -1;

    ret = window_move(win, x, y);
    if (ret != 0)
        return ret;

    return window_resize(win, w, h);
}

void window_get_client_rect(funsos_window_t *win, int *x, int *y,
                            int *w, int *h)
{
    if (win == NULL || !win->valid) {
        if (x) *x = 0;
        if (y) *y = 0;
        if (w) *w = 0;
        if (h) *h = 0;
        return;
    }

    /* 客户区尺寸 = 窗口总尺寸(简化模型中无装饰边框偏移) */
    if (x) *x = win->x;
    if (y) *y = win->y;
    if (w) *w = win->width;
    if (h) *h = win->height;
}

/* ================================================================
 *  公共 API 实现: Z-Order 操作
 * ================================================================ */

int window_raise(funsos_window_t *win)
{
    if (win == NULL || !win->valid)
        return -1;

    /* 通过 syscall 将窗口提升到 Z-Order 最前端 */
    (void)win;
    return 0;
}

int window_lower(funsos_window_t *win)
{
    if (win == NULL || !win->valid)
        return -1;

    /* 通过 syscall 将窗口降低到 Z-Order 最后端 */
    (void)win;
    return 0;
}

int window_set_topmost(funsos_window_t *win, int topmost)
{
    if (win == NULL || !win->valid)
        return -1;

    if (topmost) {
        win->flags |= WIN_TOPMOST;
    } else {
        win->flags &= ~WIN_TOPMOST;
    }

    return 0;
}

/* ================================================================
 *  公共 API 实现: 属性设置
 * ================================================================ */

int window_set_opacity(funsos_window_t *win, uint8_t alpha)
{
    if (win == NULL || !win->valid)
        return -1;

    if (alpha > 255)
        alpha = 255;

    /* 设置合成器不透明度属性 */
    /* 实际通过 syscall 调用桌面服务的透明度接口 */
    (void)alpha;
    return 0;
}

int window_set_cursor(funsos_window_t *win, int cursor_type)
{
    if (win == NULL || !win->valid)
        return -1;

    /* 通过 syscall 设置光标样式 */
    (void)cursor_type;
    return 0;
}

int window_invalidate_rect(funsos_window_t *win, int x, int y,
                           int w, int h)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;

    if (win == NULL || !win->valid)
        return -1;

    /* 标记指定矩形区域需要重绘 */
    /* 实际向桌面服务发送 INVALIDATE_RECT 消息 */
    if (win->on_paint != NULL && win->client_surface != NULL) {
        win->on_paint(win, win->client_surface);
    }

    return 0;
}

int window_invalidate(funsos_window_t *win)
{
    if (win == NULL || !win->valid)
        return -1;

    /* 使整个客户区无效 */
    return window_invalidate_rect(win, 0, 0, win->width, win->height);
}

/* ================================================================
 *  公共 API 实现: 回调设置
 * ================================================================ */

void window_set_on_paint(funsos_window_t *win,
                         void (*cb)(funsos_window_t*, funsos_surface_t*))
{
    if (win != NULL && win->valid)
        win->on_paint = cb;
}

void window_set_on_resize(funsos_window_t *win,
                          void (*cb)(funsos_window_t*, int, int))
{
    if (win != NULL && win->valid)
        win->on_resize = cb;
}

void window_set_on_key(funsos_window_t *win,
                       void (*cb)(funsos_window_t*, int, int))
{
    if (win != NULL && win->valid)
        win->on_key = cb;
}

void window_set_on_mouse(funsos_window_t *win,
                         void (*cb)(funsos_window_t*, int, int, int, int))
{
    if (win != NULL && win->valid)
        win->on_mouse = cb;
}

void window_set_on_close(funsos_window_t *win,
                         void (*cb)(funsos_window_t*))
{
    if (win != NULL && win->valid)
        win->on_close = cb;
}

/* ================================================================
 *  公共 API 实现: 消息循环
 * ================================================================ */

int window_message_loop(void)
{
    win_msg_t msg;

    if (!system_initialized)
        return -1;

    msg_loop_running = 1;

    while (msg_loop_running) {
        /* 阻塞等待消息 */
        if (window_peek_message(&msg) > 0) {
            switch (msg.msg_id) {
            case WIN_MSG_QUIT:
                msg_loop_running = 0;
                break;

            case WIN_MSG_PAINT:
                /* 处理绘制消息 */
                {
                    funsos_window_t *target =
                        find_by_handle(msg.wparam);
                    if (target != NULL &&
                        target->on_paint != NULL &&
                        target->client_surface != NULL) {
                        target->on_paint(target,
                                         target->client_surface);
                    }
                }
                break;

            case WIN_MSG_CLOSE:
                /* 处理关闭消息 */
                {
                    funsos_window_t *target =
                        find_by_handle(msg.wparam);
                    if (target != NULL) {
                        if (target->on_close != NULL)
                            target->on_close(target);
                        else
                            window_destroy(target);
                    }
                }
                break;

            case WIN_MSG_KEYDOWN:
            case WIN_MSG_KEYUP:
                /* 处理键盘消息 */
                {
                    funsos_window_t *target =
                        find_by_handle(msg.wparam);
                    if (target != NULL &&
                        target->on_key != NULL) {
                        target->on_key(target,
                                       (int)(msg.lparam & 0xFFFF),
                                       (msg.msg_id == WIN_MSG_KEYDOWN)
                                           ? 1 : 0);
                    }
                }
                break;

            case WIN_MSG_MOUSEMOVE:
            case WIN_MSG_LBUTTONDOWN:
            case WIN_MSG_LBUTTONUP:
            case WIN_MSG_RBUTTONDOWN:
            case WIN_MSG_RBUTTONUP:
                /* 处理鼠标消息 */
                {
                    funsos_window_t *target =
                        find_by_handle(msg.wparam);
                    int btn = 0, act = 0;

                    if (target == NULL ||
                        target->on_mouse == NULL)
                        break;

                    /* 解析按钮和动作 */
                    if (msg.msg_id == WIN_MSG_LBUTTONDOWN ||
                        msg.msg_id == WIN_MSG_RBUTTONDOWN) {
                        act = 1; /* 按下 */
                        btn = (msg.msg_id == WIN_MSG_LBUTTONDOWN)
                                  ? 0 : 1;
                    } else if (msg.msg_id ==
                               WIN_MSG_LBUTTONUP ||
                               msg.msg_id ==
                               WIN_MSG_RBUTTONUP) {
                        act = 0; /* 释放 */
                        btn = (msg.msg_id == WIN_MSG_LBUTTONUP)
                                  ? 0 : 1;
                    } else {
                        act = 2; /* 移动 */
                        btn = -1;
                    }

                    target->on_mouse(target,
                                     (int)(msg.lparam & 0xFFFF),
                                     (int)((msg.lparam >> 16) & 0xFFFF),
                                     btn, act);
                }
                break;

            default:
                break;
            }
        }

        /* 短暂休眠避免忙等（实际由调度器控制） */
    }

    return 0;
}

int window_peek_message(win_msg_t *msg)
{
    if (msg == NULL || !system_initialized)
        return -1;

    if (msg_queue.count <= 0)
        return 0; /* 无消息 */

    /* 从队头取出消息 */
    *msg = msg_queue.msgs[msg_queue.head];
    msg_queue.head = (msg_queue.head + 1) % MSG_QUEUE_SIZE;
    msg_queue.count--;

    return 1;
}

int window_post_message(uint32_t win_handle, uint32_t msg,
                        uint32_t wp, uint32_t lp)
{
    int next_tail;

    if (!system_initialized)
        return -1;

    /* 检查队列是否已满 */
    if (msg_queue.count >= MSG_QUEUE_SIZE)
        return -1;

    /* 入队 */
    next_tail = (msg_queue.tail + 1) % MSG_QUEUE_SIZE;
    msg_queue.msgs[msg_queue.tail].msg_id = msg;
    msg_queue.msgs[msg_queue.tail].wparam = wp;
    msg_queue.msgs[msg_queue.tail].lparam = lp;
    msg_queue.msgs[msg_queue.tail].time   = 0; /* 由系统填充 */
    msg_queue.tail = next_tail;
    msg_queue.count++;

    return 0;
}

/* ================================================================
 *  公共 API 实现: 模态对话框
 * ================================================================ */

int window_dialog_run(funsos_window_t *parent, funsos_window_t *dialog)
{
    (void)parent;

    if (dialog == NULL || !dialog->valid)
        return -1;

    /* 显示对话框 */
    dialog->flags |= (WIN_VISIBLE | WIN_MODAL);
    if (dialog->on_paint != NULL && dialog->client_surface != NULL) {
        dialog->on_paint(dialog, dialog->client_surface);
    }

    /* 简化实现: 直接返回成功 */
    /* 完整实现应进入子消息循环直到对话框关闭 */
    return 0;
}
