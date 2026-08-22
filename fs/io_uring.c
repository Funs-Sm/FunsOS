/*
 * fs/io_uring.c - implements the kernel-side slice of io_uring.
 *
 * Submission ring semantics:
 *   - sq_head is the *next* slot the kernel must consume.
 *   - sq_tail is the *next* slot the user will produce into.
 *   - Until the kernel catches up, the user can keep posting.
 *
 * Completion ring semantics:
 *   - cq_head is the *next* slot the user must consume.
 *   - cq_tail is the *next* slot the kernel will produce into.
 *
 * For each SQE we currently apply the operation immediately inside
 * io_uring_submit() (synchronous semantics hidden behind the async
 * ring API).  True async dispatch via a worker thread is left for a
 * follow-up PR.
 */
#include "io_uring.h"
#include "vfs.h"
#include "kheap.h"
#include "string.h"
#include "stdio.h"
#include "klog.h"
#include "errno.h"

static io_uring_stats_t g_stats;

void io_uring_get_stats(io_uring_stats_t *out)
{
    if (!out) return;
    *out = g_stats;
}

void io_uring_reset_stats(void)
{
    memset(&g_stats, 0, sizeof(g_stats));
}

int io_uring_init(io_uring_t *r)
{
    if (!r) return -EINVAL;
    memset(r, 0, sizeof(*r));
    g_stats.ring_opens++;
    return 0;
}

static int push_cq(io_uring_t *r, uint32_t data, int32_t result)
{
    if (!r) return -EINVAL;
    uint32_t slot = r->cq_tail % IO_URING_ENTRIES;
    r->cq[slot].data = data;
    r->cq[slot].result = result;
    r->cq[slot].flags = 0;
    r->cq[slot].reserved = 0;
    r->cq_tail++;
    g_stats.completed++;
    return 0;
}

static int open_file(const char *path, uint32_t flags, file_t **out)
{
    return vfs_open(path, flags, out);
}

int io_uring_submit(io_uring_t *r, const io_uring_sqe_t *sqe)
{
    if (!r || !sqe) return -EINVAL;
    g_stats.submitted++;
    /* Stash into SQ slot. */
    if (r->sq_tail - r->sq_head >= IO_URING_ENTRIES) return -EBUSY;
    uint32_t slot = r->sq_tail % IO_URING_ENTRIES;
    r->sq[slot] = *sqe;
    r->sq_tail++;

    int32_t res = -ENOSYS;
    switch (sqe->opcode) {
    case IORING_OP_NOP:
        res = 0;
        break;

    case IORING_OP_READ: {
        file_t *f = NULL;
        /* fd-index indirection: we treat sqe->fd as our internal file
         * descriptor pool.  Look up the file via vfs_open because the
         * user-space fd table doesn't exist yet. */
        extern file_t *io_uring_get_file(uint16_t fd);
        f = io_uring_get_file(sqe->fd);
        if (!f) { res = -EBADF; break; }
        void *buf = (void *)(uintptr_t)sqe->buf_addr;
        res = vfs_read(f, buf, sqe->len);
        if (res > 0) g_stats.read_bytes += (uint32_t)res;
        break;
    }

    case IORING_OP_WRITE: {
        file_t *f = NULL;
        extern file_t *io_uring_get_file(uint16_t fd);
        f = io_uring_get_file(sqe->fd);
        if (!f) { res = -EBADF; break; }
        const void *buf = (const void *)(uintptr_t)sqe->buf_addr;
        res = vfs_write(f, buf, sqe->len);
        if (res > 0) g_stats.write_bytes += (uint32_t)res;
        break;
    }

    case IORING_OP_FSYNC: {
        file_t *f = NULL;
        extern file_t *io_uring_get_file(uint16_t fd);
        f = io_uring_get_file(sqe->fd);
        if (!f) { res = -EBADF; break; }
        extern int32_t vfs_fsync(file_t *f);
        res = vfs_fsync(f);
        break;
    }

    case IORING_OP_CLOSE: {
        extern file_t *io_uring_get_file(uint16_t fd);
        file_t *f = io_uring_get_file(sqe->fd);
        if (!f) { res = -EBADF; break; }
        res = vfs_close(f);
        if (res == 0) g_stats.ring_closes++;
        break;
    }

    case IORING_OP_OPENAT: {
        const char *path = (const char *)(uintptr_t)sqe->buf_addr;
        file_t *f = NULL;
        res = open_file(path, sqe->off, &f);
        break;
    }

    default:
        res = -EINVAL;
    }

    push_cq(r, sqe->data, res);
    return 0;
}

int io_uring_poll(io_uring_t *r, io_uring_cqe_t *out)
{
    if (!r || !out) return -EINVAL;
    if (r->cq_head == r->cq_tail) return -EAGAIN;
    *out = r->cq[r->cq_head % IO_URING_ENTRIES];
    r->cq_head++;
    return 0;
}

int io_uring_wait(io_uring_t *r, io_uring_cqe_t *out, uint32_t max_iters)
{
    if (!r || !out) return -EINVAL;
    for (uint32_t i = 0; i < max_iters && r->cq_head == r->cq_tail; i++) {
        /* Tiny busy-wait.  Real implementations yield to scheduler. */
        for (volatile int j = 0; j < 256; j++) ;
    }
    if (r->cq_head == r->cq_tail) return -EAGAIN;
    *out = r->cq[r->cq_head % IO_URING_ENTRIES];
    r->cq_head++;
    return 0;
}

int io_uring_pending(const io_uring_t *r)
{
    if (!r) return 0;
    return (int)(r->cq_tail - r->cq_head);
}

/* Stub for the per-ring file-table indirection.  Real
 * implementations will pass in a populated table when initialising
 * the ring.  For now we treat every fd as "current process's first
 * open file" so that smoke tests work. */
static file_t *g_stub_file;

void io_uring_set_stub_file(file_t *f) { g_stub_file = f; }

file_t *io_uring_get_file(uint16_t fd)
{
    (void)fd;
    return g_stub_file;
}
