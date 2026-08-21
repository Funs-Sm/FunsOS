/* jbd2.c - generic FS journaling (JBD2-style). */
#include "jbd2.h"
#include "kheap.h"
#include "string.h"
#include "../kernel/klog.h"

#define TRANS_TIME_DEBUG 0

static jbd2_transaction_t *find_txn(jbd2_journal_t *j, uint32_t tid) {
    if (!j) return (jbd2_transaction_t *)0;
    for (int i = 0; i < JBD2_MAX_TRANSACTIONS; i++) {
        if (j->transactions[i].tid == tid && j->transactions[i].active)
            return &j->transactions[i];
    }
    return (jbd2_transaction_t *)0;
}

static int alloc_txn(jbd2_journal_t *j) {
    if (!j) return -1;
    if (j->active_transactions >= JBD2_MAX_TRANSACTIONS) return -1;
    for (int i = 0; i < JBD2_MAX_TRANSACTIONS; i++) {
        if (!j->transactions[i].active) {
            j->transactions[i].active = 1;
            j->transactions[i].committed = 0;
            j->transactions[i].tid = ++j->sequence;
            j->transactions[i].logs = (jbd2_buf_tag_t *)kmalloc(
                                          sizeof(jbd2_buf_tag_t) * 256);
            if (!j->transactions[i].logs) return -1;
            j->transactions[i].n_logged = 0;
            j->transactions[i].handles = (jbd2_handle_t *)0;
            j->transactions[i].flags = 0;
            j->active_transactions++;
            j->transactions_started++;
            return i;
        }
    }
    return -1;
}

static jbd2_handle_t *alloc_handle(jbd2_journal_t *j) {
    if (!j) return (jbd2_handle_t *)0;
    for (int i = 0; i < JBD2_MAX_HANDLES; i++) {
        if (j->handles[i].active == 0) {
            j->handles[i].active = 1;
            j->handles[i].journal = j;
            j->handles[i].transaction_id = 0;
            j->handles[i].reserved_blocks = 0;
            j->handles[i].reserved_data_blocks = 0;
            j->handles[i].flags = 0;
            j->handles[i].n_logged = 0;
            j->handles[i].next = (jbd2_handle_t *)0;
            return &j->handles[i];
        }
    }
    return (jbd2_handle_t *)0;
}

static void free_handle(jbd2_handle_t *h) {
    if (!h) return;
    h->active = 0;
    h->transaction_id = 0;
    h->n_logged = 0;
    h->reserved_blocks = 0;
    h->reserved_data_blocks = 0;
}

static uint32_t ring_index(jbd2_journal_t *j, uint32_t blk) {
    if (!j) return 0;
    return (blk - j->start_block) % j->total_blocks;
}

static int journal_read_abs(jbd2_journal_t *j, uint32_t abs_block, void *buf) {
    if (!j || !j->read_block) return -1;
    return j->read_block(j, abs_block, buf);
}

static int journal_write_abs(jbd2_journal_t *j, uint32_t abs_block,
                              const void *buf) {
    if (!j || !j->write_block) return -1;
    return j->write_block(j, abs_block, buf);
}

static int load_sb(jbd2_journal_t *j) {
    if (!j || j->total_blocks == 0) return -1;
    jbd2_superblock_t sb;
    if (journal_read_abs(j, j->start_block, &sb) != 0) return -1;
    if (sb.header.magic != JBD2_MAGIC) return -1;
    j->blocksize = sb.blocksize;
    j->first = sb.start;
    j->sequence = sb.sequence;
    j->needs_recovery = (j->first != 0 || j->sequence != 0) ? 1 : 0;
    return 0;
}

static int sync_sb(jbd2_journal_t *j) {
    if (!j) return -1;
    memset(j->block_buf, 0, j->blocksize);
    jbd2_superblock_t *sb = (jbd2_superblock_t *)j->block_buf;
    sb->header.magic = JBD2_MAGIC;
    sb->header.block_type = JBD2_SUPERBLOCK_V2;
    sb->header.sequence = j->sequence;
    sb->blocksize = j->blocksize;
    sb->maxlen = j->total_blocks;
    sb->first = j->first;
    sb->start = j->first;
    return journal_write_abs(j, j->start_block, j->block_buf);
}

