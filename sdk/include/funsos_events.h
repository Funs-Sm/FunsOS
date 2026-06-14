#ifndef FUNSOS_EVENTS_H
#define FUNSOS_EVENTS_H

/*
 * FUNSOS 事件系统 API
 * 提供键盘、鼠标、窗口、定时器等事件的处理。
 * 基于 kernel/sys_api.h 和 gui/window.h 的事件定义。
 */

#include "stdint.h"

/* ---- 事件类型常量 ---- */
#define FUNSOS_EVENT_KEY_PRESS    1   /* 键盘按下 */
#define FUNSOS_EVENT_KEY_RELEASE  2   /* 键盘释放 */
#define FUNSOS_EVENT_MOUSE_MOVE   3   /* 鼠标移动 */
#define FUNSOS_EVENT_MOUSE_CLICK  4   /* 鼠标点击 */
#define FUNSOS_EVENT_WINDOW_CLOSE 5   /* 窗口关闭 */
#define FUNSOS_EVENT_TIMER        6   /* 定时器 */
#define FUNSOS_EVENT_MOUSE_PRESS  7   /* 鼠标按下 */
#define FUNSOS_EVENT_MOUSE_RELEASE 8  /* 鼠标释放 */
#define FUNSOS_EVENT_WINDOW_MOVE  9   /* 窗口移动 */
#define FUNSOS_EVENT_WINDOW_RESIZE 10 /* 窗口大小改变 */
#define FUNSOS_EVENT_FOCUS        11  /* 获得焦点 */
#define FUNSOS_EVENT_UNFOCUS      12  /* 失去焦点 */
#define FUNSOS_EVENT_EXPOSE       13  /* 窗口需要重绘 */

/* ---- 扩展事件类型 ---- */
#define FUNSOS_EVENT_CLIPBOARD_UPDATE  30  /* 剪贴板内容更新 */
#ifndef FUNSOS_EVENT_DRAG_ENTER
#define FUNSOS_EVENT_DRAG_ENTER        31  /* 拖拽进入 */
#endif
#ifndef FUNSOS_EVENT_DRAG_LEAVE
#define FUNSOS_EVENT_DRAG_LEAVE        32  /* 拖拽离开 */
#endif
#ifndef FUNSOS_EVENT_DRAG_MOVE
#define FUNSOS_EVENT_DRAG_MOVE         33  /* 拖拽移动 */
#endif
#ifndef FUNSOS_EVENT_DRAG_DROP
#define FUNSOS_EVENT_DRAG_DROP         34  /* 拖拽放下 */
#endif
#define FUNSOS_EVENT_GLOBAL_HOTKEY     35  /* 全局热键触发 */
#define FUNSOS_EVENT_TIMER_TICK        36  /* 高精度定时器滴答 */

/* ---- 事件结构体 ---- */
typedef struct {
    uint32_t type;          /* 事件类型 */
    uint32_t param1;        /* 参数1（键盘: 键码; 鼠标: x坐标） */
    uint32_t param2;        /* 参数2（鼠标: y坐标） */
    void    *window;        /* 关联的窗口句柄 */
    uint32_t timestamp;     /* 事件时间戳（tick 数） */
    uint32_t modifiers;     /* 修饰键状态 (SHIFT/CTRL/ALT) */
} funsos_event_t;

/* ---- 鼠标按钮 ---- */
#define FUNSOS_MOUSE_LEFT    0
#define FUNSOS_MOUSE_RIGHT   1
#define FUNSOS_MOUSE_MIDDLE  2

/* ---- 修饰键 ---- */
#define FUNSOS_MOD_SHIFT     0x01
#define FUNSOS_MOD_CTRL      0x02
#define FUNSOS_MOD_ALT       0x04

/* ---- 常用键码 ---- */
#define FUNSOS_KEY_ESCAPE    0x1B
#define FUNSOS_KEY_ENTER     0x0D
#define FUNSOS_KEY_TAB       0x09
#define FUNSOS_KEY_BACKSPACE 0x08
#define FUNSOS_KEY_SPACE     0x20
#define FUNSOS_KEY_DELETE    0x7F
#define FUNSOS_KEY_UP        0x4800
#define FUNSOS_KEY_DOWN      0x5000
#define FUNSOS_KEY_LEFT      0x4B00
#define FUNSOS_KEY_RIGHT     0x4D00
#define FUNSOS_KEY_HOME      0x4700
#define FUNSOS_KEY_END       0x4F00
#define FUNSOS_KEY_PAGEUP    0x4900
#define FUNSOS_KEY_PAGEDOWN  0x5100
#define FUNSOS_KEY_F1        0x3B00
#define FUNSOS_KEY_F12       0x3C00

