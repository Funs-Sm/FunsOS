#include "kwork.h"
#include "kheap.h"
#include "string.h"
#include "timer.h"
#include "sched.h"
#include "thread.h"
#include "klog.h"

/* ============================================================
 * Kernel Workqueue Subsystem - 内核工作队列实现
 *
 * 设计：
 *   - 默认工作队列在 kwork_init() 时创建，并启动 worker 线程
 *   - worker 线程在无工作时阻塞在 sem_wait 上（高效）
 *   - 有延迟工作时改用 sched_sleep(1) 轮询（避免错过到期）
 *   - 立即队列用 FIFO；延迟队列按 schedule_tick 升序维护
 *   - 周期性工作执行后重新插入延迟队列
 * ============================================================ */

#define KWORK_NAME_MAX  32

/* 全局 ID 计数器（用于追踪工作项） */
static volatile uint32_t g_kwork_next_id = 1;

/* 默认工作队列 */
static workqueue_t g_default_wq;

/* ---- 内部辅助 ---- */

static uint64_t kwork_now_tick(void) {
    return (uint64_t)timer_get_ticks();
}

static void kwork_wq_init(workqueue_t *wq, const char *name) {
    memset(wq, 0, sizeof(*wq));
    if (name) {
        strncpy(wq->name, name, KWORK_NAME_MAX - 1);
        wq->name[KWORK_NAME_MAX - 1] = '\0';
    } else {
        strcpy(wq->name, "anon");
    }
    spinlock_init(&wq->lock);
    sem_init(&wq->sem, 0);
    wq->shutdown = 0;
    wq->worker_running = 0;
    wq->worker = NULL;
}

/* 把工作项插入立即队列尾部（调用者持锁） */
static void kwork_enqueue_immediate_locked(workqueue_t *wq, kwork_t *work) {
    work->next = NULL;
    if (wq->immediate_tail) {
        wq->immediate_tail->next = work;
    } else {
        wq->immediate_head = work;
    }
    wq->immediate_tail = work;
    wq->immediate_count++;
    wq->queued++;
    uint32_t total = wq->immediate_count + wq->delayed_count;
    if (total > wq->max_queue_depth) {
        wq->max_queue_depth = total;
    }
}

/* 从立即队列头部取出（调用者持锁） */
static kwork_t *kwork_dequeue_immediate_locked(workqueue_t *wq) {
    kwork_t *work = wq->immediate_head;
    if (!work) return NULL;
    wq->immediate_head = work->next;
    if (!wq->immediate_head) {
        wq->immediate_tail = NULL;
    }
    wq->immediate_count--;
    work->next = NULL;
    return work;
}

/* 把工作项按 schedule_tick 升序插入延迟队列（调用者持锁） */
static void kwork_enqueue_delayed_locked(workqueue_t *wq, kwork_t *work) {
    kwork_t **pp;
    work->next = NULL;
    /* 找到第一个 schedule_tick 大于本项的位置 */
    pp = &wq->delayed_head;
    while (*pp && (*pp)->schedule_tick <= work->schedule_tick) {
        pp = &(*pp)->next;
    }
    work->next = *pp;
    *pp = work;
    wq->delayed_count++;
    wq->queued++;
    uint32_t total = wq->immediate_count + wq->delayed_count;
    if (total > wq->max_queue_depth) {
        wq->max_queue_depth = total;
    }
}

/* 从延迟队列中移除指定工作项（调用者持锁） */
static int kwork_remove_delayed_locked(workqueue_t *wq, kwork_t *work) {
    kwork_t **pp = &wq->delayed_head;
    while (*pp) {
        if (*pp == work) {
            *pp = work->next;
            work->next = NULL;
            wq->delayed_count--;
            return 1;
        }
        pp = &(*pp)->next;
    }
    return 0;
}

/* 从立即队列中移除指定工作项（调用者持锁） */
static int kwork_remove_immediate_locked(workqueue_t *wq, kwork_t *work) {
    kwork_t **pp = &wq->immediate_head;
    while (*pp) {
        if (*pp == work) {
            *pp = work->next;
            if (work == wq->immediate_tail) {
                wq->immediate_tail = (kwork_t *)NULL;
                /* 重新找 tail */
                kwork_t *p = wq->immediate_head;
                while (p && p->next) p = p->next;
                wq->immediate_tail = p;
            }
            work->next = NULL;
            wq->immediate_count--;
            return 1;
        }
        pp = &(*pp)->next;
    }
    return 0;
}

/* 把到期的延迟项移到立即队列（调用者持锁） */
static void kwork_promote_due_locked(workqueue_t *wq) {
    uint64_t now = kwork_now_tick();
    while (wq->delayed_head &&
           wq->delayed_head->schedule_tick <= now) {
        kwork_t *work = wq->delayed_head;
        wq->delayed_head = work->next;
        work->next = NULL;
        wq->delayed_count--;
        /* 清除 DELAYED 标志，加入立即队列 */
        work->flags &= ~KWORK_FLAG_DELAYED;
        kwork_enqueue_immediate_locked(wq, work);
    }
}

