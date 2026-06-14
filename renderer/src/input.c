/*
 * input.c - 输入事件处理系统实现
 *
 * 统一管理键盘、鼠标和触摸输入:
 *   - 键盘事件队列: 存储按键按下/释放/重复事件
 *   - 鼠标状态跟踪: 位置、按钮状态、滚轮增量
 *   - 手势识别引擎: 从触摸点序列识别常见手势
 *   - 焦点管理: 维护当前输入焦点控件
 *   - 事件分发: 将事件路由到正确的目标控件
 */

#include "fr_input.h"
#include "funrender.h"
#include "fr_context.h"
#include "fr_widgets.h"
#include "string.h"
#include "math.h"

/* ---- 全局输入状态 ---- */
static fr_input_state_t g_input;

/* ---- 时间辅助 (使用简单的 tick 计数) ---- */
static uint32_t g_input_tick = 0;

void fr_input_tick_advance(void) { g_input_tick++; }
uint32_t fr_input_get_tick(void) { return g_input_tick; }

/* ---- 键盘操作实现 ---- */

/*
 * fr_input_init - 初始化输入系统
 *
 * 清空所有事件队列, 重置状态变量。
 */
void fr_input_init(void) {
    memset(&g_input, 0, sizeof(fr_input_state_t));
    g_input.key_head = 0;
    g_input.key_tail = 0;
    g_input.mouse_head = 0;
    g_input.mouse_tail = 0;
    g_input.focus_strategy = FR_FOCUS_CLICK_TO_FOCUS;
    g_input_tick = 0;
}

/*
 * fr_input_reset - 重置输入系统
 */
void fr_input_reset(void) {
    fr_input_init();
}

/*
 * fr_input_push_key_event - 推送键盘事件到队列
 *
 * 采用环形缓冲区实现。如果队列满则丢弃最旧的事件。
 */
void fr_input_push_key_event(uint8_t scancode, uint8_t event_type,
                              uint8_t modifiers, char ascii, uint32_t unicode) {
    uint32_t next = (g_input.key_tail + 1) % FR_KEY_QUEUE_SIZE;
    if (next == g_input.key_head) {
        /* 队列满, 丢弃最旧的事件 */
        g_input.key_head = (g_input.key_head + 1) % FR_KEY_QUEUE_SIZE;
    }

    fr_key_event_t *evt = &g_input.key_queue[g_input.key_tail];
    evt->scancode = scancode;
    evt->event_type = event_type;
    evt->modifiers = modifiers;
    evt->ascii = ascii;
    evt->unicode = unicode;

    g_input.key_tail = next;

    /* 更新按键状态表 */
    if (event_type == FR_KEY_EVENT_PRESS) {
        g_input.keys_pressed[scancode] = 1;
        g_input.current_modifiers = modifiers;
    } else if (event_type == FR_KEY_EVENT_RELEASE) {
        g_input.keys_pressed[scancode] = 0;
        /* 重新计算修饰键状态 */
        g_input.current_modifiers = 0;
        if (g_input.keys_pressed[FR_KEY_LSHIFT] || g_input.keys_pressed[FR_KEY_RSHIFT])
            g_input.current_modifiers |= FR_MOD_SHIFT;
        if (g_input.keys_pressed[FR_KEY_LCTRL] || g_input.keys_pressed[FR_KEY_RCTRL])
            g_input.current_modifiers |= FR_MOD_CTRL;
        if (g_input.keys_pressed[FR_KEY_LALT] || g_input.keys_pressed[FR_KEY_RALT])
            g_input.current_modifiers |= FR_MOD_ALT;
    }

    g_input.total_key_events++;
}

/*
 * fr_input_pop_key_event - 从队列取出键盘事件
 *
 * 非阻塞操作。返回 1=成功取出, 0=队列为空。
 */
int fr_input_pop_key_event(fr_key_event_t *out_event) {
    if (out_event == NULL) return 0;
    if (g_input.key_head == g_input.key_tail) return 0;

    *out_event = g_input.key_queue[g_input.key_head];
    g_input.key_head = (g_input.key_head + 1) % FR_KEY_QUEUE_SIZE;
    return 1;
}

int fr_input_is_key_pressed(uint8_t scancode) {
    if (scancode >= 256) return 0;
    return g_input.keys_pressed[scancode] ? 1 : 0;
}

