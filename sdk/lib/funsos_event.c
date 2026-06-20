/*
 * funsos_event.c - FunsOS 事件系统实现
 *
 * 提供跨模块的事件发布/订阅机制。
 * 支持同步和异步事件分发、事件过滤器、优先级队列。
 */

#include "funsos_event.h"
#include "funsos_libc.h"
#include <stddef.h>

/* ---- 监听器结构体 ---- */
typedef struct event_listener {
    event_callback_t callback;
    void            *user_data;
    uint32_t         priority;   /* 0=最高优先级 */
    uint8_t          enabled;
} event_listener_t;

/* ---- 事件类型条目（每种事件类型一个） ---- */
typedef struct event_type_entry {
    uint32_t         type;
    event_listener_t listeners[EVENT_MAX_LISTENERS_PER_TYPE];
    int              listener_count;
    uint8_t          used;
} event_type_entry_t;

/* ---- 事件队列（异步模式使用环形缓冲区） ---- */
typedef struct event_queue {
    funsos_event_t events[EVENT_QUEUE_SIZE];
    int head, tail, count;
    uint8_t overflow;
} event_queue_t;

/* ---- 延迟事件结构 ---- */
#define MAX_DELAYED_EVENTS 32
typedef struct delayed_event {
    funsos_event_t event;
    uint64_t       trigger_time;  /* 触发时间戳(ms) */
    uint8_t        pending;
} delayed_event_t;

/* ---- 过滤器条目 ---- */
typedef struct filter_entry {
    event_filter_t filter;
    void          *user_data;
    uint8_t        active;
} filter_entry_t;

/* ================================================================
 *  全局状态
 * ================================================================ */

static event_type_entry_t event_types[EVENT_MAX_TYPES];
static event_queue_t      async_queue;
static delayed_event_t    delayed_events[MAX_DELAYED_EVENTS];
static filter_entry_t     filters[EVENT_MAX_FILTERS];
static uint64_t           event_counter;
static event_stats_t      stats;
static uint8_t            system_initialized = 0;

/* ================================================================
 *  内部辅助函数
 * ================================================================ */

/*
 * 查找或创建指定事件类型的类型条目
 * 返回: 条目指针, NULL 表示已满
 */
static event_type_entry_t *find_or_create_type(uint32_t type)
{
    int i;
    event_type_entry_t *entry;
    event_type_entry_t *free_slot = NULL;

    /* 先查找是否已存在 */
    for (i = 0; i < EVENT_MAX_TYPES; i++) {
        if (event_types[i].used && event_types[i].type == type) {
            return &event_types[i];
        }
        if (!event_types[i].used && free_slot == NULL) {
            free_slot = &event_types[i];
        }
    }

    /* 不存在则创建新条目 */
    if (free_slot != NULL) {
        entry = free_slot;
        funsos_memset(entry, 0, sizeof(event_type_entry_t));
        entry->type = type;
        entry->used = 1;
        return entry;
    }

    return NULL; /* 类型表已满 */
}

/*
 * 获取当前时间戳(毫秒) - 简化实现，实际应调用系统时钟
 */
static uint64_t get_timestamp_ms(void)
{
    return event_counter++;
}

/*
 * 对监听器数组按优先级排序（插入排序，小规模数据足够高效）
 */
static void sort_listeners_by_priority(event_listener_t *arr, int count)
{
    int i, j;
    event_listener_t tmp;

    for (i = 1; i < count; i++) {
        tmp = arr[i];
        j = i - 1;
        while (j >= 0 && arr[j].priority > tmp.priority) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = tmp;
    }
}

/*
 * 运行所有过滤器检查事件是否被拦截
 * 返回: 1=事件被消费/阻止(不再分发), 0=正常通过
 */
static int run_filters(funsos_event_t *event)
{
    int i;
    int result;

    for (i = 0; i < EVENT_MAX_FILTERS; i++) {
        if (filters[i].active && filters[i].filter != NULL) {
            result = filters[i].filter(event, filters[i].user_data);
            if (result != 0) {
                /* 过滤器返回非零表示事件被消费或阻止 */
                return 1;
            }
        }
    }

    return 0; /* 所有过滤器放行 */
}

/*
 * 同步分发事件给指定类型的所有监听器
 * 返回: 实际调用的监听器数量
 */
