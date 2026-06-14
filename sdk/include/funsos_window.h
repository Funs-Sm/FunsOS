#ifndef FUNSOS_WINDOW_H
#define FUNSOS_WINDOW_H

/*
 * FUNSOS 窗口管理 API
 * 提供窗口的创建、销毁、移动、调整大小等操作。
 * 基于 kernel/sys_api.h 和 gui/window.h 的窗口函数封装。
 */

#include "stdint.h"

/* Forward declaration for funsos_rect_t (defined in funsos_graphics.h) */
#ifndef FUNSOS_RECT_T_DEFINED
#define FUNSOS_RECT_T_DEFINED
typedef struct { int32_t x; int32_t y; int32_t w; int32_t h; } funsos_rect_t;
#endif

/* ---- 窗口句柄类型 ---- */
typedef void *funsos_window_t;

/* ---- 窗口标志位 ---- */
#define FUNSOS_WIN_VISIBLE     0x01  /* 窗口可见 */
#define FUNSOS_WIN_BORDER      0x02  /* 显示边框 */
#define FUNSOS_WIN_TITLE       0x04  /* 显示标题栏 */
#define FUNSOS_WIN_RESIZABLE   0x08  /* 可调整大小 */
#define FUNSOS_WIN_CLOSABLE    0x10  /* 可关闭 */
#define FUNSOS_WIN_MINIMIZABLE 0x20  /* 可最小化 */

/* ---- 窗口装饰样式选项 ---- */
#define FUNSOS_STYLE_DEFAULT    0x0000  /* 默认样式（标题栏+边框+可关闭） */
#define FUNSOS_STYLE_POPUP      0x0100  /* 弹出式窗口（无任务栏图标） */
#define FUNSOS_STYLE_TOOL       0x0200  /* 工具窗口（窄标题栏） */
#define FUNSOS_STYLE_SPLASH     0x0300  /* 启动画面（无标题栏，居中） */
#define FUNSOS_STYLE_DIALOG     0x0400  /* 对话框样式（模态提示） */
#define FUNSOS_STYLE_BORDERLESS 0x0500  /* 无边框窗口 */
#define FUNSOS_STYLE_FIXED_SIZE 0x0600  /* 固定大小（不可调整） */
#define FUNSOS_STYLE_TOPMOST    0x0800  /* 置顶窗口（始终在最前） */
#define FUNSOS_STYLE_TASKBAR    0x1000  /* 显示在任务栏 */

/* ---- 窗口状态定义 ---- */
#define FUNSOS_WIN_STATE_NORMAL     0   /* 正常显示 */
#define FUNSOS_WIN_STATE_MINIMIZED  1   /* 已最小化（到任务栏） */
#define FUNSOS_WIN_STATE_MAXIMIZED  2   /* 已最大化（全屏幕但保留装饰） */
#define FUNSOS_WIN_STATE_FULLSCREEN 3   /* 全屏模式（无装饰，独占显示） */
#define FUNSOS_WIN_STATE_HIDDEN     4   /* 隐藏状态 */
#define FUNSOS_WIN_STATE_ICONIFIED  5   /* 图标化（同 minimized 的别名） */

/*
 * 创建新窗口
 * 参数: x, y - 窗口位置; w, h - 窗口尺寸; title - 窗口标题
 * 返回: 窗口句柄，失败返回 NULL
 */
funsos_window_t funsos_create_window(int x, int y, int w, int h, const char *title);

/*
 * 创建带样式的窗口（扩展版创建函数）
 * 参数: x, y - 窗口位置; w, h - 窗口尺寸; title - 窗口标题;
 *       style - 窗口样式 (FUNSOS_STYLE_* 组合)
 * 返回: 窗口句柄，失败返回 NULL
 */
funsos_window_t funsos_create_window_ex(int x, int y, int w, int h,
                                        const char *title, uint32_t style);

/*
 * 销毁窗口
 * 参数: win - 窗口句柄
 * 返回: 0 成功, -1 失败
 */
int funsos_destroy_window(funsos_window_t win);

/*
 * 设置窗口标题
 * 参数: win - 窗口句柄; title - 新标题
 * 返回: 0 成功, -1 失败
 */
int funsos_set_window_title(funsos_window_t win, const char *title);

/*
 * 请求窗口重绘
 * 参数: win - 窗口句柄
 * 返回: 0 成功, -1 失败
 */
int funsos_invalidate_window(funsos_window_t win);

/*
 * 显示窗口
 * 参数: win - 窗口句柄
 */
void funsos_show_window(funsos_window_t win);

/*
 * 隐藏窗口
 * 参数: win - 窗口句柄
 */
void funsos_hide_window(funsos_window_t win);

/*
 * 移动窗口到指定位置
 * 参数: win - 窗口句柄; x, y - 新位置
 */
void funsos_move_window(funsos_window_t win, int x, int y);

/*
 * 调整窗口大小
 * 参数: win - 窗口句柄; w, h - 新尺寸
 */
void funsos_resize_window(funsos_window_t win, int w, int h);

/*
 * 获取窗口的图形上下文（用于绘图）
 * 参数: win - 窗口句柄
 * 返回: 图形上下文指针
 */
void *funsos_get_window_context(funsos_window_t win);

/* ---- 窗口状态控制 ---- */

/*
 * 设置窗口状态（最小化/最大化/全屏等）
 * 参数: win - 窗口句柄; state - 目标状态 (FUNSOS_WIN_STATE_* )
 * 返回: 0 成功, -1 失败
 */
int funsos_set_window_state(funsos_window_t win, int state);