static int jbd2_store_one_block(jbd2_journal_t *j, uint32_t abs_blk,
                                 const void *buf) {
    return journal_write_abs(j, abs_blk, buf);
}

int jbd2_init(jbd2_journal_t *j, uint64_t start, uint32_t total_blocks,
              uint32_t blocksize) {
    if (!j || total_blocks < 8) return -1;
    memset(j, 0, sizeof(*j));
    j->start_block = start;
    j->total_blocks = total_blocks;
    j->blocksize = blocksize;
    j->block_buf_size = blocksize;
    j->block_buf = kmalloc(blocksize);
    if (!j->block_buf) return -1;
    j->next_handle = 0;
    if (load_sb(j) != 0) {
        memset(j->block_buf, 0, blocksize);
        sync_sb(j);
        j->sequence = 1;
        j->first = (uint32_t)start + 1;
    }
    j->needs_recovery = 0;
    return 0;
}

static int journal_log_block(jbd2_journal_t *j, jbd2_transaction_t *t,
                              const void *data, uint32_t fs_block) {
    if (!j || !t) return -1;
    uint32_t used = j->used_blocks;
    if (used + 3 > j->total_blocks) {
        j->first = (uint32_t)j->start_block
                 + ((j->first + 1 - j->start_block + 1) % j->total_blocks);
    }
    memset(j->block_buf, 0, j->blocksize);
    jbd2_block_header_t *hdr = (jbd2_block_header_t *)j->block_buf;
    hdr->magic = JBD2_MAGIC;
    hdr->block_type = JBD2_DESCRIPTOR_BLOCK;
    hdr->sequence = t->tid;
    jbd2_tag_t *tags = (jbd2_tag_t *)(hdr + 1);
    if (JBD2_ENTRIES_PER_BLOCK > 0) tags[0].block = fs_block;
    tags[0].sequence = t->tid;
    uint32_t slot = (uint32_t)j->start_block
                  + ((j->first + j->used_blocks - j->start_block)
                     % j->total_blocks);
    journal_write_abs(j, slot, j->block_buf);
    j->used_blocks++;

    uint32_t data_slot = (uint32_t)j->start_block
                       + ((j->first + j->used_blocks - j->start_block)
                          % j->total_blocks);
    journal_write_abs(j, data_slot, data);
    j->used_blocks++;
    j->blocks_logged++;
    t->n_logged++;
    return 0;
}

static int journal_commit_descriptor(jbd2_journal_t *j,
                                      jbd2_transaction_t *t,
                                      const jbd2_buf_tag_t *tags, int n) {
    if (!j || !t || n <= 0) return -1;
    memset(j->block_buf, 0, j->blocksize);
    jbd2_block_header_t *hdr = (jbd2_block_header_t *)j->block_buf;
    hdr->magic = JBD2_MAGIC;
    hdr->block_type = JBD2_DESCRIPTOR_BLOCK;
    hdr->sequence = t->tid;
    jbd2_tag_t *dst = (jbd2_tag_t *)(hdr + 1);
    int max = (int)((j->blocksize - sizeof(jbd2_block_header_t))
                     / sizeof(jbd2_tag_t));
    int written = n;
    if (written > max) written = max;
    if (written > 170) written = 170;
    for (int i = 0; i < written; i++) {
        dst[i].block = tags[i].block;
        dst[i].sequence = t->tid;
    }
    uint32_t desc_slot = (uint32_t)j->start_block
                       + ((j->first + j->used_blocks - j->start_block)
                          % j->total_blocks);
    journal_write_abs(j, desc_slot, j->block_buf);
    j->used_blocks++;
    return written;
}