/* ---- worker 线程 ---- */

static void kwork_worker(void *arg) {
    workqueue_t *wq = (workqueue_t *)arg;
    kwork_t *work;
    kwork_func_t func;
    void *data;

    klog_info("kwork: worker '%s' started", wq->name);

    while (!wq->shutdown) {
        /* 把到期的延迟项移到立即队列 */
        spinlock_lock(&wq->lock);
        kwork_promote_due_locked(wq);
        work = kwork_dequeue_immediate_locked(wq);
        if (work) {
            work->flags |= KWORK_FLAG_RUNNING;
        }
        spinlock_unlock(&wq->lock);

        if (work) {
            func = work->func;
            data = work->data;
            if (func) {
                /* 执行回调（不在锁内） */
                func(data);
                wq->executed++;
            } else {
                wq->errors++;
            }
            /* 处理周期性：重新插入延迟队列 */
            spinlock_lock(&wq->lock);
            work->flags &= ~KWORK_FLAG_RUNNING;
            if ((work->flags & KWORK_FLAG_PERIODIC) &&
                !(work->flags & KWORK_FLAG_CANCELLED) &&
                work->period_ms > 0) {
                work->schedule_tick = kwork_now_tick() +
                                      (uint64_t)work->period_ms;
                work->flags |= KWORK_FLAG_DELAYED;
                kwork_enqueue_delayed_locked(wq, work);
            } else if (work->flags & KWORK_FLAG_CANCELLED) {
                wq->cancelled++;
            }
            spinlock_unlock(&wq->lock);
            continue;
        }

        /* 没有立即工作可做 */
        spinlock_lock(&wq->lock);
        int has_delayed = (wq->delayed_head != NULL);
        spinlock_unlock(&wq->lock);

        if (has_delayed) {
            /* 有未到期的延迟项 - 短睡眠轮询 */
            sched_sleep(1);
        } else {
            /* 完全空闲 - 阻塞在信号量上 */
            sem_wait(&wq->sem);
        }
    }

    wq->worker_running = 0;
    klog_info("kwork: worker '%s' exiting", wq->name);
    thread_exit();
}

/* ---- 公共 API ---- */

void kwork_init_work(kwork_t *work, kwork_func_t func, void *data) {
    if (!work) return;
    memset(work, 0, sizeof(*work));
    work->func = func;
    work->data = data;
    work->flags = KWORK_FLAG_STATIC;
    work->id = g_kwork_next_id++;
}

void kwork_init(void) {
    kwork_wq_init(&g_default_wq, "system");
    g_default_wq.worker = thread_create(kwork_worker, &g_default_wq, "kworkd");
    if (g_default_wq.worker) {
        g_default_wq.worker_running = 1;
        klog_info("kwork: default workqueue initialized");
    } else {
        klog_err("kwork: failed to create worker thread");
    }
}

workqueue_t *kwork_create_wq(const char *name) {
    workqueue_t *wq = (workqueue_t *)kmalloc(sizeof(workqueue_t));
    if (!wq) return NULL;
    kwork_wq_init(wq, name);
    wq->worker = thread_create(kwork_worker, wq, wq->name);
    if (wq->worker) {
        wq->worker_running = 1;
    } else {
        kfree(wq);
        return NULL;
    }
    return wq;
}

void kwork_destroy_wq(workqueue_t *wq) {
    if (!wq || wq == &g_default_wq) return;
    /* 通知 worker 退出 */
    spinlock_lock(&wq->lock);
    wq->shutdown = 1;
    spinlock_unlock(&wq->lock);
    sem_post(&wq->sem);
    /* 等待 worker 退出（简单轮询） */
    int wait_count = 0;
    while (wq->worker_running && wait_count < 1000) {
        sched_sleep(1);
        wait_count++;
    }
    kfree(wq);
}

workqueue_t *kwork_get_default(void) {
    return &g_default_wq;
}

int kwork_queue(workqueue_t *wq, kwork_t *work) {
    if (!wq || !work || !work->func) return -1;

    spinlock_lock(&wq->lock);
    /* 如果已经在队列中，先移除 */
    kwork_remove_immediate_locked(wq, work);
    kwork_remove_delayed_locked(wq, work);
    work->flags &= ~(KWORK_FLAG_DELAYED | KWORK_FLAG_PERIODIC |
                     KWORK_FLAG_CANCELLED);
    work->period_ms = 0;
    kwork_enqueue_immediate_locked(wq, work);
    spinlock_unlock(&wq->lock);

    sem_post(&wq->sem);
    return 0;
}

int kwork_queue_work(kwork_t *work) {
    return kwork_queue(&g_default_wq, work);
}

