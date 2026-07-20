#include "workqueue.h"
#include "kheap.h"
#include "string.h"
#include "timer.h"
#include "klog.h"
#include "sync.h"
#include "stdio.h"

static struct workqueue_struct wq_pool[WQ_MAX_QUEUES];
static uint8_t wq_initialized = 0;
static uint32_t wq_count;
static spinlock_t wq_lock;

struct workqueue_struct *system_wq;
struct workqueue_struct *system_long_wq;
struct workqueue_struct *system_unbound_wq;
struct workqueue_struct *system_highpri_wq;

void INIT_WORK(struct work_struct *work, work_func_t func) {
    if (!work) return;
    memset(work, 0, sizeof(*work));
    work->func = func;
    work->pending = 0;
}

void INIT_DELAYED_WORK(struct delayed_work *dwork, work_func_t func) {
    if (!dwork) return;
    memset(dwork, 0, sizeof(*dwork));
    dwork->work.func = func;
    dwork->work.pending = 0;
    dwork->delay_ticks = 0;
}

int workqueue_init(void) {
    if (wq_initialized) return 0;

    spinlock_init(&wq_lock);
    memset(wq_pool, 0, sizeof(wq_pool));
    wq_count = 0;

    system_wq = create_workqueue("events");
    system_long_wq = create_workqueue("events_long");
    system_unbound_wq = create_workqueue("events_unbound");
    system_highpri_wq = create_singlethread_workqueue("events_highpri");

    wq_initialized = 1;
    klog_info("Workqueue subsystem initialized (%u queues)", wq_count);
    return 0;
}

struct workqueue_struct *create_workqueue(const char *name) {
    if (!name) return NULL;
    if (wq_count >= WQ_MAX_QUEUES) return NULL;

    struct workqueue_struct *wq = &wq_pool[wq_count++];
    memset(wq, 0, sizeof(*wq));
    strncpy(wq->name, name, sizeof(wq->name) - 1);
    wq->work_list = NULL;
    wq->work_tail = NULL;
    wq->pending = 0;
    wq->processed = 0;
    wq->running = 1;
    wq->singlethread = 0;
    return wq;
}

struct workqueue_struct *create_singlethread_workqueue(const char *name) {
    struct workqueue_struct *wq = create_workqueue(name);
    if (wq) wq->singlethread = 1;
    return wq;
}

void destroy_workqueue(struct workqueue_struct *wq) {
    if (!wq) return;
    flush_workqueue(wq);
    wq->running = 0;
}

int queue_work(struct workqueue_struct *wq, struct work_struct *work) {
    if (!wq || !work || !work->func) return -1;
    if (work->pending) return 0;

    work->next = NULL;
    work->pending = 1;
    work->scheduled_at = timer_get_ticks();

    spinlock_lock(&wq_lock);
    if (wq->work_tail) {
        wq->work_tail->next = work;
    } else {
        wq->work_list = work;
    }
    wq->work_tail = work;
    wq->pending++;
    spinlock_unlock(&wq_lock);
    return 1;
}

int queue_delayed_work(struct workqueue_struct *wq, struct delayed_work *dwork, uint64_t delay) {
    if (!wq || !dwork || !dwork->work.func) return -1;
    dwork->delay_ticks = delay;
    return queue_work(wq, &dwork->work);
}

int cancel_work(struct work_struct *work) {
    if (!work) return -1;
    if (!work->pending) return 0;
    work->pending = 0;
    work->func = NULL;
    return 1;
}

int cancel_delayed_work(struct delayed_work *dwork) {
    if (!dwork) return -1;
    return cancel_work(&dwork->work);
}

void process_one_work(struct workqueue_struct *wq) {
    if (!wq) return;

    spinlock_lock(&wq_lock);
    struct work_struct *work = wq->work_list;
    if (!work) {
        spinlock_unlock(&wq_lock);
        return;
    }

    wq->work_list = work->next;
    if (!wq->work_list) wq->work_tail = NULL;
    wq->pending--;
    spinlock_unlock(&wq_lock);

    work->pending = 0;
    if (work->func) {
        work->func(work);
        wq->processed++;
    }
}

int flush_workqueue(struct workqueue_struct *wq) {
    if (!wq) return -1;
    while (wq->pending > 0 && wq->work_list) {
        process_one_work(wq);
    }
    return 0;
}

int work_pending(struct work_struct *work) {
    return work ? work->pending : 0;
}

uint32_t workqueue_pending(struct workqueue_struct *wq) {
    return wq ? wq->pending : 0;
}

void workqueue_drain(struct workqueue_struct *wq) {
    flush_workqueue(wq);
}

void workqueue_print_stats(void) {
    klog_info("=== Workqueue Statistics ===");
    klog_info("Total workqueues: %u", wq_count);
    for (uint32_t i = 0; i < wq_count; i++) {
        struct workqueue_struct *wq = &wq_pool[i];
        klog_info("  [%s] pending=%u processed=%u single=%s",
                 wq->name, wq->pending, wq->processed,
                 wq->singlethread ? "yes" : "no");
    }
}