/*
 * 获取当前窗口状态
 * 参数: win - 窗口句柄
 * 返回: 当前状态 (FUNSOS_WIN_STATE_* ), -1 表示无效句柄
 */
int funsos_get_window_state(funsos_window_t win);

/*
 * 使窗口获得焦点
 * 参数: win - 窗口句柄
 * 返回: 0 成功, -1 失败
 */
int funsos_focus_window(funsos_window_t win);

/*
 * 将窗口置于最前端
 * 参数: win - 窗口句柄
 */
void funsos_raise_window(funsos_window_t win);

/*
 * 获取窗口的位置和尺寸
 * 参数: win - 窗口句柄; rect - 输出的矩形区域
 * 返回: 0 成功, -1 失败
 */
int funsos_get_window_rect(funsos_window_t win, funsos_rect_t *rect);

/* ---- 事件类型常量 (集中定义供全局引用) ---- */
#define FUNSOS_EVENT_NONE         0    /* 无事件 */
#define FUNSOS_EVENT_KEY_PRESS    1    /* 键盘按下 */
#define FUNSOS_EVENT_KEY_RELEASE  2    /* 键盘释放 */
#define FUNSOS_EVENT_MOUSE_MOVE   3    /* 鼠标移动 */
#define FUNSOS_EVENT_MOUSE_CLICK  4    /* 鼠标点击 */
#define FUNSOS_EVENT_WINDOW_CLOSE 5    /* 窗口关闭 */
#define FUNSOS_EVENT_TIMER        6    /* 定时器 */
#define FUNSOS_EVENT_MOUSE_PRESS  7    /* 鼠标按下 */
#define FUNSOS_EVENT_MOUSE_RELEASE 8   /* 鼠标释放 */
#define FUNSOS_EVENT_WINDOW_MOVE  9    /* 窗口移动 */
#define FUNSOS_EVENT_WINDOW_RESIZE 10  /* 窗口大小改变 */
#define FUNSOS_EVENT_FOCUS        11   /* 获得焦点 */
#define FUNSOS_EVENT_UNFOCUS      12   /* 失去焦点 */
#define FUNSOS_EVENT_EXPOSE       13   /* 窗口需要重绘 */
#define FUNSOS_EVENT_MINIMIZE     14   /* 窗口最小化 */
#define FUNSOS_EVENT_MAXIMIZE     15   /* 窗口最大化 */
#define FUNSOS_EVENT_RESTORE      16   /* 窗口恢复（从最小化/最大化） */
#define FUNSOS_EVENT_ENTER        17   /* 鼠标进入窗口区域 */
#define FUNSOS_EVENT_LEAVE        18   /* 鼠标离开窗口区域 */
#define FUNSOS_EVENT_SCROLL       19   /* 鼠标滚轮滚动 */
#define FUNSOS_EVENT_CLIPBOARD    20   /* 剪贴板内容变化 */
#define FUNSOS_EVENT_DRAG_ENTER   21   /* 拖拽进入窗口 */
#define FUNSOS_EVENT_DRAG_MOVE    22   /* 拖拽移动中 */
#define FUNSOS_EVENT_DRAG_DROP    23   /* 拖拽放下 */
#define FUNSOS_EVENT_DRAG_LEAVE   24   /* 拖拽离开窗口 */

/* ---- 对话框 / 消息框 API ---- */

/* 消息框按钮组合 */
#define FUNSOS_MB_OK              0x00000000L  /* 确定 */
#define FUNSOS_MB_OKCANCEL        0x00000001L  /* 确定 / 取消 */
#define FUNSOS_MB_YESNO           0x00000004L  /* 是 / 否 */
#define FUNSOS_YESNOCANCEL        0x00000003L  /* 是 / 否 / 取消 */

/* 消息框图标类型 */
#define FUNSOS_MB_ICON_NONE       0x00000000L  /* 无图标 */
#define FUNSOS_MB_ICON_INFO       0x00000040L  /* 信息图标 (i) */
#define FUNSOS_MB_ICON_WARNING    0x00000030L  /* 警告图标 (!) */
#define FUNSOS_MB_ICON_ERROR      0x00000010L  /* 错误图标 (X) */
#define FUNSOS_MB_ICON_QUESTION   0x00000020L  /* 问号图标 (?) */

/* 消息框返回值 */
#define FUNSOS_IDOK      1
#define FUNSOS_IDCANCEL  2
#define FUNSOS_IDYES     6
#define FUNSOS_IDNO      7

/*
 * 显示模态消息对话框
 * 参数: parent - 父窗⼝句柄 (可为 NULL); title - 对话框标题;
 *       message - 消息文本; type - 按钮与图标组合 (FUNSOS_MB_* | FUNSOS_MB_ICON_* )
 * 返回: 用户点击的按钮 ID (FUNSOS_ID*), -1 表示错误
 */
int funs_message_box(funsos_window_t parent, const char *title,
                     const char *message, uint32_t type);

/*
 * 显示简单的确认对话框（确定/取消）
 * 参数: parent - 父窗口; title - 标题; question - 提问文本
 * 返回: 1=用户点击确定, 0=用户点击取消, -1=错误
 */
int funs_confirm_dialog(funsos_window_t parent, const char *title,
                        const char *question);

/*
 * 显示输入对话框（获取用户输入的单行文本）
 * 参数: parent - 父窗口; title - 标题; prompt - 提示文本;
 *       buf - 接收输入的缓冲区; bufsize - 缓冲区大小
 * 返回: 输入的字符数, -1 表示取消或错误
 */
int funs_input_dialog(funsos_window_t parent, const char *title,
                      const char *prompt, char *buf, uint32_t bufsize);

#endif /* FUNSOS_WINDOW_H */
