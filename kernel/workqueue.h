#ifndef WORKQUEUE_H
#define WORKQUEUE_H

#include "stdint.h"
#include "stddef.h"

#define WQ_NAME_LEN 32
#define WQ_MAX_WORKS 256
#define WQ_MAX_QUEUES 16

struct work_struct;
typedef void (*work_func_t)(struct work_struct *work);

struct work_struct {
    struct work_struct *next;
    work_func_t func;
    unsigned long data;
    uint64_t scheduled_at;
    uint8_t pending;
    uint8_t ctor;
};

struct delayed_work {
    struct work_struct work;
    uint64_t delay_ticks;
};

struct workqueue_struct;
typedef void (*wq_thread_fn_t)(struct workqueue_struct *wq);

struct workqueue_struct {
    char name[WQ_NAME_LEN];
    struct work_struct *work_list;
    struct work_struct *work_tail;
    uint32_t pending;
    uint32_t processed;
    uint8_t running;
    uint8_t singlethread;
    uint8_t freezable;
    uint8_t rt;
    wq_thread_fn_t thread_fn;
};

int workqueue_init(void);
struct workqueue_struct *create_workqueue(const char *name);
struct workqueue_struct *create_singlethread_workqueue(const char *name);
void destroy_workqueue(struct workqueue_struct *wq);

int queue_work(struct workqueue_struct *wq, struct work_struct *work);
int queue_delayed_work(struct workqueue_struct *wq, struct delayed_work *dwork, uint64_t delay);
int cancel_work(struct work_struct *work);
int cancel_delayed_work(struct delayed_work *dwork);
int flush_workqueue(struct workqueue_struct *wq);

void INIT_WORK(struct work_struct *work, work_func_t func);
void INIT_DELAYED_WORK(struct delayed_work *dwork, work_func_t func);

int work_pending(struct work_struct *work);
void process_one_work(struct workqueue_struct *wq);
void workqueue_drain(struct workqueue_struct *wq);
uint32_t workqueue_pending(struct workqueue_struct *wq);
void workqueue_print_stats(void);

extern struct workqueue_struct *system_wq;
extern struct workqueue_struct *system_long_wq;
extern struct workqueue_struct *system_unbound_wq;
extern struct workqueue_struct *system_highpri_wq;

#endif