/* ---- 基础事件轮询 API ---- */

/*
 * 轮询事件（非阻塞）
 * 如果有事件就填入 event 并返回 1，否则返回 0。
 * 参数: event - 接收事件的结构体指针
 * 返回: 1 有事件, 0 无事件
 */
int funsos_poll_event(funsos_event_t *event);

/*
 * 等待事件（阻塞）
 * 阻塞当前进程直到有事件到达。
 * 参数: event - 接收事件的结构体指针
 * 返回: 1 成功获取事件, 0 失败
 */
int funsos_wait_event(funsos_event_t *event);

/* ================================================================
 *  Timer Event API (Extended)
 * ================================================================ */

/*
 * 设置定时器（基础版）
 * 参数: interval_ms - 定时器间隔（毫秒）
 * 返回: 定时器 ID，-1 表示失败
 */
int funsos_set_timer(uint32_t interval_ms);

/*
 * 取消定时器
 * 参数: timer_id - 定时器 ID
 * 返回: 0 成功, -1 失败
 */
int funsos_cancel_timer(int timer_id);

/* 定时器触发模式 */
#define FUNSOS_TIMER_ONESHOT  0  /* 单次触发 */
#define FUNSOS_TIMER_PERIODIC 1  /* 周期性触发 */

/*
 * 创建扩展定时器（支持单次/周期模式及回调）
 * 参数: interval_ms - 间隔（毫秒）; mode - 触发模式;
 *       callback - 到期回调函数 (NULL 则发送事件);
 *       user_data - 传递给回调的用户数据
 * 返回: 定时器 ID, -1 表示失败
 */
typedef void (*funsos_timer_callback_t)(uint32_t timer_id, void *user_data);

#ifndef FUNSOS_TIMER_CALLBACK_T_DEFINED
#define FUNSOS_TIMER_CALLBACK_T_DEFINED
#endif

int funsos_create_timer(uint32_t interval_ms, int mode,
                        funsos_timer_callback_t callback, void *user_data);

/*
 * 重置定时器（重新开始计时）
 * 参数: timer_id - 定时器 ID
 * 返回: 0 成功, -1 失败
 */
int funsos_reset_timer(int timer_id);

/*
 * 查询定时器剩余时间
 * 参数: timer_id - 定时器 ID
 * 返回: 剩余毫秒数, -1 表示无效 ID
 */
int funsos_get_timer_remaining(int timer_id);

/* ================================================================
 *  Clipboard / Drag-Drop Event Types & API
 * ================================================================ */

/* 剪贴板数据格式 (legacy, use funsos_clipboard.h for new code) */
#ifndef FUNSOS_CLIPBOARD_TEXT_LEGACY
#define FUNSOS_CLIPBOARD_TEXT_LEGACY    0  /* 纯文本 */
#define FUNSOS_CLIPBOARD_BITMAP_LEGACY  1  /* 位图图像 */
#define FUNSOS_CLIPBOARD_FILELIST_LEGACY 2 /* 文件列表 */
#define FUNSOS_CLIPBOARD_CUSTOM_LEGACY  99 /* 自定义格式 */
#endif

/*
 * 将文本放入系统剪贴板
 * 参数: text - 要复制的文本内容
 * 返回: 0 成功, -1 失败
 */
int funs_set_clipboard_text(const char *text);

/*
 * 从系统剪贴板获取文本
 * 参数: buf - 接收缓冲区; bufsize - 缓冲区大小
 * 返回: 实际复制的字符数（不含终止符）, -1 表示失败
 */
int funs_get_clipboard_text(char *buf, uint32_t bufsize);

/*
 * 清空剪贴板
 * 返回: 0 成功, -1 失败
 */
int funs_clear_clipboard(void);

/*
 * 检查剪贴板是否有可用数据
 * 返回: 数据格式 (FUNSOS_CLIPBOARD_*), 0 表示空
 */
uint32_t funs_clipboard_has_data(void);

/* ---- 拖放事件数据 ---- */