uint8_t fr_input_get_modifiers(void) {
    return g_input.current_modifiers;
}

/* ---- 鼠标操作实现 ---- */

/*
 * fr_input_push_mouse_event - 推送鼠标事件到队列
 *
 * 同时更新鼠标位置和按钮状态的内部追踪。
 */
void fr_input_push_mouse_event(int x, int y, int rel_x, int rel_y,
                                uint8_t buttons, uint8_t button_changed,
                                uint8_t event_type, int wheel_delta) {
    uint32_t next = (g_input.mouse_tail + 1) % FR_MOUSE_QUEUE_SIZE;
    if (next == g_input.mouse_head) {
        g_input.mouse_head = (g_input.mouse_head + 1) % FR_MOUSE_QUEUE_SIZE;
    }

    fr_mouse_event_t *evt = &g_input.mouse_queue[g_input.mouse_tail];
    evt->x = x;
    evt->y = y;
    evt->rel_x = rel_x;
    evt->rel_y = rel_y;
    evt->buttons = buttons;
    evt->button_changed = button_changed;
    evt->event_type = event_type;
    evt->wheel_delta = wheel_delta;

    g_input.mouse_tail = next;

    /* 更新内部鼠标状态 */
    g_input.mouse_x = x;
    g_input.mouse_y = y;
    g_input.mouse_buttons = buttons;

    g_input.total_mouse_events++;
}

int fr_input_pop_mouse_event(fr_mouse_event_t *out_event) {
    if (out_event == NULL) return 0;
    if (g_input.mouse_head == g_input.mouse_tail) return 0;

    *out_event = g_input.mouse_queue[g_input.mouse_head];
    g_input.mouse_head = (g_input.mouse_head + 1) % FR_MOUSE_QUEUE_SIZE;
    return 1;
}

void fr_input_get_mouse_pos(int *x, int *y) {
    if (x) *x = g_input.mouse_x;
    if (y) *y = g_input.mouse_y;
}

/* ---- 触摸/手势识别实现 ---- */

/*
 * fr_input_update_touch - 更新触摸点位置
 *
 * 当手指在触摸屏上移动时调用。
 */
void fr_input_update_touch(uint32_t touch_id, int x, int y, int active) {
    if (touch_id >= 10) return;

    fr_touch_point_t *tp = &g_input.touch_points[touch_id];
    tp->x = x;
    tp->y = y;
    tp->id = touch_id;
    tp->active = active;

    /* 第一个触摸点按下时记录起始时间和位置 */
    if (active && g_input.touch_start_time == 0) {
        g_input.touch_start_time = g_input_tick;
        g_input.touch_start_x = x;
        g_input.touch_start_y = y;
    }
}

/*
 * fr_input_end_touch - 结束触摸 (手指抬起)
 *
 * 触发手势识别逻辑判断最终手势类型。
 */
void fr_input_end_touch(uint32_t touch_id) {
    if (touch_id >= 10) return;

    g_input.touch_points[touch_id].active = 0;

    /* 检查是否所有触摸都已结束 */
    int any_active = 0;
    for (int i = 0; i < 10; i++) {
        if (g_input.touch_points[i].active) { any_active = 1; break; }
    }

    if (!any_active && g_input.touch_start_time > 0) {
        uint32_t duration = g_input_tick - g_input.touch_start_time;
        int dx = g_input.mouse_x - g_input.touch_start_x;
        int dy = g_input.mouse_y - g_input.touch_start_y;
        int dist = dx * dx + dy * dy;

        if (duration >= FR_LONG_PRESS_TIME_MS && dist < FR_DRAG_DISTANCE_PX * FR_DRAG_DISTANCE_PX) {
            /* 长按: 按住时间超过阈值且位移很小 */
            g_input.last_gesture.gesture_type = FR_GESTURE_LONG_PRESS;
        } else if (dist >= FR_DRAG_DISTANCE_PX * FR_DRAG_DISTANCE_PX) {
            /* 拖动: 位移超过阈值 */
            g_input.last_gesture.gesture_type = FR_GESTURE_DRAG;
        } else {
            /* 单击: 短时间小位移 */
            uint32_t now = g_input_tick;
            if (now - g_input.last_tap_time <= FR_DOUBLE_TAP_TIME_MS) {
                g_input.tap_count++;
                if (g_input.tap_count >= 2) {
                    g_input.last_gesture.gesture_type = FR_GESTURE_DOUBLE_TAP;
                    g_input.tap_count = 0;
                } else {
                    g_input.last_gesture.gesture_type = FR_GESTURE_TAP;
                }
            } else {
                g_input.tap_count = 1;
                g_input.last_gesture.gesture_type = FR_GESTURE_TAP;
            }
            g_input.last_tap_time = now;
        }

        g_input.last_gesture.start_x = g_input.touch_start_x;
        g_input.last_gesture.start_y = g_input.touch_start_y;
        g_input.last_gesture.current_x = g_input.mouse_x;
        g_input.last_gesture.current_y = g_input.mouse_y;

        g_input.total_gestures++;
        g_input.touch_start_time = 0;
    }
}

