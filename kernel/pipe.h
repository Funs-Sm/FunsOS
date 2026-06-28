#ifndef PIPE_H
#define PIPE_H

#include "sync.h"
#include "stdint.h"

#define PIPE_BUFFER_SIZE 4096

typedef struct {
    uint8_t *buffer;
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t count;
    mutex_t mutex;
    sem_t read_wait;
    sem_t write_wait;
    int32_t readers;
    int32_t writers;
} pipe_t;

static inline int pipe_is_empty(pipe_t *pipe) {
    return pipe->count == 0;
}

static inline int pipe_is_full(pipe_t *pipe) {
    return pipe->count >= PIPE_BUFFER_SIZE;
}

int pipe_create(pipe_t **pipe);
int pipe_read(pipe_t *pipe, void *buf, uint32_t count);
int pipe_write(pipe_t *pipe, const void *buf, uint32_t count);
void pipe_close_read(pipe_t *pipe);
void pipe_close_write(pipe_t *pipe);
void pipe_destroy(pipe_t *pipe);

#endif
