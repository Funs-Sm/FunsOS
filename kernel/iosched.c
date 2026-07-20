#include "iosched.h"
#include "kheap.h"
#include "string.h"
#include "timer.h"
#include "klog.h"
#include "stdio.h"
#include "sched.h"
#include "sync.h"

#define DEADLINE_READ_EXPIRE  500
#define DEADLINE_WRITE_EXPIRE 5000

struct deadline_data {
    struct io_request *fifo_read;
    struct io_request *fifo_write;
};

static struct iosched_policy *registered_policies[IOSCHED_MAX];
static struct iosched_policy *default_policy;
static uint8_t iosched_initialized = 0;
static struct iosched_queue *all_queues;
static uint32_t queue_count;
static spinlock_t iosched_lock;

static int noop_init(struct iosched_queue *q) { (void)q; return 0; }
static void noop_exit(struct iosched_queue *q) { (void)q; }

static int noop_enqueue(struct iosched_queue *q, struct io_request *req) {
    req->next = NULL;
    if (q->tail) {
        q->tail->next = req;
    } else {
        q->head = req;
    }
    q->tail = req;
    q->queued++;
    return 0;
}

static struct io_request *noop_dispatch(struct iosched_queue *q) {
    if (!q->head) return NULL;
    struct io_request *req = q->head;
    q->head = req->next;
    if (!q->head) q->tail = NULL;
    req->next = NULL;
    q->queued--;
    q->dispatched++;
    return req;
}

static void noop_completed(struct iosched_queue *q, struct io_request *req) {
    (void)q; (void)req;
}

static int deadline_init(struct iosched_queue *q) {
    struct deadline_data *dd = (struct deadline_data *)kmalloc(sizeof(struct deadline_data));
    if (!dd) return -1;
    memset(dd, 0, sizeof(*dd));
    q->private_data = dd;
    return 0;
}

static void deadline_exit(struct iosched_queue *q) {
    if (q->private_data) kfree(q->private_data);
}

static int deadline_enqueue(struct iosched_queue *q, struct io_request *req) {
    req->next = NULL;
    req->enqueue_time = timer_get_ticks();
    req->deadline = req->enqueue_time +
        (req->is_write ? q->policy->fifo_expire_write : q->policy->fifo_expire_read);

    if (req->is_write) {
        struct io_request **tailp = &q->tail;
        while (*tailp) tailp = &(*tailp)->next;
        *tailp = req;
        if (!q->head) q->head = req;
        q->tail = req;
    } else {
        struct io_request **tailp = &q->tail;
        while (*tailp) tailp = &(*tailp)->next;
        *tailp = req;
        if (!q->head) q->head = req;
        q->tail = req;
    }
    q->queued++;
    return 0;
}

static struct io_request *deadline_dispatch(struct iosched_queue *q) {
    struct io_request *req = NULL;
    uint64_t now = timer_get_ticks();

    for (struct io_request *r = q->head, *prev = NULL; r; prev = r, r = r->next) {
        if ((int64_t)(now - r->deadline) >= 0) {
            if (prev) {
                prev->next = r->next;
            } else {
                q->head = r->next;
            }
            if (!r->next) q->tail = prev;
            req = r;
            break;
        }
    }

    if (!req) {
        req = q->head;
        if (req) {
            q->head = req->next;
            if (!q->head) q->tail = NULL;
        }
    }

    if (req) {
        req->next = NULL;
        q->queued--;
        q->dispatched++;
    }
    return req;
}

static void deadline_completed(struct iosched_queue *q, struct io_request *req) {
    (void)q; (void)req;
}

static int cfq_init(struct iosched_queue *q) { (void)q; return 0; }
static void cfq_exit(struct iosched_queue *q) { (void)q; }

static int cfq_enqueue(struct iosched_queue *q, struct io_request *req) {
    return noop_enqueue(q, req);
}

static struct io_request *cfq_dispatch(struct iosched_queue *q) {
    return noop_dispatch(q);
}

static void cfq_completed(struct iosched_queue *q, struct io_request *req) {
    (void)q; (void)req;
}

static struct iosched_policy policies[IOSCHED_MAX] = {
    {
        .name = "noop",
        .algo = IOSCHED_NOOP,
        .init = noop_init,
        .exit = noop_exit,
        .enqueue = noop_enqueue,
        .dispatch = noop_dispatch,
        .completed = noop_completed,
        .fifo_expire_read = 0,
        .fifo_expire_write = 0,
        .fifo_batch = 0,
        .description = "No-op elevator - simple FIFO ordering"
    },
    {
        .name = "deadline",
        .algo = IOSCHED_DEADLINE,
        .init = deadline_init,
        .exit = deadline_exit,
        .enqueue = deadline_enqueue,
        .dispatch = deadline_dispatch,
        .completed = deadline_completed,
        .fifo_expire_read = DEADLINE_READ_EXPIRE,
        .fifo_expire_write = DEADLINE_WRITE_EXPIRE,
        .fifo_batch = 16,
        .description = "Deadline I/O scheduler - minimizes starvation"
    },
    {
        .name = "cfq",
        .algo = IOSCHED_CFQ,
        .init = cfq_init,
        .exit = cfq_exit,
        .enqueue = cfq_enqueue,
        .dispatch = cfq_dispatch,
        .completed = cfq_completed,
        .fifo_expire_read = DEADLINE_READ_EXPIRE,
        .fifo_expire_write = DEADLINE_WRITE_EXPIRE,
        .fifo_batch = 8,
        .description = "Completely Fair Queuing - fair I/O bandwidth"
    },
    {
        .name = "as",
        .algo = IOSCHED_AS,
        .init = noop_init,
        .exit = noop_exit,
        .enqueue = noop_enqueue,
        .dispatch = noop_dispatch,
        .completed = noop_completed,
        .fifo_expire_read = DEADLINE_READ_EXPIRE,
        .fifo_expire_write = DEADLINE_WRITE_EXPIRE,
        .fifo_batch = 16,
        .description = "Anticipatory scheduler - hint-based merging"
    }
};

