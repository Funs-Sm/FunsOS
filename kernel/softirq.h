#ifndef SOFTIRQ_H
#define SOFTIRQ_H

#include "stdint.h"

#define HI_SOFTIRQ          0
#define TIMER_SOFTIRQ       1
#define NET_TX_SOFTIRQ      2
#define NET_RX_SOFTIRQ      3
#define BLOCK_SOFTIRQ       4
#define TASKLET_SOFTIRQ     5
#define SCHED_SOFTIRQ       6
#define HRTIMER_SOFTIRQ     7
#define RCU_SOFTIRQ         8
#define NR_SOFTIRQS         9

#define TASKLET_STATE_SCHED 0
#define TASKLET_STATE_RUN   1

struct tasklet_struct;

typedef void (*softirq_handler_t)(void);
typedef void (*tasklet_handler_t)(unsigned long data);

struct tasklet_struct {
    struct tasklet_struct *next;
    tasklet_handler_t func;
    unsigned long data;
    uint32_t count;
    uint32_t state;
};

void softirq_init(void);
void open_softirq(uint32_t nr, softirq_handler_t handler);
void raise_softirq(uint32_t nr);
void do_softirq(void);

void tasklet_init(struct tasklet_struct *t, tasklet_handler_t func, unsigned long data);
void tasklet_schedule(struct tasklet_struct *t);
void tasklet_hi_schedule(struct tasklet_struct *t);
void tasklet_kill(struct tasklet_struct *t);
void tasklet_disable(struct tasklet_struct *t);
void tasklet_enable(struct tasklet_struct *t);

void softirq_check_pending(void);
uint32_t softirq_pending_count(void);
void softirq_print_stats(void);

#endif
