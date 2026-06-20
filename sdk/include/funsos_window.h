#ifndef FUNSOS_WINDOW_SDK_H
#define FUNSOS_WINDOW_SDK_H

/*
 * funsos_window.h - FunsOS 窗口管理SDK头文件
 *
 * 为用户态程序提供窗口创建、管理和绘图的统一接口。
 * 底层通过syscall与桌面服务通信。
 */

#include "stdint.h"
#include "stddef.h"
#include "funsos_graphics.h"  /* 提供 funsos_surface_t 完整定义 */

/* ---- 窗口属性标志 ---- */
#define WIN_VISIBLE     0x01  /* 窗口可见 */
#define WIN_FOCUSED     0x02  /* 窗口拥有焦点 */
#define WIN_MAXIMIZED   0x04  /* 窗口已最大化 */
#define WIN_MINIMIZED   0x08  /* 窗口已最小化 */
#define WIN_RESIZABLE   0x10  /* 可调整大小 */
#define WIN_DECORATED   0x20  /* 有标题栏装饰 */
#define WIN_MODAL       0x40  /* 模态窗口 */
#define WIN_TOPMOST     0x80  /* 始终置顶 */
#define WIN_TRANSPARENT 0x100 /* 支持透明度 */
#define WIN_NO_ACTIVATE 0x200 /* 创建时不激活 */

/* ---- 光标类型常量 ---- */
#define WIN_CURSOR_DEFAULT   0
#define WIN_CURSOR_ARROW     1
#define WIN_CURSOR_TEXT      2
#define WIN_CURSOR_HAND      3
#define WIN_CURSOR_CROSSHAIR 4
#define WIN_CURSOR_MOVE      5
#define WIN_CURSOR_SIZE_NS   6
#define WIN_CURSOR_SIZE_EW   7
#define WIN_CURSOR_SIZE_NWSE 8
#define WIN_CURSOR_SIZE_NESW 9
#define WIN_CURSOR_PROGRESS  10
#define WIN_CURSOR_FORBIDDEN 11
#define WIN_CURSOR_HELP      12

/* ---- 窗口结构体 ---- */
typedef struct funsos_window {
    uint32_t handle;            /* 服务端句柄 */
    char     title[128];        /* 窗口标题 */
    int      x, y;              /* 窗口位置 */
    int      width, height;     /* 窗口尺寸 */
    uint32_t flags;             /* 属性标志 (WIN_*) */
    funsos_surface_t *client_surface;  /* 客户区绘图表面 */
    void    *user_data;         /* 用户自定义数据 */
    /* 回调函数指针 */
    void (*on_paint)(struct funsos_window *win, funsos_surface_t *surf);
    void (*on_resize)(struct funsos_window *win, int w, int h);
    void (*on_key)(struct funsos_window *win, int key, int action);
    void (*on_mouse)(struct funsos_window *win, int x, int y, int btn, int act);
    void (*on_close)(struct funsos_window *win);
    void (*on_focus)(struct funsos_window *win, int focused);
    uint8_t valid;              /* 结构体是否有效 */
} funsos_window_t;

/* ================================================================
 *  核心初始化
 * ================================================================ */

/*
 * 初始化窗口系统（分配内部数据结构）
 */
void window_system_init(void);

/* ================================================================
 *  窗口生命周期管理
 * ================================================================ */

/*
 * 创建新窗口
 * 参数: title - 窗口标题; x,y - 位置; w,h - 尺寸; flags - 属性标志
 * 返回: 窗口结构体指针, NULL 表示创建失败
 */
funsos_window_t* window_create(const char *title, int x, int y,
                               int w, int h, uint32_t flags);

/*
 * 销毁窗口并释放资源
 * 参数: win - 窗口指针
 * 返回: 0 成功, -1 失败
 */
int window_destroy(funsos_window_t *win);

/*
 * 显示窗口（将可见标志设为真并通知服务端）
 * 返回: 0 成功, -1 失败
 */
int window_show(funsos_window_t *win);

/*
 * 隐藏窗口
 * 返回: 0 成功, -1 失败
 */
int window_hide(funsos_window_t *win);

/*
 * 设置窗口标题
 * 参数: title - 新标题字符串(最长127字符)
 * 返回: 0 成功, -1 失败
 */
int window_set_title(funsos_window_t *win, const char *title);

/* ================================================================
 *  几何操作
 * ================================================================ */

/*
 * 移动窗口到指定位置
 * 返回: 0 成功, -1 失败
 */
int window_move(funsos_window_t *win, int x, int y);

/*
 * 调整窗口大小
 * 返回: 0 成功, -1 失败
 */
int window_resize(funsos_window_t *win, int w, int h);

/*
 * 同时设置位置和大小
 * 返回: 0 成功, -1 失败
 */
int window_set_bounds(funsos_window_t *win, int x, int y, int w, int h);

/*
 * 获取窗口客户区矩形
 * 参数: x,y,w,h - 输出客户区的位置和尺寸
 */