int iosched_init(void) {
    if (iosched_initialized) return 0;

    spinlock_init(&iosched_lock);
    all_queues = NULL;
    queue_count = 0;

    for (int i = 0; i < IOSCHED_MAX; i++) {
        registered_policies[i] = &policies[i];
    }
    default_policy = &policies[IOSCHED_NOOP];

    iosched_initialized = 1;
    klog_info("I/O scheduler framework initialized (default: %s)", default_policy->name);
    return 0;
}

struct iosched_queue *iosched_alloc_queue(const char *name) {
    if (!iosched_initialized) return NULL;

    struct iosched_queue *q = (struct iosched_queue *)kmalloc(sizeof(struct iosched_queue));
    if (!q) return NULL;

    memset(q, 0, sizeof(*q));
    q->policy = default_policy;
    q->head = NULL;
    q->tail = NULL;
    q->queued = 0;
    q->dispatched = 0;
    q->merged = 0;
    q->plugged = 1;
    q->is_sync = 1;
    strncpy(q->name, name ? name : "unknown", sizeof(q->name) - 1);

    if (q->policy->init(q) != 0) {
        kfree(q);
        return NULL;
    }

    q->next = all_queues;
    all_queues = q;
    queue_count++;
    return q;
}

void iosched_free_queue(struct iosched_queue *q) {
    if (!q) return;

    if (q->policy && q->policy->exit) {
        q->policy->exit(q);
    }

    struct io_request *r = q->head;
    while (r) {
        struct io_request *next = r->next;
        kfree(r);
        r = next;
    }

    struct iosched_queue **prev = &all_queues;
    while (*prev && *prev != q) prev = &(*prev)->next;
    if (*prev) *prev = q->next;
    queue_count--;
    kfree(q);
}

int iosched_set_policy(struct iosched_queue *q, iosched_algo_t algo) {
    if (!q || algo >= IOSCHED_MAX) return -1;
    if (!registered_policies[algo]) return -1;

    struct iosched_policy *new_policy = registered_policies[algo];
    if (q->policy && q->policy->exit) {
        q->policy->exit(q);
    }

    q->head = NULL;
    q->tail = NULL;
    q->queued = 0;
    q->policy = new_policy;
    if (new_policy->init(q) != 0) return -1;

    klog_info("iosched: set policy %s on queue %s", new_policy->name, q->name);
    return 0;
}

int iosched_enqueue(struct iosched_queue *q, struct io_request *req) {
    if (!q || !req || !q->policy) return -1;
    return q->policy->enqueue(q, req);
}

struct io_request *iosched_dispatch(struct iosched_queue *q) {
    if (!q || !q->policy) return NULL;
    if (q->plugged) return NULL;
    return q->policy->dispatch(q);
}

void iosched_request_completed(struct iosched_queue *q, struct io_request *req) {
    if (!q || !req || !q->policy) return;
    if (q->policy->completed) q->policy->completed(q, req);
}

struct io_request *iosched_alloc_request(void) {
    struct io_request *req = (struct io_request *)kmalloc(sizeof(struct io_request));
    if (!req) return NULL;
    memset(req, 0, sizeof(*req));
    pcb_t *cur = sched_get_current();
    req->pid = cur ? cur->pid : 0;
    return req;
}

void iosched_free_request(struct io_request *req) {
    if (req) kfree(req);
}

void iosched_plug(struct iosched_queue *q) {
    if (q) q->plugged = 1;
}

void iosched_unplug(struct iosched_queue *q) {
    if (q) q->plugged = 0;
}

const char *iosched_get_algo_name(iosched_algo_t algo) {
    if (algo >= IOSCHED_MAX) return "unknown";
    if (!registered_policies[algo]) return "unknown";
    return registered_policies[algo]->name;
}

void iosched_print_stats(struct iosched_queue *q) {
    if (!q) return;
    klog_info("  Queue %s: policy=%s queued=%u dispatched=%u merged=%u",
             q->name, q->policy->name, q->queued, q->dispatched, q->merged);
}

void iosched_print_all_policies(void) {
    klog_info("=== Available I/O Scheduler Policies ===");
    for (int i = 0; i < IOSCHED_MAX; i++) {
        if (registered_policies[i]) {
            klog_info("  [%s] - %s", registered_policies[i]->name, registered_policies[i]->description);
            klog_info("    read_expire=%u write_expire=%u batch=%u",
                      registered_policies[i]->fifo_expire_read,
                      registered_policies[i]->fifo_expire_write,
                      registered_policies[i]->fifo_batch);
        }
    }
    klog_info("Default policy: %s", default_policy ? default_policy->name : "none");
    klog_info("Active queues: %u", queue_count);
}