static int dispatch_to_type(event_type_entry_t *entry, funsos_event_t *event)
{
    int i;
    int dispatched = 0;
    int result;

    if (entry == NULL || entry->listener_count <= 0)
        return 0;

    /* 按优先级从高到低遍历监听器 */
    for (i = 0; i < entry->listener_count; i++) {
        if (entry->listeners[i].enabled &&
            entry->listeners[i].callback != NULL) {

            result = entry->listeners[i].callback(event,
                                                  entry->listeners[i].user_data);
            dispatched++;

            /* 如果回调返回非零或事件已被标记为消费，停止分发 */
            if (result != 0 || (event->flags & EVENT_FLAG_CONSUMED)) {
                event->flags |= EVENT_FLAG_CONSUMED;
                break;
            }
        }
    }

    return dispatched;
}

/* ================================================================
 *  公共 API 实现
 * ================================================================ */

void event_system_init(void)
{
    int i;

    /* 清零事件类型表 */
    funsos_memset(event_types, 0, sizeof(event_types));

    /* 清零异步队列 */
    funsos_memset(&async_queue, 0, sizeof(async_queue));
    async_queue.head = 0;
    async_queue.tail = 0;
    async_queue.count = 0;
    async_queue.overflow = 0;

    /* 清零延迟事件表 */
    funsos_memset(delayed_events, 0, sizeof(delayed_events));

    /* 清零过滤器表 */
    funsos_memset(filters, 0, sizeof(filters));

    /* 重置计数器和统计 */
    event_counter = 0;
    funsos_memset(&stats, 0, sizeof(stats));

    system_initialized = 1;
}

int event_post(funsos_event_t *event)
{
    event_type_entry_t *entry;
    int dispatched;

    if (event == NULL || !system_initialized)
        return -1;

    /* 设置时间戳 */
    event->timestamp = get_timestamp_ms();
    event->flags &= ~EVENT_FLAG_QUEUED;
    event->flags |= EVENT_FLAG_CAPTURED;

    /* 更新统计 */
    stats.posted_total++;

    /* 运行过滤器 */
    if (run_filters(event)) {
        return 0; /* 被过滤器拦截 */
    }

    /* 查找该类型的监听器列表 */
    entry = find_or_create_type(event->type);

    if (entry == NULL || entry->listener_count <= 0) {
        /* 无监听器订阅此类型的事件 */
        return 0;
    }

    /* 分发事件 */
    dispatched = dispatch_to_type(entry, event);

    stats.dispatched_total += (uint64_t)dispatched;

    return dispatched;
}

int event_post_async(funsos_event_t *event)
{
    int next_tail;

    if (event == NULL || !system_initialized)
        return -1;

    /* 检查队列是否已满 */
    if (async_queue.count >= EVENT_QUEUE_SIZE) {
        async_queue.overflow = 1;
        stats.dropped_overflow++;
        return -1;
    }

    /* 设置时间戳和标志 */
    event->timestamp = get_timestamp_ms();
    event->flags |= (EVENT_FLAG_ASYNC | EVENT_FLAG_QUEUED);
    event->flags &= ~EVENT_FLAG_CONSUMED;

    /* 入队到环形缓冲区尾部 */
    next_tail = (async_queue.tail + 1) % EVENT_QUEUE_SIZE;
    async_queue.events[async_queue.tail] = *event;
    async_queue.tail = next_tail;
    async_queue.count++;

    stats.posted_total++;

    return 0;
}

int event_post_delayed(funsos_event_t *event, uint32_t delay_ms)
{
    int i;

    if (event == NULL || !system_initialized)
        return -1;

    /* 查找空闲延迟槽位 */
    for (i = 0; i < MAX_DELAYED_EVENTS; i++) {
        if (!delayed_events[i].pending) {
            delayed_events[i].event = *event;
            delayed_events[i].trigger_time = get_timestamp_ms() + delay_ms;
            delayed_events[i].pending = 1;
            stats.posted_total++;
            return 0;
        }
    }

    return -1; /* 延迟槽位已满 */
}

int event_subscribe(uint32_t type, event_callback_t cb, void *ud, uint32_t pri)
{
    event_type_entry_t *entry;

    if (cb == NULL || !system_initialized)
        return -1;

    entry = find_or_create_type(type);
    if (entry == NULL)
        return -1;

    /* 检查是否已有相同回调（防止重复注册） */
    /* 允许重复注册以支持多个 user_data 实例 */

    /* 检查监听器数量上限 */
    if (entry->listener_count >= EVENT_MAX_LISTENERS_PER_TYPE)
        return -1;

    /* 添加新的监听器 */
    entry->listeners[entry->listener_count].callback   = cb;
    entry->listeners[entry->listener_count].user_data  = ud;
    entry->listeners[entry->listener_count].priority   = pri;
    entry->listeners[entry->listener_count].enabled    = 1;
    entry->listener_count++;

    /* 按优先级排序 */
    sort_listeners_by_priority(entry->listeners, entry->listener_count);

    /* 更新统计 */
    stats.listener_counts[type % EVENT_MAX_TYPES] =
        (uint32_t)entry->listener_count;

    return 0;
}

