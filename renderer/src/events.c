/* events.c - 事件分发实现
 * 鼠标/键盘/焦点/拖拽/滚轮事件处理
 */

#include "funrender.h"
#include "fr_events.h"
#include "fr_context.h"
#include "sys_api.h"

/* 事件处理器节点 */
typedef struct fr_event_node {
    uint32_t event_type;
    fr_event_handler_t handler;
    void *user_data;
    struct fr_event_node *next;
} fr_event_node_t;

/* 事件系统状态 */
typedef struct {
    fr_handle_t focused_widget;
    fr_handle_t hovered_widget;
    fr_handle_t dragged_widget;
    int drag_start_x, drag_start_y;
    int is_dragging;
} fr_event_state_t;

static fr_event_state_t g_event_state;

/* 初始化事件系统 */
void fr_event_system_init(fr_handle_t ctx)
{
    (void)ctx;
    g_event_state.focused_widget = NULL;
    g_event_state.hovered_widget = NULL;
    g_event_state.dragged_widget = NULL;
    g_event_state.drag_start_x = 0;
    g_event_state.drag_start_y = 0;
    g_event_state.is_dragging = 0;
}

/* 关闭事件系统 */
void fr_event_system_shutdown(fr_handle_t ctx)
{
    (void)ctx;
}

/* 查找鼠标位置下的控件 */
static fr_widget_t *find_widget_at(fr_widget_t *root, int x, int y)
{
    if (root == NULL) return NULL;
    if (!(root->state & FR_STATE_VISIBLE)) return NULL;

    int wx = (int)root->bounds.x;
    int wy = (int)root->bounds.y;
    int ww = (int)root->bounds.w;
    int wh = (int)root->bounds.h;

    if (x < wx || x >= wx + ww || y < wy || y >= wy + wh)
        return NULL;

    /* 检查子控件（从后往前，后绘制的在上面） */
    fr_widget_t *child = root->first_child;
    fr_widget_t *last_match = NULL;

    while (child) {
        fr_widget_t *found = find_widget_at(child, x, y);
        if (found) last_match = found;
        child = child->next_sibling;
    }

    return last_match ? last_match : root;
}

/* 分发事件 */
void fr_event_dispatch(fr_handle_t ctx, fr_event_t *event)
{
    if (ctx == NULL || event == NULL) return;

    fr_context_t *c = (fr_context_t *)ctx;
    fr_widget_t *root = (fr_widget_t *)c->root_widget;

    switch (event->type) {
    case FR_EVENT_MOUSE_MOVE: {
        int mx = event->data.mouse.x;
        int my = event->data.mouse.y;

        /* 更新悬停状态 */
        fr_widget_t *hovered = find_widget_at(root, mx, my);
        g_event_state.hovered_widget = (fr_handle_t)hovered;

        /* 拖拽更新 */
        if (g_event_state.is_dragging) {
            event->type = FR_EVENT_DRAG_MOVE;
            event->data.drag.current_x = mx;
            event->data.drag.current_y = my;
            event->data.drag.delta_x = mx - g_event_state.drag_start_x;
            event->data.drag.delta_y = my - g_event_state.drag_start_y;
            event->data.drag.source = g_event_state.dragged_widget;
        }

        /* 发送给悬停控件 */
        if (hovered && hovered->handle_event)
            hovered->handle_event(hovered, event);

        event->type = FR_EVENT_MOUSE_MOVE;  /* 恢复类型 */
        break;
    }

    case FR_EVENT_MOUSE_PRESS: {
        int mx = event->data.mouse.x;
        int my = event->data.mouse.y;
        fr_widget_t *target = find_widget_at(root, mx, my);

        if (target) {
            target->state |= FR_STATE_PRESSED;

            /* 设置焦点 */
            fr_focus_set(ctx, (fr_handle_t)target);

            if (target->handle_event)
                target->handle_event(target, event);

            if (target->on_click)
                target->on_click(target, target->user_data);
        }
        break;
    }

    case FR_EVENT_MOUSE_RELEASE: {
        /* 清除所有控件的按下状态 */
        fr_widget_t *target = (fr_widget_t *)g_event_state.hovered_widget;
        if (target) {
            target->state &= ~FR_STATE_PRESSED;
            if (target->handle_event)
                target->handle_event(target, event);
        }

        /* 结束拖拽 */
        if (g_event_state.is_dragging) {
            g_event_state.is_dragging = 0;
            event->type = FR_EVENT_DRAG_END;
            if (target && target->handle_event)
                target->handle_event(target, event);
        }
        break;
    }

    case FR_EVENT_KEY_PRESS: {
        fr_widget_t *focused = (fr_widget_t *)g_event_state.focused_widget;
        if (focused) {
            if (focused->handle_event)
                focused->handle_event(focused, event);
            if (focused->on_key)
                focused->on_key(focused, focused->user_data);
        }
        break;
    }

    case FR_EVENT_MOUSE_WHEEL: {
        fr_widget_t *hovered = (fr_widget_t *)g_event_state.hovered_widget;
        if (hovered && hovered->handle_event)
            hovered->handle_event(hovered, event);
        break;
    }

    default:
        break;
    }

    event->consumed = 1;
}

