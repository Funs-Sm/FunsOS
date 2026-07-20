#ifndef HRTIMER_H
#define HRTIMER_H

#include "stdint.h"

#define HRTIMER_MAX_TIMERS 256

typedef enum {
    HRTIMER_INACTIVE = 0,
    HRTIMER_ENQUEUED = 1,
    HRTIMER_CALLBACK = 2,
    HRTIMER_RESTART = 3
} hrtimer_state_t;

typedef enum {
    HRTIMER_MODE_ABS = 0,
    HRTIMER_MODE_REL = 1
} hrtimer_mode_t;

struct hrtimer;
typedef enum hrtimer_restart (*hrtimer_callback_t)(struct hrtimer *timer);

enum hrtimer_restart {
    HRTIMER_NORESTART = 0,
    HRTIMER_RESTART_VAL = 1
};

struct hrtimer {
    struct hrtimer *next;
    hrtimer_callback_t function;
    uint64_t expire_time;
    uint64_t interval;
    hrtimer_state_t state;
    hrtimer_mode_t mode;
    void *private_data;
    const char *name;
};

struct timer_list;
typedef void (*timer_callback_t)(uint64_t data);

struct timer_list {
    struct timer_list *next;
    timer_callback_t function;
    uint64_t data;
    uint64_t expires;
    uint32_t flags;
    const char *name;
};

int hrtimer_init(void);

struct hrtimer *hrtimer_create(const char *name, hrtimer_callback_t callback, void *priv);
int hrtimer_destroy(struct hrtimer *timer);

int hrtimer_start(struct hrtimer *timer, uint64_t time, hrtimer_mode_t mode);
int hrtimer_start_range(struct hrtimer *timer, uint64_t time, uint64_t delta, hrtimer_mode_t mode);
int hrtimer_cancel(struct hrtimer *timer);
int hrtimer_restart(struct hrtimer *timer);
uint64_t hrtimer_get_remaining(struct hrtimer *timer);
int hrtimer_is_queued(struct hrtimer *timer);

void legacy_init_timer(struct timer_list *timer);
void legacy_add_timer(struct timer_list *timer);
int legacy_del_timer(struct timer_list *timer);
int legacy_mod_timer(struct timer_list *timer, uint64_t expires);
int legacy_timer_pending(struct timer_list *timer);

void hrtimer_run_queues(void);
uint32_t hrtimer_pending_count(void);
uint32_t hrtimer_active_count(void);
void hrtimer_print_stats(void);
void timer_list_print_stats(void);

#endif