static int journal_write_commit(jbd2_journal_t *j, jbd2_transaction_t *t) {
    if (!j || !t) return -1;
    memset(j->block_buf, 0, j->blocksize);
    jbd2_block_header_t *hdr = (jbd2_block_header_t *)j->block_buf;
    hdr->magic = JBD2_MAGIC;
    hdr->block_type = JBD2_COMMIT_BLOCK;
    hdr->sequence = t->tid;
    uint32_t slot = (uint32_t)j->start_block
                  + ((j->first + j->used_blocks - j->start_block)
                     % j->total_blocks);
    journal_write_abs(j, slot, j->block_buf);
    j->used_blocks++;
    return 0;
}

static int finish_txn(jbd2_journal_t *j, jbd2_transaction_t *t, int commit);

jbd2_handle_t *jbd2_start(jbd2_journal_t *j, uint32_t credits, uint32_t flags) {
    if (!j) return (jbd2_handle_t *)0;
    jbd2_handle_t *h = alloc_handle(j);
    if (!h) return (jbd2_handle_t *)0;
    int tx_idx = alloc_txn(j);
    if (tx_idx < 0) { free_handle(h); return (jbd2_handle_t *)0; }
    h->transaction_id = j->transactions[tx_idx].tid;
    h->flags = flags;
    h->reserved_blocks = credits;
    h->next = j->transactions[tx_idx].handles;
    j->transactions[tx_idx].handles = h;
    j->transactions[tx_idx].flags |= flags;
    j->transactions[tx_idx].reserved_credits = credits;
    j->handles_used++;
    return h;
}

int jbd2_extend(jbd2_handle_t *h, uint32_t add_credits) {
    if (!h) return -1;
    jbd2_transaction_t *t = find_txn(h->journal, h->transaction_id);
    if (!t) return -1;
    h->reserved_blocks += add_credits;
    t->reserved_credits += add_credits;
    return 0;
}

int jbd2_log_data(jbd2_handle_t *h, const void *data, uint32_t block) {
    if (!h || !data) return -1;
    jbd2_transaction_t *t = find_txn(h->journal, h->transaction_id);
    if (!t) return -1;
    if (h->reserved_blocks == 0) return JBD2_ERR_FULL;
    if (t->n_logged < 256) {
        t->logs[t->n_logged].block = block;
        t->logs[t->n_logged].sequence = t->tid;
        t->n_logged++;
    }
    h->reserved_blocks--;
    h->reserved_data_blocks++;
    h->n_logged++;
    return journal_log_block(h->journal, t, data, block);
}

int jbd2_log_metadata(jbd2_handle_t *h, const void *data, uint32_t block) {
    if (!h || !data) return -1;
    jbd2_transaction_t *t = find_txn(h->journal, h->transaction_id);
    if (!t) return -1;
    if (h->reserved_blocks == 0) return JBD2_ERR_FULL;
    if (t->n_logged < 256) {
        t->logs[t->n_logged].block = block;
        t->logs[t->n_logged].sequence = t->tid;
        t->n_logged++;
    }
    h->reserved_blocks--;
    h->n_logged++;
    return journal_log_block(h->journal, t, data, block);
}

int jbd2_stop(jbd2_handle_t *h) {
    if (!h) return -1;
    jbd2_transaction_t *t = find_txn(h->journal, h->transaction_id);
    free_handle(h);
    if (t) {
        if (t->handles == (jbd2_handle_t *)0 && t->n_logged > 0) {
            return finish_txn(h->journal, t, 1);
        }
    }
    return 0;
}

static int finish_txn(jbd2_journal_t *j, jbd2_transaction_t *t, int commit) {
    if (!j || !t) return -1;
    if (commit) {
        journal_commit_descriptor(j, t, t->logs, (int)t->n_logged);
        journal_write_commit(j, t);
        t->committed = 1;
        j->commits++;
        j->first = (uint32_t)j->start_block
                 + ((j->first + j->used_blocks - j->start_block)
                    % j->total_blocks);
        j->used_blocks = 0;
    } else {
        j->rollbacks++;
    }
    j->sequence++;
    t->active = 0;
    j->active_transactions--;
    if (t->logs) kfree(t->logs);
    t->logs = (jbd2_buf_tag_t *)0;
    sync_sb(j);
    return 0;
}

