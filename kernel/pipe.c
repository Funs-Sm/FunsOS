#include "pipe.h"
#include "sync.h"
#include "kheap.h"
#include "sched.h"
#include "process.h"
#include "stddef.h"

#define PIPE_BUFFER_SIZE 4096

int pipe_create(pipe_t **pipe) {
    *pipe = (pipe_t *)kmalloc(sizeof(pipe_t));
    if (*pipe == NULL) {
        return -1;
    }

    (*pipe)->buffer = (uint8_t *)kmalloc(PIPE_BUFFER_SIZE);
    if ((*pipe)->buffer == NULL) {
        kfree(*pipe);
        return -1;
    }

    (*pipe)->read_pos = 0;
    (*pipe)->write_pos = 0;
    (*pipe)->count = 0;

    mutex_init(&(*pipe)->mutex);
    sem_init(&(*pipe)->read_wait, 0);
    sem_init(&(*pipe)->write_wait, 0);

    (*pipe)->readers = 1;
    (*pipe)->writers = 1;

    return 0;
}

int pipe_read(pipe_t *pipe, void *buf, uint32_t count) {
    mutex_lock(&pipe->mutex);

    while (pipe->count == 0) {
        mutex_unlock(&pipe->mutex);
        sem_wait(&pipe->read_wait);
        mutex_lock(&pipe->mutex);
    }

    uint32_t to_read = count < pipe->count ? count : pipe->count;
    uint8_t *dst = (uint8_t *)buf;

    for (uint32_t i = 0; i < to_read; i++) {
        dst[i] = pipe->buffer[pipe->read_pos];
        pipe->read_pos = (pipe->read_pos + 1) % PIPE_BUFFER_SIZE;
    }

    pipe->count -= to_read;

    mutex_unlock(&pipe->mutex);
    sem_post(&pipe->write_wait);

    return (int)to_read;
}

int pipe_write(pipe_t *pipe, const void *buf, uint32_t count) {
    mutex_lock(&pipe->mutex);

    while (pipe->count >= PIPE_BUFFER_SIZE) {
        mutex_unlock(&pipe->mutex);
        sem_wait(&pipe->write_wait);
        mutex_lock(&pipe->mutex);
    }

    uint32_t available = PIPE_BUFFER_SIZE - pipe->count;
    uint32_t to_write = count < available ? count : available;
    const uint8_t *src = (const uint8_t *)buf;

    for (uint32_t i = 0; i < to_write; i++) {
        pipe->buffer[pipe->write_pos] = src[i];
        pipe->write_pos = (pipe->write_pos + 1) % PIPE_BUFFER_SIZE;
    }

    pipe->count += to_write;

    mutex_unlock(&pipe->mutex);
    sem_post(&pipe->read_wait);

    return (int)to_write;
}

void pipe_close_read(pipe_t *pipe) {
    pipe->readers--;
    if (pipe->readers == 0) {
        sem_post(&pipe->write_wait);
    }
}

void pipe_close_write(pipe_t *pipe) {
    pipe->writers--;
    if (pipe->writers == 0) {
        sem_post(&pipe->read_wait);
    }
}

void pipe_destroy(pipe_t *pipe) {
    kfree(pipe->buffer);
    kfree(pipe);
}
