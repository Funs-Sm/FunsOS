/*
 * fr_input.h - 输入事件处理系统接口定义
 *
 * 提供统一的输入事件管理:
 *   - 键盘事件队列 (按键按下/释放、Unicode 输入)
 *   - 鼠标输入追踪 (位置、按钮状态、滚轮)
 *   - 触摸/手势识别 (点击、双击、拖动、缩放)
 *   - 输入焦点管理
 *   - 事件分发到活动控件
 */

#ifndef FR_INPUT_H
#define FR_INPUT_H

#include "stdint.h"

/* 前向声明 */
struct fr_context;
struct fr_widget;

/* ---- 键盘事件 ---- */

/* 键盘扫描码 (常用子集) */
#define FR_KEY_UNKNOWN        0x00
#define FR_KEY_ESCAPE         0x01
#define FR_KEY_1              0x02
#define FR_KEY_2              0x03
#define FR_KEY_3              0x04
#define FR_KEY_4              0x05
#define FR_KEY_5              0x06
#define FR_KEY_6              0x07
#define FR_KEY_7              0x08
#define FR_KEY_8              0x09
#define FR_KEY_9              0x0A
#define FR_KEY_0              0x0B
#define FR_KEY_MINUS          0x0C
#define FR_KEY_EQUALS         0x0D
#define FR_KEY_BACKSPACE      0x0E
#define FR_KEY_TAB            0x0F
#define FR_KEY_Q              0x10
#define FR_KEY_W              0x11
#define FR_KEY_E              0x12
#define FR_KEY_R              0x13
#define FR_KEY_T              0x14
#define FR_KEY_Y              0x15
#define FR_KEY_U              0x16
#define FR_KEY_I              0x17
#define FR_KEY_O              0x18
#define FR_KEY_P              0x19
#define FR_KEY_LBRACKET       0x1A
#define FR_KEY_RBRACKET       0x1B
#define FR_KEY_ENTER          0x1C
#define FR_KEY_LCTRL          0x1D
#define FR_KEY_A              0x1E
#define FR_KEY_S              0x1F
#define FR_KEY_D              0x20
#define FR_KEY_F              0x21
#define FR_KEY_G              0x22
#define FR_KEY_H              0x23
#define FR_KEY_J              0x24
#define FR_KEY_K              0x25
#define FR_KEY_L              0x26
#define FR_KEY_SEMICOLON      0x27
#define FR_KEY_QUOTE          0x28
#define FR_KEY_BACKQUOTE      0x29
#define FR_KEY_LSHIFT         0x2A
#define FR_KEY_BACKSLASH      0x2B
#define FR_KEY_Z              0x2C
#define FR_KEY_X              0x2D
#define FR_KEY_C              0x2E
#define FR_KEY_V              0x2F
#define FR_KEY_B              0x30
#define FR_KEY_N              0x31
#define FR_KEY_M              0x32
#define FR_KEY_COMMA          0x33
#define FR_KEY_PERIOD         0x34
#define FR_KEY_SLASH          0x35
#define FR_KEY_RSHIFT         0x36
#define FR_KEY_NP_MULTIPLY    0x37
#define FR_KEY_LALT           0x38
#define FR_KEY_SPACE          0x39
#define FR_KEY_CAPSLOCK       0x3A
#define FR_KEY_F1             0x3B
#define FR_KEY_F2             0x3C
#define FR_KEY_F3             0x3D
#define FR_KEY_F4             0x3E
#define FR_KEY_F5             0x3F
#define FR_KEY_F6             0x40
#define FR_KEY_F7             0x41
#define FR_KEY_F8             0x42
#define FR_KEY_F9             0x43
#define FR_KEY_F10            0x44
#define FR_KEY_F11            0x57
#define FR_KEY_F12            0x58
#define FR_KEY_NUMLOCK        0x45
#define FR_KEY_SCROLLLOCK     0x46
#define FR_KEY_HOME           0x47
#define FR_KEY_UP             0x48
#define FR_KEY_PAGEUP         0x49
#define FR_KEY_NP_MINUS       0x4A
#define FR_KEY_LEFT           0x4B
#define FR_KEY_NP_CENTER      0x4C
#define FR_KEY_RIGHT          0x4D
#define FR_KEY_NP_PLUS        0x4E
#define FR_KEY_END            0x4F
#define FR_KEY_DOWN           0x50
#define FR_KEY_PAGEDOWN       0x51
#define FR_KEY_NP_ENTER       0x5C
#define FR_KEY_RCTRL          0x5D
#define FR_KEY_RALT           0x5E
#define FR_KEY_DEL            0x53

