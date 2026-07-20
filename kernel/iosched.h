#ifndef IOSCHED_H
#define IOSCHED_H

#include "stdint.h"

#define IOSCHED_NAME_LEN 16

struct bio;

typedef enum {
    IOSCHED_NOOP = 0,
    IOSCHED_DEADLINE = 1,
    IOSCHED_CFQ = 2,
    IOSCHED_AS = 3,
    IOSCHED_MAX
} iosched_algo_t;

struct io_request {
    struct io_request *next;
    uint64_t sector;
    uint32_t nr_sectors;
    uint8_t  is_write;
    uint32_t pid;
    uint64_t enqueue_time;
    uint64_t deadline;
    void    *buffer;
    int      status;
};

struct iosched_queue;

typedef int (*iosched_enqueue_func)(struct iosched_queue *q, struct io_request *req);
typedef struct io_request *(*iosched_dispatch_func)(struct iosched_queue *q);
typedef void (*iosched_completed_func)(struct iosched_queue *q, struct io_request *req);
typedef int (*iosched_init_func)(struct iosched_queue *q);
typedef void (*iosched_exit_func)(struct iosched_queue *q);

struct iosched_policy {
    char name[IOSCHED_NAME_LEN];
    iosched_algo_t algo;
    iosched_init_func init;
    iosched_exit_func exit;
    iosched_enqueue_func enqueue;
    iosched_dispatch_func dispatch;
    iosched_completed_func completed;
    uint32_t fifo_expire_read;
    uint32_t fifo_expire_write;
    uint32_t fifo_batch;
    const char *description;
};

struct iosched_queue {
    struct iosched_queue *next;
    struct iosched_policy *policy;
    void *private_data;
    struct io_request *head;
    struct io_request *tail;
    uint32_t queued;
    uint32_t dispatched;
    uint32_t merged;
    uint8_t  is_sync;
    uint8_t  plugged;
    char name[32];
};

int iosched_init(void);
struct iosched_queue *iosched_alloc_queue(const char *name);
void iosched_free_queue(struct iosched_queue *q);
int iosched_set_policy(struct iosched_queue *q, iosched_algo_t algo);
int iosched_enqueue(struct iosched_queue *q, struct io_request *req);
struct io_request *iosched_dispatch(struct iosched_queue *q);
void iosched_request_completed(struct iosched_queue *q, struct io_request *req);
struct io_request *iosched_alloc_request(void);
void iosched_free_request(struct io_request *req);
void iosched_plug(struct iosched_queue *q);
void iosched_unplug(struct iosched_queue *q);
const char *iosched_get_algo_name(iosched_algo_t algo);
void iosched_print_stats(struct iosched_queue *q);
void iosched_print_all_policies(void);

#endif
