#ifndef NET_BUF_POOL_H
#define NET_BUF_POOL_H

#include "net.h"
#include "stdint.h"

/* Lock-free net buffer pool.
 *
 * Pre-allocates a fixed ring of net_buffer_t objects so the hot path
 * (rx/tx) does not have to call kmalloc for every packet.  Buffers
 * not in the pool are served from a small overflow heap list.  The
 * pool is thread-safe via a simple spinlock around the free list. */

#define NET_BUF_POOL_SIZE 128

void         net_buf_pool_init(void);
net_buffer_t *net_buf_pool_alloc(void);
void         net_buf_pool_free(net_buffer_t *buf);
uint32_t     net_buf_pool_available(void);
uint32_t     net_buf_pool_in_use(void);
uint32_t     net_buf_pool_overflow(void);

#endif