void window_get_client_rect(funsos_window_t *win, int *x, int *y,
                            int *w, int *h);

/* ================================================================
 *  Z-Order 操作
 * ================================================================ */

/*
 * 将窗口提升到最前端
 * 返回: 0 成功, -1 失败
 */
int window_raise(funsos_window_t *win);

/*
 * 将窗口降低到最后端
 * 返回: 0 成功, -1 失败
 */
int window_lower(funsos_window_t *win);

/*
 * 设置/取消窗口置顶属性
 * 参数: topmost - 1=置顶, 0=取消置顶
 * 返回: 0 成功, -1 失败
 */
int window_set_topmost(funsos_window_t *win, int topmost);

/* ================================================================
 *  属性设置
 * ================================================================ */

/*
 * 设置窗口不透明度
 * 参数: alpha - 不透明度值 (0=完全透明, 255=完全不透明)
 * 返回: 0 成功, -1 失败
 */
int window_set_opacity(funsos_window_t *win, uint8_t alpha);

/*
 * 设置鼠标光标样式
 * 参数: cursor_type - 光标类型 (WIN_CURSOR_*)
 * 返回: 0 成功, -1 失败
 */
int window_set_cursor(funsos_window_t *win, int cursor_type);

/*
 * 使指定矩形区域无效（触发重绘）
 * 返回: 0 成功, -1 失败
 */
int window_invalidate_rect(funsos_window_t *win, int x, int y,
                           int w, int h);

/*
 * 使整个客户区无效（触发完整重绘）
 * 返回: 0 成功, -1 失败
 */
int window_invalidate(funsos_window_t *win);

/* ================================================================
 *  回调函数设置 API
 * ================================================================ */

/*
 * 设置绘制回调
 */
void window_set_on_paint(funsos_window_t *win,
                         void (*cb)(funsos_window_t*, funsos_surface_t*));

/*
 * 设置尺寸变化回调
 */
void window_set_on_resize(funsos_window_t *win,
                          void (*cb)(funsos_window_t*, int, int));

/*
 * 设置键盘事件回调
 */
void window_set_on_key(funsos_window_t *win,
                       void (*cb)(funsos_window_t*, int, int));

/*
 * 设置鼠标事件回调
 */
void window_set_on_mouse(funsos_window_t *win,
                         void (*cb)(funsos_window_t*, int, int, int, int));

/*
 * 设置关闭事件回调
 */
void window_set_on_close(funsos_window_t *win,
                         void (*cb)(funsos_window_t*));

/* ================================================================
 *  消息循环 API
 * ================================================================ */

/* ---- 消息结构体 ---- */
typedef struct win_msg {
    uint32_t msg_id;    /* 消息ID */
    uint32_t wparam;    /* wParam 参数 */
    uint32_t lparam;    /* lParam 参数 */
    int64_t  time;      /* 消息时间戳 */
} win_msg_t;

/* ---- 消息ID常量 ---- */
#define WIN_MSG_PAINT     0x0001
#define WIN_MSG_CLOSE     0x0002
#define WIN_MSG_DESTROY   0x0003
#define WIN_MSG_KEYDOWN   0x0100
#define WIN_MSG_KEYUP     0x0101
#define WIN_MSG_CHAR      0x0102
#define WIN_MSG_MOUSEMOVE 0x0200
#define WIN_MSG_LBUTTONDOWN 0x0201
#define WIN_MSG_LBUTTONUP   0x0202
#define WIN_MSG_RBUTTONDOWN 0x0204
#define WIN_MSG_RBUTTONUP   0x0205
#define WIN_MSG_MOUSEWHEEL  0x020A
#define WIN_MSG_SIZE       0x0005
#define WIN_MSG_MOVE       0x0006
#define WIN_MSG_SETFOCUS   0x0007
#define WIN_MSG_KILLFOCUS  0x0008
#define WIN_MSG_TIMER      0x0083
#define WIN_MSG_QUIT       0x0012

/*
 * 进入消息循环（阻塞直到收到 WM_QUIT 消息）
 * 返回: 退出码
 */
int window_message_loop(void);

/*
 * 非阻塞地查看消息队列中是否有消息
 * 参数: msg - 接收消息的结构体
 * 返回: 1 有消息, 0 无消息, -1 错误
 */
int window_peek_message(win_msg_t *msg);

/*
 * 向指定窗口投递消息
 * 参数: win_handle - 目标窗口句柄; msg - 消息ID; wp/lp - 参数
 * 返回: 0 成功, -1 失败
 */
int window_post_message(uint32_t win_handle, uint32_t msg,
                        uint32_t wp, uint32_t lp);

/* ================================================================
 *  模态对话框 API
 * ================================================================ */

/*
 * 以模态方式运行对话框（阻塞父窗口直到对话框关闭）
 * 参数: parent - 父窗口; dialog - 对话框窗口
 * 返回: 对话框返回值
 */
int window_dialog_run(funsos_window_t *parent, funsos_window_t *dialog);

#endif /* FUNSOS_WINDOW_SDK_H */
