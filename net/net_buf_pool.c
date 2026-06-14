#include "net_buf_pool.h"
#include "kheap.h"
#include "string.h"
#include "sync.h"
#include "stddef.h"

static net_buffer_t  pool[NET_BUF_POOL_SIZE];
static net_buffer_t *free_list[NET_BUF_POOL_SIZE];
static uint32_t      free_count;
static uint32_t      in_use;
static uint32_t      overflow_count;
static spinlock_t    pool_lock;

void net_buf_pool_init(void) {
    spinlock_init(&pool_lock);
    spinlock_lock(&pool_lock);
    for (uint32_t i = 0; i < NET_BUF_POOL_SIZE; i++) {
        memset(&pool[i], 0, sizeof(net_buffer_t));
        free_list[i] = &pool[i];
    }
    free_count = NET_BUF_POOL_SIZE;
    in_use = 0;
    overflow_count = 0;
    spinlock_unlock(&pool_lock);
}

net_buffer_t *net_buf_pool_alloc(void) {
    spinlock_lock(&pool_lock);
    net_buffer_t *b = NULL;
    if (free_count > 0) {
        b = free_list[--free_count];
    }
    if (b) in_use++;
    else   overflow_count++;
    spinlock_unlock(&pool_lock);
    if (!b) {
        b = (net_buffer_t *)kmalloc(sizeof(net_buffer_t));
        if (b) {
            memset(b, 0, sizeof(*b));
            in_use++;
        }
    }
    if (b) {
        memset(b, 0, sizeof(*b));
    }
    return b;
}

void net_buf_pool_free(net_buffer_t *buf) {
    if (!buf) return;
    spinlock_lock(&pool_lock);
    /* Determine whether the buffer belongs to the pool. */
    if ((uintptr_t)buf >= (uintptr_t)&pool[0] &&
        (uintptr_t)buf <  (uintptr_t)&pool[NET_BUF_POOL_SIZE]) {
        if (free_count < NET_BUF_POOL_SIZE) {
            free_list[free_count++] = buf;
        }
        if (in_use > 0) in_use--;
    } else {
        if (in_use > 0) in_use--;
        spinlock_unlock(&pool_lock);
        kfree(buf);
        return;
    }
    spinlock_unlock(&pool_lock);
}

uint32_t net_buf_pool_available(void) {
    uint32_t c;
    spinlock_lock(&pool_lock);
    c = free_count;
    spinlock_unlock(&pool_lock);
    return c;
}

uint32_t net_buf_pool_in_use(void) {
    uint32_t c;
    spinlock_lock(&pool_lock);
    c = in_use;
    spinlock_unlock(&pool_lock);
    return c;
}

uint32_t net_buf_pool_overflow(void) {
    uint32_t c;
    spinlock_lock(&pool_lock);
    c = overflow_count;
    spinlock_unlock(&pool_lock);
    return c;
}