/*
 * fr_input_process_gestures - 处理持续手势的中间状态
 *
 * 应该每帧调用一次以检测进行中的拖动、缩放等手势。
 */
void fr_input_process_gestures(void) {
    /* 检测双指缩放 (Pinch): 计算两指间距离变化 */
    int active_count = 0;
    fr_touch_point_t pts[2];
    for (int i = 0; i < 10 && active_count < 2; i++) {
        if (g_input.touch_points[i].active) {
            pts[active_count++] = g_input.touch_points[i];
        }
    }

    if (active_count == 2) {
        int dx = pts[0].x - pts[1].x;
        int dy = pts[0].y - pts[1].y;
        float dist = sqrt((float)(dx * dx + dy * dy));

        if (g_input.initial_pinch_dist > 0) {
            float delta = dist - g_input.initial_pinch_dist;
            if (delta < -10.0f || delta > 10.0f) {
                /* 缩放手势正在进行 */
                g_input.last_gesture.gesture_type = FR_GESTURE_PINCH;
                g_input.last_gesture.pinch_distance = delta;
            }
        } else {
            g_input.initial_pinch_dist = dist;
        }
    } else {
        g_input.initial_pinch_dist = 0;
    }

    /* 检测拖动过程中的速度 */
    if (g_input.is_dragging) {
        g_input.last_gesture.velocity_x =
            (float)(g_input.mouse_x - g_input.drag_start_x);
        g_input.last_gesture.velocity_y =
            (float)(g_input.mouse_y - g_input.drag_start_y);
    }
}

const fr_gesture_event_t *fr_input_get_last_gesture(void) {
    return &g_input.last_gesture;
}

/* ---- 焦点管理实现 ---- */

/*
 * fr_input_set_focus - 设置输入焦点
 *
 * 将焦点转移到指定控件, 并通知之前拥有焦点的控件失去焦点。
 */
void fr_input_set_focus(struct fr_widget *widget) {
    /* 通知之前的焦点控件失去焦点 */
    if (g_input.focus_widget &&
        g_input.focus_widget->on_focus) {
        g_input.focus_widget->on_focus(g_input.focus_widget, 0);
    }

    g_input.focus_widget = widget;

    /* 通知新的焦点控件获得焦点 */
    if (widget && widget->on_focus) {
        widget->on_focus(widget, 1);
    }
}

struct fr_widget *fr_input_get_focus(void) {
    return g_input.focus_widget;
}

void fr_input_set_hover(struct fr_widget *widget) {
    g_input.hover_widget = widget;
}

struct fr_widget *fr_input_get_hover(void) {
    return g_input.hover_widget;
}

void fr_input_set_focus_strategy(uint8_t strategy) {
    g_input.focus_strategy = strategy;
}

/* ---- 事件分发实现 ---- */

/*
 * fr_input_dispatch - 分发输入事件到目标控件
 *
 * 处理流程:
 *   1. 处理所有待处理的鼠标事件 -> 悬停检测、点击检测
 *   2. 处理所有待处理的键盘事件 -> 发送到焦点控件
 *   3. 处理手势事件 -> 发送到悬停/焦点控件
 *
 * 使用命中测试 (hit test) 确定事件的目标控件。
 */
