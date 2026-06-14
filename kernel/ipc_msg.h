#ifndef IPC_MSG_H
#define IPC_MSG_H

#include "stdint.h"
#include "kernel_types.h"
#include "sync.h"

#define MSG_NOWAIT 0x01
#define MSG_MAX_SIZE 4096

typedef struct msg_node_t {
    void *data;
    uint32_t size;
    struct msg_node_t *next;
} msg_node_t;

typedef struct {
    int key;
    msg_node_t *head;
    msg_node_t *tail;
    uint32_t count;
    mutex_t lock;
    sem_t wait_sem;
} msg_queue_t;

void msg_init(void);
int msg_create(int key);
int msg_send(int qid, void *data, uint32_t size, int flags);
int msg_recv(int qid, void *buf, uint32_t size, int flags);
void msg_destroy(int qid);

#endif
