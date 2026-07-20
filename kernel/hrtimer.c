#include "hrtimer.h"
#include "kheap.h"
#include "string.h"
#include "timer.h"
#include "klog.h"
#include "sync.h"
#include "stdio.h"

static struct hrtimer hrtimer_pool[HRTIMER_MAX_TIMERS];
static struct timer_list timer_pool[HRTIMER_MAX_TIMERS];
static struct hrtimer *hrtimer_list;
static struct timer_list *timer_list_head;
static uint32_t hrtimer_count;
static uint32_t timer_count;
static uint64_t hrtimer_ticks_processed;
static uint32_t hrtimer_fired_count;
static uint32_t timer_fired_count;
static uint8_t hrtimer_initialized = 0;
static spinlock_t hrtimer_lock;

static void hrtimer_remove_list(struct hrtimer *timer) {
    struct hrtimer **prev = &hrtimer_list;
    while (*prev && *prev != timer) prev = &(*prev)->next;
    if (*prev) *prev = timer->next;
}

static void hrtimer_insert_sorted(struct hrtimer *timer) {
    struct hrtimer **prev = &hrtimer_list;
    while (*prev && (*prev)->expire_time < timer->expire_time) {
        prev = &(*prev)->next;
    }
    timer->next = *prev;
    *prev = timer;
}

static void timer_remove_list(struct timer_list *timer) {
    struct timer_list **prev = &timer_list_head;
    while (*prev && *prev != timer) prev = &(*prev)->next;
    if (*prev) *prev = timer->next;
}

static void timer_insert_sorted(struct timer_list *timer) {
    struct timer_list **prev = &timer_list_head;
    while (*prev && (*prev)->expires < timer->expires) {
        prev = &(*prev)->next;
    }
    timer->next = *prev;
    *prev = timer;
}

int hrtimer_init(void) {
    if (hrtimer_initialized) return 0;
    spinlock_init(&hrtimer_lock);
    memset(hrtimer_pool, 0, sizeof(hrtimer_pool));
    memset(timer_pool, 0, sizeof(timer_pool));
    hrtimer_list = NULL;
    timer_list_head = NULL;
    hrtimer_count = 0;
    timer_count = 0;
    hrtimer_ticks_processed = 0;
    hrtimer_fired_count = 0;
    timer_fired_count = 0;
    hrtimer_initialized = 1;
    klog_info("High-resolution timer subsystem initialized");
    return 0;
}

struct hrtimer *hrtimer_create(const char *name, hrtimer_callback_t callback, void *priv) {
    if (!callback || hrtimer_count >= HRTIMER_MAX_TIMERS) return NULL;
    struct hrtimer *timer = &hrtimer_pool[hrtimer_count++];
    memset(timer, 0, sizeof(*timer));
    timer->function = callback;
    timer->private_data = priv;
    timer->state = HRTIMER_INACTIVE;
    timer->name = name ? name : "hrtimer";
    return timer;
}

int hrtimer_destroy(struct hrtimer *timer) {
    if (!timer) return -1;
    hrtimer_cancel(timer);
    timer->state = HRTIMER_INACTIVE;
    return 0;
}

int hrtimer_start(struct hrtimer *timer, uint64_t time, hrtimer_mode_t mode) {
    if (!timer || !timer->function) return -1;
    spinlock_lock(&hrtimer_lock);
    if (timer->state == HRTIMER_ENQUEUED) {
        hrtimer_remove_list(timer);
    }
    timer->mode = mode;
    if (mode == HRTIMER_MODE_REL) {
        timer->expire_time = timer_get_ticks() + time;
    } else {
        timer->expire_time = time;
    }
    timer->state = HRTIMER_ENQUEUED;
    hrtimer_insert_sorted(timer);
    spinlock_unlock(&hrtimer_lock);
    return 0;
}

int hrtimer_start_range(struct hrtimer *timer, uint64_t time, uint64_t delta, hrtimer_mode_t mode) {
    (void)delta;
    return hrtimer_start(timer, time, mode);
}

int hrtimer_cancel(struct hrtimer *timer) {
    if (!timer) return -1;
    spinlock_lock(&hrtimer_lock);
    if (timer->state == HRTIMER_ENQUEUED) {
        hrtimer_remove_list(timer);
    }
    timer->state = HRTIMER_INACTIVE;
    spinlock_unlock(&hrtimer_lock);
    return 0;
}