void fr_input_dispatch(struct fr_context *ctx) {
    if (ctx == NULL || ctx->root_widget == NULL) return;

    fr_widget_t *root = (fr_widget_t *)ctx->root_widget;

    /* ---- 处理鼠标事件 ---- */
    fr_mouse_event_t mevt;
    while (fr_input_pop_mouse_event(&mevt)) {
        /* 命中测试: 查找鼠标位置下的最上层控件 */
        fr_widget_t *target = root;
        fr_widget_t *hit_widget = NULL;

        /* 简单递归命中测试 */
        void hit_test(fr_widget_t *w) {
            if (!(w->state & FR_STATE_VISIBLE)) return;
            if (mevt.x >= w->bounds.x &&
                mevt.x < w->bounds.x + w->bounds.w &&
                mevt.y >= w->bounds.y &&
                mevt.y < w->bounds.y + w->bounds.h) {
                hit_widget = w;
                /* 递归检查子控件 (后添加的在上面) */
                fr_widget_t *child = w->first_child;
                while (child) {
                    hit_test(child);
                    child = child->next_sibling;
                }
            }
        }
        hit_test(root);

        if (hit_widget) {
            /* 更新悬停状态 */
            if (hit_widget != g_input.hover_widget) {
                if (g_input.hover_widget && g_input.hover_widget->handle_event) {
                    /* 发送 LEAVE 事件给旧悬停控件 */
                    fr_mouse_event_t leave_evt = mevt;
                    leave_evt.event_type = FR_MOUSE_EVENT_LEAVE;
                    g_input.hover_widget->handle_event(g_input.hover_widget,
                                                        &leave_evt);
                }
                g_input.hover_widget = hit_widget;
                if (hit_widget->handle_event) {
                    fr_mouse_event_t enter_evt = mevt;
                    enter_evt.event_type = FR_MOUSE_EVENT_ENTER;
                    hit_widget->handle_event(hit_widget, &enter_evt);
                }
            }

            /* 点击事件: 根据 focus strategy 决定是否转移焦点 */
            if (mevt.event_type == FR_MOUSE_EVENT_PRESS &&
                (mevt.button_changed & FR_MOUSE_LEFT)) {
                if (g_input.focus_strategy == FR_FOCUS_CLICK_TO_FOCUS ||
                    g_input.focus_strategy == FR_FOCUS_STRONG) {
                    fr_input_set_focus(hit_widget);
                }

                /* 触发拖动检测开始 */
                g_input.is_dragging = 1;
                g_input.drag_start_x = mevt.x;
                g_input.drag_start_y = mevt.y;
            }

            if (mevt.event_type == FR_MOUSE_EVENT_RELEASE) {
                g_input.is_dragging = 0;
            }

            /* 将事件分发给命中的控件 */
            if (hit_widget->handle_event) {
                hit_widget->handle_event(hit_widget, &mevt);
            }

            /* 触发控件的 on_click 回调 */
            if (mevt.event_type == FR_MOUSE_EVENT_PRESS &&
                (mevt.button_changed & FR_MOUSE_LEFT) &&
                hit_widget->on_click) {
                hit_widget->on_click(hit_widget, hit_widget->user_data);
            }
        }
    }

    /* ---- 处理键盘事件 ---- */
    fr_key_event_t kevt;
    while (fr_input_pop_key_event(&kevt)) {
        /* 键盘事件发送到当前焦点控件 */
        fr_widget_t *target = g_input.focus_widget ?
                              g_input.focus_widget : root;

        if (target && target->handle_event) {
            target->handle_event(target, &kevt);
        }

        /* 触发 on_key 回调 */
        if (target && target->on_key) {
            target->on_key(target, target->user_data);
        }

        /* Tab 键: 焦点切换 (简化实现) */
        if (kevt.scancode == FR_KEY_TAB &&
            kevt.event_type == FR_KEY_EVENT_PRESS) {
            /* TODO: 实现焦点遍历逻辑 */
            (void)0;
        }
    }

    /* ---- 处理手势事件 ---- */
    const fr_gesture_event_t *gest = fr_input_get_last_gesture();
    if (gest && gest->gesture_type != 0) {
        fr_widget_t *gesture_target = g_input.hover_widget ?
                                       g_input.hover_widget : g_input.focus_widget;
        if (gesture_target && gesture_target->handle_event) {
            gesture_target->handle_event(gesture_target, (void *)gest);
        }
        /* 手势处理后清除, 避免重复分发 */
        g_input.last_gesture.gesture_type = 0;
    }

    (void)ctx;
}

/*
 * fr_input_flush_queues - 清空所有待处理事件
 */
void fr_input_flush_queues(void) {
    g_input.key_head = g_input.key_tail;
    g_input.mouse_head = g_input.mouse_tail;
}
