#ifndef JBD2_H
#define JBD2_H

#include "stdint.h"

/* JBD2-style generic journaling (ext3/ext4).
 *
 * Provides a transport-independent journal API that any filesystem
 * can use to make on-disk updates atomic. Supports:
 *   - handle / transaction lifecycle (start / stop / commit)
 *   - buffer / metadata reservations
 *   - ordered / writeback / journal data modes
 *   - checkpoint of committed data
 *   - replay on mount with sequence numbers
 *
 * Implements the JBD2 design without the extensive Linux-specific
 * locking, focusing on correctness of the journal ring.
 */

#define JBD2_MAGIC               0xC03B3998

/* Superblock magic is 0x4A42 ("JB") to indicate ext4 journal layout. */
#define JBD2_SB_MAGIC            0x4A42

/* Block types - first kernel reserved, second=metadata descriptors. */
#define JBD2_DESCRIPTOR_BLOCK    1
#define JBD2_COMMIT_BLOCK        2
#define JBD2_SUPERBLOCK_V1       1
#define JBD2_SUPERBLOCK_V2       2
#define JBD2_REVOKE_BLOCK        4

/* Commit-option flags per transaction. */
#define JBD2_BARRIER             (1 << 0)
#define JBD2_ORDERED             (1 << 1)  /* data flushed before commit */
#define JBD2_WRITEBACK          (1 << 2)  /* only metadata ordered */
#define JBD2_NODATA             (1 << 3)  /* data-only journals are forbidded */
#define JBD2_SYNCHRONOUS        (1 << 4)

/* Journal error and recovery states. */
#define JBD2_OK                  0
#define JBD2_ERR_REPLAY          (-1)
#define JBD2_ERR_FULL            (-2)

#define JBD2_ENTRIES_PER_BLOCK   170   /* approximate; depends on block size */

#define JBD2_MAX_HANDLES         64
#define JBD2_MAX_TRANSACTIONS    8

struct jbd2_journal;
struct jbd2_handle;
struct jbd2_buf_tag;
typedef struct jbd2_journal  jbd2_journal_t;
typedef struct jbd2_handle   jbd2_handle_t;
typedef struct jbd2_buf_tag  jbd2_buf_tag_t;

/* Tag in descriptor block indicating which block is logged. */
typedef struct jbd2_buf_tag {
    uint32_t block;
    uint32_t sequence;
} jbd2_buf_tag_t;

/* Tag in descriptor block (on-disk representation). */
typedef struct __attribute__((packed)) {
    uint32_t block;
    uint32_t sequence;
} jbd2_tag_t;

/* Block header common. */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t block_type;
    uint32_t sequence;
} jbd2_block_header_t;

/* Superblock on disk. */
typedef struct __attribute__((packed)) {
    jbd2_block_header_t header;   /* magic/type 0x01 or 0x02 */
    uint32_t blocksize;
    uint32_t maxlen;              /* journal total blocks */
    uint32_t first;               /* first free block in journal */
    uint32_t sequence;
    uint32_t start;
    uint8_t  uuid[16];
    /* Feature flags would follow in production. */
} jbd2_superblock_t;

/* Handle represents an in-progress transaction write. */
typedef struct jbd2_handle {
    jbd2_journal_t *journal;
    uint32_t        transaction_id;
    uint32_t        n_logged;
    uint32_t        flags;
    uint64_t        reserved_data_blocks;
    /* Reservation of blocks for atomic multi-block commit. */
    uint32_t        reserved_blocks;
    /* Internal state. */
    struct jbd2_handle *next;
    uint8_t           active;
} jbd2_handle_t;

/* Per-transaction state. */
typedef struct jbd2_transaction {
    uint32_t        tid;
    uint32_t        n_logged;
    jbd2_buf_tag_t *logs;        /* descriptor list */
    uint32_t        n_data_pending;
    uint64_t        data_pending[64];
    uint32_t        flags;
    uint64_t        reserved_credits;
    jbd2_handle_t  *handles;
    uint8_t         committed;
    uint8_t         active;
} jbd2_transaction_t;

/* Journal state. */
typedef struct jbd2_journal {
    /* Underlying device. */
    int (*read_block)(jbd2_journal_t *j, uint64_t block, void *buf);
    int (*write_block)(jbd2_journal_t *j, uint64_t block, const void *buf);
    int (*sync_device)(jbd2_journal_t *j);
    void *block_buf;             /* one block worth of memory */
    uint32_t block_buf_size;
    /* Geometry. */
    uint64_t start_block;        /* absolute block # of journal */
    uint32_t total_blocks;
    uint32_t blocksize;
    uint32_t first;              /* first free block in ring */
    uint32_t sequence;
    /* Live transactions. */
    jbd2_transaction_t  transactions[JBD2_MAX_TRANSACTIONS];
    int      active_transactions;
    /* Free / committed counters. */
    uint32_t used_blocks;
    /* Handles pool. */
    jbd2_handle_t handles[JBD2_MAX_HANDLES];
    int      next_handle;
    /* Recovery state. */
    int      needs_recovery;
    int      in_recovery;
    /* Stats. */
    uint64_t commits;
    uint64_t transactions_started;
    uint64_t blocks_logged;
    uint64_t checkpoints;
    uint64_t replayed_blocks;
    uint64_t rollbacks;
    uint64_t handles_used;
} jbd2_journal_t;

/* Lifecycle. */
int  jbd2_init(jbd2_journal_t *j,
                uint64_t start, uint32_t total_blocks, uint32_t blocksize);
int  jbd2_replay(jbd2_journal_t *j);
int  jbd2_recover_checkpoint(jbd2_journal_t *j);
int  jbd2_checkpoint(jbd2_journal_t *j);

/* Transaction API. */
jbd2_handle_t *jbd2_start(jbd2_journal_t *j, uint32_t credits,
                          uint32_t flags);
int  jbd2_extend(jbd2_handle_t *h, uint32_t add_credits);
int  jbd2_log_data(jbd2_handle_t *h, const void *data, uint32_t block);
int  jbd2_log_metadata(jbd2_handle_t *h, const void *data, uint32_t block);
int  jbd2_stop(jbd2_handle_t *h);
int  jbd2_commit(jbd2_handle_t *h);
int  jbd2_abort(jbd2_handle_t *h);

/* Force flush of any committed transactions. */
int  jbd2_force_commit(jbd2_journal_t *j);

/* Stats. */
const char *jbd2_state_string(int status);
void jbd2_print(jbd2_journal_t *j);

#endif