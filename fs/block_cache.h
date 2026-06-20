#ifndef BLOCK_IO_SCHED_H
#define BLOCK_IO_SCHED_H

#include "stdint.h"

/* Block I/O scheduler - orders and prioritizes block device requests
 * to minimize seek time and improve throughput (elevator algorithm).
 * This is separate from the block cache (buffer.h) which handles
 * data caching. */

/* I/O request direction */
#define BIO_READ    0
#define BIO_WRITE   1

/* I/O request priority */
#define BIO_PRIO_LOW      0
#define BIO_PRIO_NORMAL   1
#define BIO_PRIO_HIGH     2
#define BIO_PRIO_SYNC     3   /* Synchronous - must complete ASAP */

/* I/O request states */
#define BIO_PENDING    0
#define BIO_IN_FLIGHT  1
#define BIO_COMPLETED  2
#define BIO_FAILED     3

/* Maximum pending requests per queue */
#define BIO_QUEUE_DEPTH  64

typedef struct bio_request {
    uint32_t sector_start;       /* Starting sector (LBA) */
    uint32_t sector_count;       /* Number of sectors */
    void     *buffer;            /* Data buffer */
    uint8_t  direction;          /* BIO_READ or BIO_WRITE */
    uint8_t  priority;           /* BIO_PRIO_* */
    uint8_t  state;              /* BIO_* state */
    uint8_t  device_id;          /* Block device identifier */
    int32_t  result;             /* Result code (0=success) */
    void     (*callback)(struct bio_request *req);  /* Completion callback */
    struct bio_request *next;    /* Linked list next */
} bio_request_t;

/* Scheduler policies */
typedef enum {
    BIO_SCHED_FIFO = 0,     /* First-In First-Out (no reordering) */
    BIO_SCHED_ELEVATOR,     /* Simple elevator (C-SCAN) */
    BIO_SCHED_DEADLINE,     /* Deadline scheduler (latency guarantee) */
} bio_sched_policy_t;

/* Per-device I/O queue */
typedef struct bio_queue {
    bio_request_t     *pending_head;   /* Pending request list */
    bio_request_t     *pending_tail;
    bio_request_t     *in_flight;      /* Currently active request */
    bio_sched_policy_t policy;         /* Scheduling policy */
    uint32_t          last_sector;     /* Last scheduled sector (for elevator) */
    uint32_t          queue_depth;     /* Current pending count */
    uint32_t          total_read_reqs;
    uint32_t          total_write_reqs;
    uint32_t          total_merged;    /* Merged request count */
} bio_queue_t;

/* Initialize the block I/O scheduler */
void bio_sched_init(void);

/* Create a per-device I/O queue */
bio_queue_t *bio_queue_create(uint8_t device_id, bio_sched_policy_t policy);

/* Submit an I/O request (non-blocking, returns immediately) */
int bio_submit(bio_queue_t *queue, bio_request_t *req);

/* Fetch the next request to dispatch (according to scheduling policy) */
bio_request_t *bio_next_request(bio_queue_t *queue);

/* Complete a request and trigger its callback */
void bio_complete(bio_request_t *req, int32_t result);

/* Merge adjacent requests in the queue (called internally) */
int bio_merge_requests(bio_queue_t *queue);

/* Get scheduler statistics */
void bio_queue_stats(bio_queue_t *queue, uint32_t *pending,
                     uint32_t *merged, uint32_t *reads, uint32_t *writes);

#endif /* BLOCK_IO_SCHED_H */
