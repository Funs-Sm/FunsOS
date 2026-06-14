#include "ext4.h"
#include "ide.h"
#include "kheap.h"
#include "string.h"

#define EXT4_JOURNAL_DESCRIPTOR_BLOCK 1
#define EXT4_JOURNAL_COMMIT_BLOCK     2
#define EXT4_JOURNAL_SUPERBLOCK_V1    1
#define EXT4_JOURNAL_SUPERBLOCK_V2    2

typedef struct {
    ext4_journal_header_t header;
    uint32_t blocktype;
    uint32_t start_seq;
    uint32_t start_block;
    uint32_t num_blocks;
} ext4_journal_descriptor_t;

typedef struct {
    ext4_journal_header_t header;
    uint32_t sequence;
} ext4_journal_commit_t;

typedef struct {
    ext4_journal_header_t header;
    uint32_t blocksize;
    uint32_t maxlen;
    uint32_t start;
    uint32_t sequence;
    uint32_t start_seq;
    uint32_t start_block;
} ext4_journal_sb_t;

static uint32_t journal_start;
static uint32_t journal_size;
static uint32_t journal_seq;
static uint32_t journal_drive;
static uint32_t journal_partition_start;
static uint32_t journal_transaction_start;
static uint32_t journal_transaction_blocks[256];
static uint32_t journal_transaction_count;

/* Journal statistics */
static ext4_journal_stats_t journal_stats;

static int32_t journal_read_block(uint32_t block, void *buf) {
    uint32_t lba = journal_partition_start + (block * (4096 / 512));
    return ide_read_sectors(journal_drive, 8, lba, buf);
}

static int32_t journal_write_block(uint32_t block, const void *buf) {
    uint32_t lba = journal_partition_start + (block * (4096 / 512));
    return ide_write_sectors(journal_drive, 8, lba, (void *)buf);
}

void ext4_journal_recover(void) {
    void *buf = kmalloc(4096);
    if (!buf) return;

    if (journal_read_block(journal_start, buf) != 0) {
        kfree(buf);
        return;
    }

    ext4_journal_sb_t *jsb = (ext4_journal_sb_t *)buf;
    if (jsb->header.magic != EXT4_JOURNAL_MAGIC) {
        kfree(buf);
        return;
    }

    uint32_t seq = jsb->sequence;
    uint32_t block = jsb->start;
    uint32_t replayed = 0;

    uint32_t i;
    for (i = 0; i < journal_size; i++) {
        uint32_t curr = journal_start + 1 + ((block - journal_start - 1) % journal_size);

        if (journal_read_block(curr, buf) != 0) break;

        ext4_journal_header_t *hdr = (ext4_journal_header_t *)buf;
        if (hdr->magic != EXT4_JOURNAL_MAGIC) break;

        if (hdr->block_type == EXT4_JOURNAL_DESCRIPTOR_BLOCK && hdr->block_seq == seq) {
            ext4_journal_descriptor_t *desc = (ext4_journal_descriptor_t *)buf;
            uint32_t num_blocks = desc->num_blocks;
            uint32_t j;

            void *data_buf = kmalloc(4096);
            if (!data_buf) break;

            for (j = 0; j < num_blocks; j++) {
                uint32_t data_block = curr + 1 + j;
                uint32_t next_in_ring = journal_start + 1 + ((data_block - journal_start - 1) % journal_size);

                if (journal_read_block(next_in_ring, data_buf) != 0) break;

                uint32_t *fs_block_list = (uint32_t *)(desc + 1);
                uint32_t target = fs_block_list[j];

                journal_write_block(target, data_buf);
                replayed++;
            }

            kfree(data_buf);

            uint32_t commit_pos = journal_start + 1 + ((curr + num_blocks - journal_start) % journal_size);
            if (journal_read_block(commit_pos, buf) == 0) {
                ext4_journal_commit_t *commit = (ext4_journal_commit_t *)buf;
                if (commit->header.magic == EXT4_JOURNAL_MAGIC &&
                    commit->header.block_type == EXT4_JOURNAL_COMMIT_BLOCK &&
                    commit->header.block_seq == seq) {
                    seq++;
                    block = commit_pos + 1;
                    i += num_blocks + 1;
                    continue;
                }
            }

            /* Uncommitted transaction found - this is a rollback scenario */
            journal_stats.rollbacks++;
            break;
        } else if (hdr->block_type == EXT4_JOURNAL_COMMIT_BLOCK) {
            seq++;
            block++;
            continue;
        } else {
            break;
        }
    }

    jsb->sequence = seq;
    jsb->start = block;
    journal_write_block(journal_start, buf);

    journal_seq = seq;
    journal_stats.replays += replayed;
    journal_stats.last_seq = journal_seq;

    kfree(buf);
}

