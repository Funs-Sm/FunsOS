/*
 * fs/io_uring.h - asynchronous I/O submission / completion ring.
 *
 * io_uring (Linux 5.1+) is a fast, mmap-free async-IO interface: the
 * kernel and a user-mode process share a ring of submission entries
 * and a ring of completion entries.  The producer (user) writes SQE
 * headers, the consumer (kernel) writes CQE entries on completion.
 *
 * FunsCore implements the minimal slice needed by libasan-style tools
 * and our shell utilities:
 *   - Single-thread submission ring (no SQPOLL)
 *   - Single completion ring
 *   - Read, write, fsync, close, nop
 *   - Submit/await ring pair by polling completion (no IRQ vector)
 */
#ifndef FS_IO_URING_H
#define FS_IO_URING_H

#include "stdint.h"
#include "stdbool.h"
#include "../fs/file_desc.h"

#define IO_URING_ENTRIES      32
#define IO_URING_MAX_FILE     256

enum {
    IORING_OP_NOP    = 0,
    IORING_OP_READ   = 1,
    IORING_OP_WRITE  = 2,
    IORING_OP_FSYNC  = 3,
    IORING_OP_CLOSE  = 4,
    IORING_OP_OPENAT = 5,
    IORING_OP_MAX    = 6,
};

typedef struct io_uring_sqe {
    uint8_t  opcode;
    uint8_t  flags;
    uint16_t fd;
    uint32_t off;       /* offset or open flags */
    uint32_t len;
    uint32_t data;      /* user cookie */
    uint32_t buf_addr;  /* kernel-side pointer or path ptr */
} io_uring_sqe_t;

typedef struct io_uring_cqe {
    uint32_t data;
    int32_t  result;    /* bytes done or -errno */
    uint32_t flags;
    uint32_t reserved;
} io_uring_cqe_t;

typedef struct io_uring {
    uint32_t sq_head;
    uint32_t sq_tail;
    uint32_t cq_head;
    uint32_t cq_tail;
    io_uring_sqe_t sq[IO_URING_ENTRIES];
    io_uring_cqe_t cq[IO_URING_ENTRIES];
} io_uring_t;

int  io_uring_init(io_uring_t *r);
int  io_uring_submit(io_uring_t *r, const io_uring_sqe_t *sqe);
int  io_uring_poll(io_uring_t *r, io_uring_cqe_t *out);
int  io_uring_wait(io_uring_t *r, io_uring_cqe_t *out, uint32_t max_iters);
int  io_uring_pending(const io_uring_t *r);

typedef struct io_uring_stats {
    uint64_t submitted;
    uint64_t completed;
    uint64_t read_bytes;
    uint64_t write_bytes;
    uint64_t ring_opens;
    uint64_t ring_closes;
} io_uring_stats_t;

void io_uring_get_stats(io_uring_stats_t *out);
void io_uring_reset_stats(void);

/* Set the single stub file pointer used by the ring when no real fd
 * table is plumbed in.  This is a transitional convenience for
 * kshell-driven smoke tests; production installs use the
 * io_uring_attach_fd_table() hook (TODO). */
void io_uring_set_stub_file(file_t *f);
file_t *io_uring_get_file(uint16_t fd);

#endif /* FS_IO_URING_H */