int kwork_queue_delayed(workqueue_t *wq, kwork_t *work, uint32_t delay_ms) {
    if (!wq || !work || !work->func) return -1;

    spinlock_lock(&wq->lock);
    kwork_remove_immediate_locked(wq, work);
    kwork_remove_delayed_locked(wq, work);
    work->flags |= KWORK_FLAG_DELAYED;
    work->flags &= ~(KWORK_FLAG_PERIODIC | KWORK_FLAG_CANCELLED);
    work->period_ms = 0;
    work->schedule_tick = kwork_now_tick() + (uint64_t)delay_ms;
    kwork_enqueue_delayed_locked(wq, work);
    spinlock_unlock(&wq->lock);

    sem_post(&wq->sem);
    return 0;
}

int kwork_queue_delayed_work(kwork_t *work, uint32_t delay_ms) {
    return kwork_queue_delayed(&g_default_wq, work, delay_ms);
}

int kwork_queue_periodic(workqueue_t *wq, kwork_t *work, uint32_t period_ms) {
    if (!wq || !work || !work->func || period_ms == 0) return -1;

    spinlock_lock(&wq->lock);
    kwork_remove_immediate_locked(wq, work);
    kwork_remove_delayed_locked(wq, work);
    work->flags |= (KWORK_FLAG_DELAYED | KWORK_FLAG_PERIODIC);
    work->flags &= ~KWORK_FLAG_CANCELLED;
    work->period_ms = period_ms;
    work->schedule_tick = kwork_now_tick() + (uint64_t)period_ms;
    kwork_enqueue_delayed_locked(wq, work);
    spinlock_unlock(&wq->lock);

    sem_post(&wq->sem);
    return 0;
}

int kwork_queue_periodic_work(kwork_t *work, uint32_t period_ms) {
    return kwork_queue_periodic(&g_default_wq, work, period_ms);
}

int kwork_cancel(workqueue_t *wq, kwork_t *work) {
    int found = 0;
    if (!wq || !work) return -1;

    spinlock_lock(&wq->lock);
    /* 如果正在运行，标记取消（周期性才不会再排） */
    if (work->flags & KWORK_FLAG_RUNNING) {
        work->flags |= KWORK_FLAG_CANCELLED;
        spinlock_unlock(&wq->lock);
        return 0;
    }
    found = kwork_remove_immediate_locked(wq, work);
    if (!found) {
        found = kwork_remove_delayed_locked(wq, work);
    }
    if (found) {
        work->flags |= KWORK_FLAG_CANCELLED;
        wq->cancelled++;
    }
    spinlock_unlock(&wq->lock);
    return found ? 0 : -1;
}

int kwork_cancel_work(kwork_t *work) {
    return kwork_cancel(&g_default_wq, work);
}

void kwork_flush_wq(workqueue_t *wq) {
    if (!wq) return;
    /* 简单实现：等到立即和延迟队列都空 */
    int wait_count = 0;
    while (wait_count < 10000) {
        spinlock_lock(&wq->lock);
        uint32_t total = wq->immediate_count + wq->delayed_count;
        spinlock_unlock(&wq->lock);
        if (total == 0) break;
        sched_sleep(1);
        wait_count++;
    }
}

void kwork_flush(void) {
    kwork_flush_wq(&g_default_wq);
}

void kwork_get_stats(kwork_stats_t *stats) {
    if (!stats) return;
    memset(stats, 0, sizeof(*stats));
    workqueue_t *wq = &g_default_wq;
    spinlock_lock(&wq->lock);
    stats->queued       = wq->queued;
    stats->executed     = wq->executed;
    stats->errors       = wq->errors;
    stats->cancelled    = wq->cancelled;
    stats->max_queue_depth = wq->max_queue_depth;
    stats->immediate_pending = wq->immediate_count;
    stats->delayed_pending   = wq->delayed_count;
    stats->worker_alive = wq->worker_running;
    spinlock_unlock(&wq->lock);
}

void kwork_reset_stats(void) {
    workqueue_t *wq = &g_default_wq;
    spinlock_lock(&wq->lock);
    wq->queued = 0;
    wq->executed = 0;
    wq->errors = 0;
    wq->cancelled = 0;
    wq->max_queue_depth = 0;
    spinlock_unlock(&wq->lock);
}

void kwork_dump(workqueue_t *wq) {
    if (!wq) return;
    spinlock_lock(&wq->lock);
    klog_info("kwork: queue '%s' immediate=%u delayed=%u "
              "queued=%llu exec=%llu err=%llu cancel=%llu maxdepth=%llu",
              wq->name,
              wq->immediate_count, wq->delayed_count,
              (unsigned long long)wq->queued,
              (unsigned long long)wq->executed,
              (unsigned long long)wq->errors,
              (unsigned long long)wq->cancelled,
              (unsigned long long)wq->max_queue_depth);
    /* 列出延迟队列前 8 项 */
    {
        kwork_t *p = wq->delayed_head;
        int i = 0;
        while (p && i < 8) {
            klog_info("  delayed[%d] id=%u tick=%llu period=%u flags=0x%x",
                      i, p->id,
                      (unsigned long long)p->schedule_tick,
                      p->period_ms, p->flags);
            p = p->next;
            i++;
        }
    }
    spinlock_unlock(&wq->lock);
}