int event_unsubscribe(uint32_t type, event_callback_t cb)
{
    event_type_entry_t *entry;
    int i, j;

    if (cb == NULL || !system_initialized)
        return -1;

    entry = find_or_create_type(type);
    if (entry == NULL || !entry->used)
        return -1;

    /* 查找并移除匹配的监听器 */
    for (i = 0; i < entry->listener_count; i++) {
        if (entry->listeners[i].callback == cb) {
            /* 将后面的元素前移覆盖 */
            for (j = i; j < entry->listener_count - 1; j++) {
                entry->listeners[j] = entry->listeners[j + 1];
            }
            entry->listener_count--;

            /* 清除末尾残留 */
            funsos_memset(&entry->listeners[entry->listener_count],
                          0, sizeof(event_listener_t));

            stats.listener_counts[type % EVENT_MAX_TYPES] =
                (uint32_t)entry->listener_count;
            return 0;
        }
    }

    return -1; /* 未找到匹配的监听器 */
}

int event_enable_listener(uint32_t type, event_callback_t cb, uint8_t en)
{
    event_type_entry_t *entry;
    int i;

    if (cb == NULL || !system_initialized)
        return -1;

    entry = find_or_create_type(type);
    if (entry == NULL || !entry->used)
        return -1;

    for (i = 0; i < entry->listener_count; i++) {
        if (entry->listeners[i].callback == cb) {
            entry->listeners[i].enabled = en ? 1 : 0;
            return 0;
        }
    }

    return -1;
}

int event_poll(funsos_event_t *out_event)
{
    funsos_event_t queued_event;
    int dispatched;

    if (out_event == NULL || !system_initialized)
        return -1;

    /* 首先检查是否有到期需要触发的延迟事件 */
    {
        int di;
        uint64_t now = get_timestamp_ms();
        for (di = 0; di < MAX_DELAYED_EVENTS; di++) {
            if (delayed_events[di].pending &&
                now >= delayed_events[di].trigger_time) {

                queued_event = delayed_events[di].event;
                delayed_events[di].pending = 0;

                /* 将延迟事件同步分发 */
                dispatched = dispatch_to_type(
                    find_or_create_type(queued_event.type), &queued_event);
                stats.dispatched_total += (uint64_t)dispatched;

                *out_event = queued_event;
                return 1;
            }
        }
    }

    /* 从异步队列中取出事件 */
    if (async_queue.count <= 0)
        return 0; /* 队列为空 */

    /* 取出队头事件 */
    *out_event = async_queue.events[async_queue.head];
    async_queue.head = (async_queue.head + 1) % EVENT_QUEUE_SIZE;
    async_queue.count--;

    out_event->flags &= ~EVENT_FLAG_QUEUED;

    /* 同步分发取出的异步事件 */
    dispatched = dispatch_to_type(
        find_or_create_type(out_event->type), out_event);
    stats.dispatched_total += (uint64_t)dispatched;

    return 1;
}

int event_peek(void)
{
    int pending_async;
    int di;
    uint64_t now;

    if (!system_initialized)
        return 0;

    pending_async = async_queue.count;

    /* 检查延迟事件中有多少已到期 */
    now = get_timestamp_ms();
    for (di = 0; di < MAX_DELAYED_EVENTS; di++) {
        if (delayed_events[di].pending && now >= delayed_events[di].trigger_time) {
            pending_async++;
        }
    }

    return pending_async;
}

int event_flush(void)
{
    int flushed = 0;
    funsos_event_t evt;

    while (event_poll(&evt) > 0) {
        flushed++;
    }

    return flushed;
}

int event_add_filter(event_filter_t filter, void *ud)
{
    int i;

    if (filter == NULL || !system_initialized)
        return -1;

    for (i = 0; i < EVENT_MAX_FILTERS; i++) {
        if (!filters[i].active) {
            filters[i].filter    = filter;
            filters[i].user_data = ud;
            filters[i].active    = 1;
            return 0;
        }
    }

    return -1; /* 过滤器槽位已满 */
}

int event_remove_filter(event_filter_t filter)
{
    int i;

    if (filter == NULL || !system_initialized)
        return -1;

    for (i = 0; i < EVENT_MAX_FILTERS; i++) {
        if (filters[i].active && filters[i].filter == filter) {
            filters[i].filter    = NULL;
            filters[i].user_data = NULL;
            filters[i].active    = 0;
            return 0;
        }
    }

    return -1; /* 未找到匹配的过滤器 */
}

event_stats_t event_get_stats(void)
{
    return stats;
}

void event_reset_stats(void)
{
    funsos_memset(&stats, 0, sizeof(stats));
}
