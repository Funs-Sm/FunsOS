/* fr_events.h - 事件系统
 * 鼠标/键盘/焦点/拖拽/滚轮事件分发
 */

#ifndef FR_EVENTS_H
#define FR_EVENTS_H

#include "stdint.h"

/* 事件类型 */
#define FR_EVENT_MOUSE_MOVE      1
#define FR_EVENT_MOUSE_PRESS     2
#define FR_EVENT_MOUSE_RELEASE   3
#define FR_EVENT_MOUSE_DOUBLE    4
#define FR_EVENT_MOUSE_WHEEL     5
#define FR_EVENT_KEY_PRESS       6
#define FR_EVENT_KEY_RELEASE     7
#define FR_EVENT_FOCUS_IN        8
#define FR_EVENT_FOCUS_OUT       9
#define FR_EVENT_DRAG_START      10
#define FR_EVENT_DRAG_MOVE       11
#define FR_EVENT_DRAG_END        12
#define FR_EVENT_RESIZE          13
#define FR_EVENT_CLOSE           14
#define FR_EVENT_SHOW            15
#define FR_EVENT_HIDE            16
#define FR_EVENT_VALUE_CHANGE    17
#define FR_EVENT_TEXT_CHANGE     18
#define FR_EVENT_SELECT_CHANGE   19

/* 鼠标按钮 */
#ifndef FR_MOUSE_LEFT
#define FR_MOUSE_LEFT     1
#define FR_MOUSE_RIGHT    2
#define FR_MOUSE_MIDDLE   3
#endif

/* 修饰键 */
#define FR_MOD_SHIFT      0x01
#define FR_MOD_CTRL       0x02
#define FR_MOD_ALT        0x04
#define FR_MOD_SUPER      0x08

/* 鼠标事件数据 */
#ifndef FR_MOUSE_EVENT_T_DEFINED
#define FR_MOUSE_EVENT_T_DEFINED
typedef struct {
    int32_t x;
    int32_t y;
    int32_t rel_x;        /* 相对移动量 */
    int32_t rel_y;
    uint8_t button;
    uint8_t clicks;
    uint8_t modifiers;
} fr_mouse_event_t;
#endif

/* 键盘事件数据 */
#ifndef FR_KEY_EVENT_T_DEFINED
#define FR_KEY_EVENT_T_DEFINED
typedef struct {
    uint32_t key;
    uint32_t scancode;
    uint8_t modifiers;
    char text[8];         /* UTF-8 字符 */
} fr_key_event_t;
#endif

/* 滚轮事件数据 */
typedef struct {
    int32_t x;
    int32_t y;
    int32_t delta_x;
    int32_t delta_y;
    uint8_t modifiers;
} fr_wheel_event_t;

/* 拖拽事件数据 */
typedef struct {
    int32_t start_x;
    int32_t start_y;
    int32_t current_x;
    int32_t current_y;
    int32_t delta_x;
    int32_t delta_y;
    fr_handle_t source;
} fr_drag_event_t;

/* 通用事件 */
typedef struct {
    uint32_t type;
    union {
        fr_mouse_event_t mouse;
        fr_key_event_t key;
        fr_wheel_event_t wheel;
        fr_drag_event_t drag;
    } data;
    fr_handle_t target;
    uint8_t consumed;      /* 事件是否已被处理 */
} fr_event_t;

/* 事件处理器类型 */
typedef void (*fr_event_handler_t)(fr_handle_t widget, fr_event_t *event, void *user_data);

/* 事件系统操作 */
void fr_event_system_init(fr_handle_t ctx);
void fr_event_system_shutdown(fr_handle_t ctx);

/* 事件分发 */
void fr_event_dispatch(fr_handle_t ctx, fr_event_t *event);

/* 注册/注销事件处理器 */
int fr_event_register(fr_handle_t widget, uint32_t event_type,
                      fr_event_handler_t handler, void *user_data);
int fr_event_unregister(fr_handle_t widget, uint32_t event_type);

/* 焦点管理 */
void fr_focus_set(fr_handle_t ctx, fr_handle_t widget);
fr_handle_t fr_focus_get(fr_handle_t ctx);

/* 拖拽管理 */
int fr_drag_begin(fr_handle_t ctx, fr_handle_t widget, int x, int y);
void fr_drag_update(fr_handle_t ctx, int x, int y);
void fr_drag_end(fr_handle_t ctx);

#endif /* FR_EVENTS_H */