int jbd2_commit(jbd2_handle_t *h) {
    if (!h) return -1;
    jbd2_transaction_t *t = find_txn(h->journal, h->transaction_id);
    if (!t) return -1;
    return finish_txn(h->journal, t, 1);
}

int jbd2_abort(jbd2_handle_t *h) {
    if (!h) return -1;
    jbd2_transaction_t *t = find_txn(h->journal, h->transaction_id);
    if (!t) return -1;
    return finish_txn(h->journal, t, 0);
}

int jbd2_force_commit(jbd2_journal_t *j) {
    if (!j) return -1;
    for (int i = 0; i < JBD2_MAX_TRANSACTIONS; i++) {
        if (j->transactions[i].active) {
            return finish_txn(j, &j->transactions[i], 1);
        }
    }
    return 0;
}

int jbd2_checkpoint(jbd2_journal_t *j) {
    if (!j) return -1;
    j->checkpoints++;
    return sync_sb(j);
}

int jbd2_recover_checkpoint(jbd2_journal_t *j) {
    return jbd2_checkpoint(j);
}

int jbd2_replay(jbd2_journal_t *j) {
    if (!j || j->total_blocks == 0) return JBD2_ERR_REPLAY;
    j->in_recovery = 1;
    uint32_t blk = j->first;
    uint32_t expected_seq = j->sequence;
    int count = 0;
    while (count < (int)j->total_blocks) {
        jbd2_block_header_t hdr;
        memset(j->block_buf, 0, j->blocksize);
        if (journal_read_abs(j, blk, j->block_buf) != 0) break;
        memcpy(&hdr, j->block_buf, sizeof(hdr));
        if (hdr.magic != JBD2_MAGIC) break;
        if (hdr.sequence != expected_seq) break;
        if (hdr.block_type == JBD2_DESCRIPTOR_BLOCK) {
            int max = (int)((j->blocksize - sizeof(jbd2_block_header_t))
                            / sizeof(jbd2_tag_t));
            int written = 0;
            jbd2_tag_t *tags = (jbd2_tag_t *)(((uint8_t *)j->block_buf)
                                                + sizeof(jbd2_block_header_t));
            for (int i = 0; i < max && i < 170; i++) {
                if (tags[i].block == 0 && tags[i].sequence == 0) break;
                written++;
            }
            if (written == 0) break;
            blk++;
            count++;
            for (int d = 0; d < written; d++) {
                memset(j->block_buf, 0, j->blocksize);
                if (journal_read_abs(j, blk, j->block_buf) != 0) break;
                uint32_t fs_block = tags[d].block;
                if (j->write_block) {
                    jbd2_store_one_block(j, fs_block, j->block_buf);
                    j->replayed_blocks++;
                }
                blk++;
                count++;
            }
            memset(j->block_buf, 0, j->blocksize);
            if (journal_read_abs(j, blk, j->block_buf) != 0) break;
            jbd2_block_header_t commit;
            memcpy(&commit, j->block_buf, sizeof(commit));
            if (commit.magic != JBD2_MAGIC
                || commit.block_type != JBD2_COMMIT_BLOCK
                || commit.sequence != expected_seq) {
                j->rollbacks++;
                break;
            }
            expected_seq++;
            blk++;
            count++;
        } else if (hdr.block_type == JBD2_COMMIT_BLOCK) {
            expected_seq++;
            blk++;
            count++;
        } else {
            break;
        }
    }
    j->first = blk;
    j->sequence = expected_seq;
    j->in_recovery = 0;
    j->needs_recovery = 0;
    return sync_sb(j);
}

const char *jbd2_state_string(int status) {
    switch (status) {
        case JBD2_OK: return "ok";
        case JBD2_ERR_REPLAY: return "replay-error";
        case JBD2_ERR_FULL: return "full";
    }
    return "unknown";
}

void jbd2_print(jbd2_journal_t *j) {
    if (!j) return;
    klog_write(KLOG_INFO,
               "jbd2: first=%u used=%u seq=%u commits=%llu replayed=%llu\n",
               j->first, j->used_blocks, j->sequence,
               j->commits, j->replayed_blocks);
}