static void ext4_journal_begin(void) {
    journal_transaction_start = journal_start + 1 + ((journal_seq) % journal_size);
    journal_transaction_count = 0;
}

int32_t ext4_journal_write(uint32_t block, const void *data) {
    if (journal_transaction_count >= 256) return -1;

    uint32_t journal_block = journal_transaction_start + journal_transaction_count;
    uint32_t ring_block = journal_start + 1 + ((journal_block - journal_start - 1) % journal_size);

    if (journal_write_block(ring_block, data) != 0) return -1;

    journal_transaction_blocks[journal_transaction_count] = block;
    journal_transaction_count++;
    journal_stats.blocks_written++;

    return 0;
}

static int32_t ext4_journal_commit(void) {
    void *buf = kmalloc(4096);
    if (!buf) return -1;

    ext4_journal_descriptor_t *desc = (ext4_journal_descriptor_t *)buf;
    memset(buf, 0, 4096);
    desc->header.magic = EXT4_JOURNAL_MAGIC;
    desc->header.block_type = EXT4_JOURNAL_DESCRIPTOR_BLOCK;
    desc->header.block_seq = journal_seq;
    desc->header.block_size = 4096;
    desc->blocktype = EXT4_JOURNAL_DESCRIPTOR_BLOCK;
    desc->start_seq = journal_seq;
    desc->start_block = journal_transaction_start;
    desc->num_blocks = journal_transaction_count;

    uint32_t i;
    uint32_t *fs_blocks = (uint32_t *)(desc + 1);
    for (i = 0; i < journal_transaction_count; i++) {
        fs_blocks[i] = journal_transaction_blocks[i];
    }

    uint32_t desc_block = journal_start + 1 + ((journal_transaction_start - journal_start - 1) % journal_size);
    journal_write_block(desc_block, buf);

    uint32_t commit_pos = journal_transaction_start + journal_transaction_count;
    uint32_t commit_ring = journal_start + 1 + ((commit_pos - journal_start - 1) % journal_size);

    ext4_journal_commit_t *commit = (ext4_journal_commit_t *)buf;
    memset(buf, 0, 4096);
    commit->header.magic = EXT4_JOURNAL_MAGIC;
    commit->header.block_type = EXT4_JOURNAL_COMMIT_BLOCK;
    commit->header.block_seq = journal_seq;
    commit->header.block_size = 4096;
    commit->sequence = journal_seq;

    journal_write_block(commit_ring, buf);

    journal_seq++;
    journal_stats.commits++;
    journal_stats.last_seq = journal_seq;

    void *sb_buf = kmalloc(4096);
    if (sb_buf) {
        journal_read_block(journal_start, sb_buf);
        ext4_journal_sb_t *jsb = (ext4_journal_sb_t *)sb_buf;
        jsb->sequence = journal_seq;
        jsb->start = commit_ring + 1;
        journal_write_block(journal_start, sb_buf);
        kfree(sb_buf);
    }

    kfree(buf);
    return 0;
}

static void ext4_journal_checkpoint(void) {
    void *buf = kmalloc(4096);
    if (!buf) return;

    if (journal_read_block(journal_start, buf) != 0) {
        kfree(buf);
        return;
    }

    ext4_journal_sb_t *jsb = (ext4_journal_sb_t *)buf;
    jsb->sequence = journal_seq;
    journal_write_block(journal_start, buf);

    journal_stats.checkpoints++;

    kfree(buf);
}

/* Public journal replay function - called at mount time */
int32_t journal_replay(void) {
    ext4_journal_recover();
    return 0;
}

/* Public checkpoint - force commit of old transactions */
int32_t journal_checkpoint(void) {
    /* If there's an active transaction, commit it first */
    if (journal_transaction_count > 0) {
        ext4_journal_commit();
    }
    ext4_journal_checkpoint();
    return 0;
}

/* Return journal statistics */
ext4_journal_stats_t *journal_get_stats(void) {
    return &journal_stats;
}
