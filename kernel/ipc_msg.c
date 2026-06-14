#include "ipc_msg.h"
#include "kheap.h"
#include "sync.h"
#include "process.h"
#include "string.h"

#define MSG_MAX_QUEUES 64

static msg_queue_t *queues[MSG_MAX_QUEUES];
static mutex_t msg_lock;

void msg_init(void) {
    mutex_init(&msg_lock);
    for (int i = 0; i < MSG_MAX_QUEUES; i++) {
        queues[i] = 0;
    }
}

int msg_create(int key) {
    mutex_lock(&msg_lock);

    int slot = -1;
    for (int i = 0; i < MSG_MAX_QUEUES; i++) {
        if (queues[i] == 0) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        mutex_unlock(&msg_lock);
        return -1;
    }

    msg_queue_t *q = (msg_queue_t *)kmalloc(sizeof(msg_queue_t));
    if (!q) {
        mutex_unlock(&msg_lock);
        return -1;
    }

    q->key = key;
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    mutex_init(&q->lock);
    sem_init(&q->wait_sem, 0);

    queues[slot] = q;

    mutex_unlock(&msg_lock);
    return slot;
}

int msg_send(int qid, void *data, uint32_t size, int flags) {
    if (qid < 0 || qid >= MSG_MAX_QUEUES || !queues[qid]) return -1;
    if (size > MSG_MAX_SIZE) return -1;

    msg_queue_t *q = queues[qid];

    mutex_lock(&q->lock);

    msg_node_t *node = (msg_node_t *)kmalloc(sizeof(msg_node_t));
    if (!node) {
        mutex_unlock(&q->lock);
        return -1;
    }

    node->data = kmalloc(size);
    if (!node->data) {
        kfree(node);
        mutex_unlock(&q->lock);
        return -1;
    }

    memcpy(node->data, data, size);
    node->size = size;
    node->next = 0;

    if (q->tail) {
        q->tail->next = node;
    } else {
        q->head = node;
    }
    q->tail = node;
    q->count++;

    sem_post(&q->wait_sem);

    mutex_unlock(&q->lock);
    return 0;
}

int msg_recv(int qid, void *buf, uint32_t size, int flags) {
    if (qid < 0 || qid >= MSG_MAX_QUEUES || !queues[qid]) return -1;

    msg_queue_t *q = queues[qid];

    if (flags & MSG_NOWAIT) {
        if (sem_getvalue(&q->wait_sem) <= 0) {
            return -1;
        }
    } else {
        sem_wait(&q->wait_sem);
    }

    mutex_lock(&q->lock);

    if (!q->head) {
        mutex_unlock(&q->lock);
        return -1;
    }

    msg_node_t *node = q->head;
    q->head = node->next;
    if (!q->head) {
        q->tail = 0;
    }
    q->count--;

    uint32_t copy_size = node->size;
    if (copy_size > size) {
        copy_size = size;
    }

    memcpy(buf, node->data, copy_size);

    kfree(node->data);
    kfree(node);

    mutex_unlock(&q->lock);
    return (int)copy_size;
}

void msg_destroy(int qid) {
    if (qid < 0 || qid >= MSG_MAX_QUEUES) return;

    mutex_lock(&msg_lock);

    msg_queue_t *q = queues[qid];
    if (!q) {
        mutex_unlock(&msg_lock);
        return;
    }

    mutex_lock(&q->lock);

    msg_node_t *node = q->head;
    while (node) {
        msg_node_t *next = node->next;
        if (node->data) kfree(node->data);
        kfree(node);
        node = next;
    }

    mutex_unlock(&q->lock);
    kfree(q);
    queues[qid] = 0;

    mutex_unlock(&msg_lock);
}