/* 拖放数据描述符 */
typedef struct {
    uint32_t data_type;          /* 数据类型 (FUNSOS_CLIPBOARD_*) */
    void    *data_ptr;           /* 数据指针 */
    uint32_t data_size;          /* 数据大小（字节） */
    int      x, y;               /* 放下位置的坐标 */
    uint32_t source_window;      /* 来源窗口（如果有） */
} funsos_drag_data_t;

/*
 * 设置拖放数据源（开始拖拽操作）
 * 参数: drag_data - 拖放数据描述符
 * 返回: 0 成功, -1 失败
 */
int funs_start_drag(const funsos_drag_data_t *drag_data);

/*
 * 获取拖放数据（在 DRAG_DROP 事件中使用）
 * 参数: out_data - 接收拖放数据的结构体指针
 * 返回: 0 成功获取数据, -1 无数据
 */
int funs_get_drag_data(funsos_drag_data_t *out_data);

/* ================================================================
 *  Global Hotkey Registration API
 * ================================================================ */

/* 热键标识符类型 */
typedef int funsos_hotkey_id_t;

/* 热键注册结果 */
#define FUNSOS_HOTKEY_OK       0   /* 注册成功 */
#define FUNSOS_HOTKEY_EXISTS   -1  /* 热键已存在 */
#define FUNSOS_HOTKEY_FULL     -2  /* 热键槽已满 */
#define FUNSOS_HOTKEY_INVALID  -3  /* 无效的热键组合 */

/*
 * 注册全局热键（系统级快捷键，即使窗口不在前台也能响应）
 * 参数: key - 键码 (FUNSOS_KEY_* 或 ASCII);
 *       mods - 修饰键组合 (FUNSOS_MOD_SHIFT | FUNSOS_MOD_CTRL 等);
 *       callback - 热键触发时的回调函数;
 *       user_data - 传给回调的用户数据
 * 返回: 热键 ID (>=0), 或负的错误码 (FUNSOS_HOTKEY_*)
 */
typedef void (*funsos_hotkey_callback_t)(funsos_hotkey_id_t hotkey_id,
                                         uint32_t key, uint32_t mods,
                                         void *user_data);

funsos_hotkey_id_t funs_register_hotkey(uint32_t key, uint32_t mods,
                                         funsos_hotkey_callback_t callback,
                                         void *user_data);

/*
 * 注销全局热键
 * 参数: hotkey_id - 要注销的热键 ID
 * 返回: 0 成功, -1 失败
 */
int funs_unregister_hotkey(funsos_hotkey_id_t hotkey_id);

/*
 * 检查某个热键是否已注册
 * 参数: key - 键码; mods - 修饰键
 * 返回: 1 已注册, 0 未注册
 */
int funs_is_hotkey_registered(uint32_t key, uint32_t mods);

/* ================================================================
 *  Event Filter / Bypass Mechanism
 * ================================================================ */

/* 事件过滤器返回值 */
#define FUNSOS_FILTER_PASS    0  /* 事件正常分发（继续传递） */
#define FUNSOS_FILTER_CONSUME 1  /* 事件被消费（不再传递给其他处理器） */
#define FUNSOS_FILTER_BLOCK   2  /* 事件被完全阻止（丢弃） */

/*
 * 事件过滤器函数类型
 * 参数: event - 待处理的事件; user_data - 注册时传入的用户数据
 * 返回: 过滤决定 (FUNSOS_FILTER_*)
 */
typedef int (*funsos_event_filter_t)(funsos_event_t *event, void *user_data);

/*
 * 安装全局事件过滤器（在事件分发之前调用）
 * 参数: filter - 过滤器函数; user_data - 用户数据;
 *       priority - 优先级（数值越小越先执行，0 为默认）
 * 返回: 过滤器 ID (>=0), -1 表示安装失败
 */
int funs_install_event_filter(funsos_event_filter_t filter,
                               void *user_data, int priority);

/*
 * 移除事件过滤器
 * 参数: filter_id - 过滤器 ID
 * 返回: 0 成功, -1 失败
 */
int funs_remove_event_filter(int filter_id);

/*
 * 临时绕过所有事件过滤器（直接派发事件）
 * 参数: bypass - 1=绕过过滤器, 0=恢复过滤器
 * 返回: 之前的绕过状态
 */
int funs_bypass_filters(int bypass);

/*
 * 清除所有已安装的事件过滤器
 * 返回: 被清除的过滤器数量
 */
int funs_clear_all_filters(void);

#endif /* FUNSOS_EVENTS_H */