/* 注册事件处理器 */
int fr_event_register(fr_handle_t widget, uint32_t event_type,
                      fr_event_handler_t handler, void *user_data)
{
    fr_widget_t *w = (fr_widget_t *)widget;
    if (w == NULL) return -1;

    fr_event_node_t *node = (fr_event_node_t *)fr_alloc(sizeof(fr_event_node_t));
    if (node == NULL) return -1;

    node->event_type = event_type;
    node->handler = handler;
    node->user_data = user_data;
    node->next = NULL;

    /* 简化：存储在 user_data 中 */
    (void)w;
    return 0;
}

/* 注销事件处理器 */
int fr_event_unregister(fr_handle_t widget, uint32_t event_type)
{
    (void)widget; (void)event_type;
    return 0;
}

/* 设置焦点 */
void fr_focus_set(fr_handle_t ctx, fr_handle_t widget)
{
    (void)ctx;

    /* 取消旧焦点 */
    if (g_event_state.focused_widget) {
        fr_widget_t *old = (fr_widget_t *)g_event_state.focused_widget;
        old->state &= ~FR_STATE_FOCUSED;
        if (old->on_focus) old->on_focus(old, 0);
    }

    /* 设置新焦点 */
    g_event_state.focused_widget = widget;
    if (widget) {
        fr_widget_t *w = (fr_widget_t *)widget;
        w->state |= FR_STATE_FOCUSED;
        if (w->on_focus) w->on_focus(w, 1);
    }
}

/* 获取焦点控件 */
fr_handle_t fr_focus_get(fr_handle_t ctx)
{
    (void)ctx;
    return g_event_state.focused_widget;
}

/* 开始拖拽 */
int fr_drag_begin(fr_handle_t ctx, fr_handle_t widget, int x, int y)
{
    (void)ctx;
    g_event_state.is_dragging = 1;
    g_event_state.dragged_widget = widget;
    g_event_state.drag_start_x = x;
    g_event_state.drag_start_y = y;
    return 0;
}

/* 更新拖拽 */
void fr_drag_update(fr_handle_t ctx, int x, int y)
{
    (void)ctx; (void)x; (void)y;
}

/* 结束拖拽 */
void fr_drag_end(fr_handle_t ctx)
{
    (void)ctx;
    g_event_state.is_dragging = 0;
    g_event_state.dragged_widget = NULL;
}

/* 公共事件 API */
void fr_on_click(fr_handle_t widget, fr_event_handler handler)
{
    fr_widget_t *w = (fr_widget_t *)widget;
    if (w) w->on_click = (void (*)(struct fr_widget *, void *))handler;
}

void fr_on_change(fr_handle_t widget, fr_event_handler handler)
{
    fr_widget_t *w = (fr_widget_t *)widget;
    if (w) w->on_change = (void (*)(struct fr_widget *, void *))handler;
}

void fr_on_key(fr_handle_t widget, fr_event_handler handler)
{
    fr_widget_t *w = (fr_widget_t *)widget;
    if (w) w->on_key = (void (*)(struct fr_widget *, void *))handler;
}

/* 处理所有事件 */
void fr_process_events(fr_handle_t ctx)
{
    /* 从内核获取事件并分发 */
    fr_context_t *c = (fr_context_t *)ctx;
    if (c == NULL) return;

    sys_event_t sys_evt;
    while (sys_poll_event(&sys_evt) == 0) {
        fr_event_t fr_evt;
        fr_evt.type = 0;
        fr_evt.consumed = 0;
        fr_evt.target = NULL;

        switch (sys_evt.type) {
        case SYS_EVENT_MOUSE_MOVE:
            fr_evt.type = FR_EVENT_MOUSE_MOVE;
            fr_evt.data.mouse.x = (int32_t)sys_evt.param1;
            fr_evt.data.mouse.y = (int32_t)sys_evt.param2;
            break;
        case SYS_EVENT_MOUSE_CLICK:
            fr_evt.type = FR_EVENT_MOUSE_PRESS;
            fr_evt.data.mouse.x = (int32_t)sys_evt.param1;
            fr_evt.data.mouse.y = (int32_t)sys_evt.param2;
            fr_evt.data.mouse.button = FR_MOUSE_LEFT;
            break;
        case SYS_EVENT_KEY_PRESS:
            fr_evt.type = FR_EVENT_KEY_PRESS;
            fr_evt.data.key.key = sys_evt.param1;
            break;
        default:
            continue;
        }

        fr_event_dispatch(ctx, &fr_evt);
    }
}