int hrtimer_restart(struct hrtimer *timer) {
    if (!timer) return -1;
    if (timer->interval == 0) return -1;
    return hrtimer_start(timer, timer->interval, HRTIMER_MODE_REL);
}

uint64_t hrtimer_get_remaining(struct hrtimer *timer) {
    if (!timer || timer->state != HRTIMER_ENQUEUED) return 0;
    uint64_t now = timer_get_ticks();
    if (timer->expire_time <= now) return 0;
    return timer->expire_time - now;
}

int hrtimer_is_queued(struct hrtimer *timer) {
    return timer ? (timer->state == HRTIMER_ENQUEUED) : 0;
}

void legacy_init_timer(struct timer_list *timer) {
    if (!timer) return;
    memset(timer, 0, sizeof(*timer));
}

void legacy_add_timer(struct timer_list *timer) {
    if (!timer || !timer->function) return;
    spinlock_lock(&hrtimer_lock);
    timer_remove_list(timer);
    timer_insert_sorted(timer);
    spinlock_unlock(&hrtimer_lock);
}

int legacy_del_timer(struct timer_list *timer) {
    if (!timer) return -1;
    spinlock_lock(&hrtimer_lock);
    timer_remove_list(timer);
    spinlock_unlock(&hrtimer_lock);
    return 0;
}

int legacy_mod_timer(struct timer_list *timer, uint64_t expires) {
    if (!timer) return -1;
    timer->expires = expires;
    spinlock_lock(&hrtimer_lock);
    timer_remove_list(timer);
    timer_insert_sorted(timer);
    spinlock_unlock(&hrtimer_lock);
    return 0;
}

int legacy_timer_pending(struct timer_list *timer) {
    return timer ? (timer->next != NULL || timer_list_head == timer) : 0;
}

void hrtimer_run_queues(void) {
    if (!hrtimer_initialized) return;

    uint64_t now = timer_get_ticks();

    spinlock_lock(&hrtimer_lock);
    while (hrtimer_list && hrtimer_list->expire_time <= now) {
        struct hrtimer *timer = hrtimer_list;
        hrtimer_list = timer->next;
        timer->next = NULL;
        timer->state = HRTIMER_CALLBACK;
        spinlock_unlock(&hrtimer_lock);

        hrtimer_fired_count++;
        enum hrtimer_restart ret = timer->function(timer);
        if (ret == HRTIMER_RESTART_VAL && timer->interval > 0) {
            hrtimer_start(timer, timer->interval, HRTIMER_MODE_REL);
        } else {
            timer->state = HRTIMER_INACTIVE;
        }

        spinlock_lock(&hrtimer_lock);
    }

    while (timer_list_head && timer_list_head->expires <= now) {
        struct timer_list *tl = timer_list_head;
        timer_list_head = tl->next;
        tl->next = NULL;
        spinlock_unlock(&hrtimer_lock);

        timer_fired_count++;
        if (tl->function) tl->function(tl->data);

        spinlock_lock(&hrtimer_lock);
    }
    spinlock_unlock(&hrtimer_lock);

    hrtimer_ticks_processed = now;
}

uint32_t hrtimer_pending_count(void) {
    uint32_t cnt = 0;
    for (struct hrtimer *t = hrtimer_list; t; t = t->next) cnt++;
    return cnt;
}

uint32_t hrtimer_active_count(void) {
    return hrtimer_count;
}

void hrtimer_print_stats(void) {
    klog_info("=== High-Resolution Timers ===");
    klog_info("  Total hrtimers:   %u", hrtimer_count);
    klog_info("  Pending hrtimers: %u", hrtimer_pending_count());
    klog_info("  Fired:            %u", hrtimer_fired_count);
    klog_info("  Current tick:     %llu", (unsigned long long)timer_get_ticks());
}

void timer_list_print_stats(void) {
    uint32_t cnt = 0;
    for (struct timer_list *t = timer_list_head; t; t = t->next) cnt++;
    klog_info("=== Legacy Timer List ===");
    klog_info("  Pending timers:   %u", cnt);
    klog_info("  Fired:            %u", timer_fired_count);
}