/* 键盘修饰键状态 */
#define FR_MOD_SHIFT          0x01
#define FR_MOD_CTRL           0x02
#define FR_MOD_ALT            0x04
#define FR_MOD_CAPS           0x08
#define FR_MOD_NUM            0x10

/* 键盘事件类型 */
#define FR_KEY_EVENT_PRESS    0   /* 按下 */
#define FR_KEY_EVENT_RELEASE  1   /* 释放 */
#define FR_KEY_EVENT_REPEAT   2   /* 长按重复 */

/* 键盘事件结构 */
#ifndef FR_KEY_EVENT_T_DEFINED
#define FR_KEY_EVENT_T_DEFINED
typedef struct {
    uint32_t key;                 /* 虚拟键码 (fr_events.h) */
    uint32_t scancode;            /* 扫描码 */
    uint8_t event_type;           /* 事件类型 (FR_KEY_EVENT_*) */
    uint8_t modifiers;            /* 修饰键状态 */
    char ascii;                   /* ASCII 字符 (如果有) */
    uint32_t unicode;             /* Unicode 码点 */
    char text[8];                 /* UTF-8 字符 (fr_events.h) */
} fr_key_event_t;
#endif

/* ---- 鼠标事件 ---- */

/* 鼠标按钮 */
#ifndef FR_MOUSE_LEFT
#define FR_MOUSE_LEFT        0x01
#define FR_MOUSE_RIGHT       0x02
#define FR_MOUSE_MIDDLE      0x04
#endif
#define FR_MOUSE_EXTRA1      0x08
#define FR_MOUSE_EXTRA2      0x10

/* 鼠标事件类型 */
#define FR_MOUSE_EVENT_MOVE       0   /* 移动 */
#define FR_MOUSE_EVENT_PRESS      1   /* 按下 */
#define FR_MOUSE_EVENT_RELEASE    2   /* 释放 */
#define FR_MOUSE_EVENT_WHEEL      3   /* 滚轮滚动 */
#define FR_MOUSE_EVENT_ENTER      4   /* 进入控件区域 */
#define FR_MOUSE_EVENT_LEAVE      5   /* 离开控件区域 */

/* 鼠标事件结构 */
#ifndef FR_MOUSE_EVENT_T_DEFINED
#define FR_MOUSE_EVENT_T_DEFINED
typedef struct {
    int32_t x, y;                  /* 当前坐标 (相对于屏幕) */
    int32_t rel_x, rel_y;          /* 相对移动量 */
    uint8_t buttons;               /* 当前按钮状态 (位掩码) */
    uint8_t button;                /* 单个按钮 (fr_events.h) */
    uint8_t button_changed;        /* 本次变化的按钮 */
    uint8_t event_type;            /* 事件类型 */
    uint8_t clicks;                /* 点击次数 (fr_events.h) */
    uint8_t modifiers;             /* 修饰键 (fr_events.h) */
    int32_t wheel_delta;           /* 滚轮增量 (+上 / -下) */
} fr_mouse_event_t;
#endif

/* ---- 触摸/手势事件 ---- */

/* 手势类型 */
#define FR_GESTURE_TAP           1   /* 单击 */
#define FR_GESTURE_DOUBLE_TAP    2   /* 双击 */
#define FR_GESTURE_DRAG          3   /* 拖动 */
#define FR_GESTURE_PINCH         4   /* 双指缩放 */
#define FR_GESTURE_ROTATE        5   /* 旋转 */
#define FR_GESTURE_SWIPE         6   /* 滑动 */
#define FR_GESTURE_LONG_PRESS    7   /* 长按 */

/* 触摸点 */
typedef struct {
    int x, y;                      /* 触摸位置 */
    uint32_t id;                   /* 触摸 ID */
    uint8_t active;                /* 是否活跃 */
} fr_touch_point_t;

/* 手势事件结构 */
typedef struct {
    uint8_t gesture_type;          /* 手势类型 */
    fr_touch_point_t points[10];   /* 触摸点数组 (最多 10 点) */
    uint8_t point_count;           /* 活跃触摸点数 */
    float pinch_distance;          /* 缩放距离变化 */
    float rotation_angle;          /* 旋转角度 */
    float velocity_x, velocity_y;  /* 滑动速度 */
    int start_x, start_y;          /* 手势起始位置 */
    int current_x, current_y;      /* 当前位置 */
} fr_gesture_event_t;

/* ---- 输入焦点 ---- */

/* 焦点策略 */
#define FR_FOCUS_CLICK_TO_FOCUS  0   /* 点击获取焦点 */
#define FR_FOCUS_POINTER         1   /* 鼠标悬停即聚焦 */
#define FR_FOCUS_STRONG          2   /* 强焦点 (需显式释放) */

/* ---- 输入系统状态 ---- */

/* 键盘事件队列大小 */
#define FR_KEY_QUEUE_SIZE         256

/* 鼠标事件队列大小 */
#define FR_MOUSE_QUEUE_SIZE       128

/* 手势识别时间阈值 (ms) */
#define FR_DOUBLE_TAP_TIME_MS     400
#define FR_LONG_PRESS_TIME_MS     500
#define FR_DRAG_DISTANCE_PX       5

typedef struct fr_input_state {
    /* 键盘状态 */
    fr_key_event_t key_queue[FR_KEY_QUEUE_SIZE];
    uint32_t key_head, key_tail;
    uint8_t current_modifiers;
    uint8_t keys_pressed[256];    /* 当前按下的键 */

    /* 鼠标状态 */
    fr_mouse_event_t mouse_queue[FR_MOUSE_QUEUE_SIZE];
    uint32_t mouse_head, mouse_tail;
    int mouse_x, mouse_y;          /* 当前鼠标位置 */
    uint8_t mouse_buttons;         /* 当前按钮状态 */

    /* 触摸状态 */
    fr_touch_point_t touch_points[10];
    fr_gesture_event_t last_gesture;
    uint32_t touch_start_time;     /* 触摸开始时间戳 */
    int touch_start_x, touch_start_y;

    /* 焦点管理 */
    struct fr_widget *focus_widget;  /* 当前拥有焦点的控件 */
    struct fr_widget *hover_widget;  /* 当前鼠标悬停的控件 */
    uint8_t focus_strategy;          /* 焦点策略 */

    /* 手势识别内部状态 */
    int tap_count;                  /* 连续点击次数 */
    uint32_t last_tap_time;         /* 上次点击时间 */
    int is_dragging;                /* 是否正在拖动 */
    int drag_start_x, drag_start_y; /* 拖动起始位置 */
    float initial_pinch_dist;       /* 初始双指距离 */

    /* 统计 */
    uint32_t total_key_events;
    uint32_t total_mouse_events;
    uint32_t total_gestures;
} fr_input_state_t;

/* ---- 公共 API ---- */

/* 初始化输入系统 */
void fr_input_init(void);

/* 重置输入系统状态 */
void fr_input_reset(void);

/* ---- 键盘操作 ---- */

/* 推送键盘事件到队列 */
void fr_input_push_key_event(uint8_t scancode, uint8_t event_type,
                              uint8_t modifiers, char ascii, uint32_t unicode);

/* 从队列取出键盘事件 (非阻塞) */
int fr_input_pop_key_event(fr_key_event_t *out_event);

/* 检查某个键是否当前被按下 */
int fr_input_is_key_pressed(uint8_t scancode);

/* 获取当前修饰键状态 */
uint8_t fr_input_get_modifiers(void);

/* ---- 鼠标操作 ---- */

/* 推送鼠标事件到队列 */
void fr_input_push_mouse_event(int x, int y, int rel_x, int rel_y,
                                uint8_t buttons, uint8_t button_changed,
                                uint8_t event_type, int wheel_delta);

/* 从队列取出鼠标事件 (非阻塞) */
int fr_input_pop_mouse_event(fr_mouse_event_t *out_event);

/* 获取当前鼠标位置 */
void fr_input_get_mouse_pos(int *x, int *y);

/* ---- 触摸/手势操作 ---- */

/* 更新触摸点状态 */
void fr_input_update_touch(uint32_t touch_id, int x, int y, int active);

/* 结束触摸 (抬起手指) */
void fr_input_end_touch(uint32_t touch_id);

/* 处理手势识别逻辑 (每帧调用) */
void fr_input_process_gestures(void);

/* 获取最近一次识别的手势 */
const fr_gesture_event_t *fr_input_get_last_gesture(void);

/* ---- 焦点管理 ---- */

/* 设置输入焦点到指定控件 */
void fr_input_set_focus(struct fr_widget *widget);

/* 获取当前拥有焦点的控件 */
struct fr_widget *fr_input_get_focus(void);

/* 设置鼠标悬停的控件 */
void fr_input_set_hover(struct fr_widget *widget);

/* 获取当前悬停的控件 */
struct fr_widget *fr_input_get_hover(void);

/* 设置焦点策略 */
void fr_input_set_focus_strategy(uint8_t strategy);

/* ---- 事件分发 ---- */

/* 将待处理的输入事件分发给对应的控件 */
void fr_input_dispatch(struct fr_context *ctx);

/* 清空所有事件队列 */
void fr_input_flush_queues(void);

#endif /* FR_INPUT_H */
