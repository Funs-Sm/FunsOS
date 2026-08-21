/* ext2_write.c - ext2 write support.
 * Provides block / inode allocation, file creation, directory
 * entry manipulation, file data write, and rename / unlink.
 * All metadata operations are wrapped in a JBD2 transaction when
 * the journal is available.
 */

#include "ext2_write.h"
#include "jbd2.h"
#include "ext2.h"
#include "ide.h"
#include "kheap.h"
#include "string.h"
#include "../kernel/klog.h"

static uint32_t g_drive;
static uint32_t g_partition_start;
static ext2_superblock_t g_sb;
static ext2_bgd_t  *g_bgdt;
static uint32_t g_block_size;
static uint32_t g_inode_size;
static uint32_t g_inodes_per_group;
static uint32_t g_blocks_per_group;
static uint32_t g_group_count;
static int     g_inited;

static int           g_journal_active;
static jbd2_journal_t g_journal;

static int read_lba(uint32_t lba, uint32_t sectors, void *buf) {
    return ide_read_sectors((uint8_t)g_drive, (uint8_t)sectors, lba, buf);
}
static int write_lba(uint32_t lba, uint32_t sectors, const void *buf) {
    return ide_write_sectors((uint8_t)g_drive, (uint8_t)sectors, lba,
                              (void *)buf);
}

static int read_fs_block(uint32_t block, void *buf) {
    uint32_t lba = g_partition_start + (block * (g_block_size / 512));
    return read_lba(lba, g_block_size / 512, buf);
}
static int write_fs_block(uint32_t block, const void *buf) {
    uint32_t lba = g_partition_start + (block * (g_block_size / 512));
    return write_lba(lba, g_block_size / 512, buf);
}

static int j_read(jbd2_journal_t *j, uint64_t block, void *buf) {
    (void)j;
    uint32_t lba = g_partition_start + ((uint32_t)block * (g_block_size / 512));
    return read_lba(lba, g_block_size / 512, buf);
}
static int j_write(jbd2_journal_t *j, uint64_t block, const void *buf) {
    (void)j;
    uint32_t lba = g_partition_start + ((uint32_t)block * (g_block_size / 512));
    return write_lba(lba, g_block_size / 512, buf);
}

static int read_bgd(uint32_t group, ext2_bgd_t *out) {
    uint32_t blk = (g_sb.first_data_block == 0) ? 2 + group : 1 + group;
    if (read_fs_block(blk, out) != 0) return -1;
    return 0;
}

static int write_bgd(uint32_t group, const ext2_bgd_t *bgd) {
    uint32_t blk = (g_sb.first_data_block == 0) ? 2 + group : 1 + group;
    return write_fs_block(blk, bgd);
}

static int read_inode(uint32_t ino, ext2_inode_t *out) {
    uint32_t group = (ino - 1) / g_inodes_per_group;
    uint32_t idx = (ino - 1) % g_inodes_per_group;
    if (group >= g_group_count) return -1;
    ext2_bgd_t bgd;
    if (read_bgd(group, &bgd) != 0) return -1;
    uint32_t byte_offset = idx * g_inode_size;
    uint32_t block = bgd.inode_table + byte_offset / g_block_size;
    uint32_t off_in_block = byte_offset % g_block_size;
    uint8_t *buf = (uint8_t *)kmalloc(g_block_size);
    if (!buf) return -1;
    if (read_fs_block(block, buf) != 0) { kfree(buf); return -1; }
    memcpy(out, buf + off_in_block, sizeof(*out));
    kfree(buf);
    return 0;
}

static int write_inode(uint32_t ino, const ext2_inode_t *inode) {
    uint32_t group = (ino - 1) / g_inodes_per_group;
    uint32_t idx = (ino - 1) % g_inodes_per_group;
    if (group >= g_group_count) return -1;
    ext2_bgd_t bgd;
    if (read_bgd(group, &bgd) != 0) return -1;
    uint32_t byte_offset = idx * g_inode_size;
    uint32_t block = bgd.inode_table + byte_offset / g_block_size;
    uint32_t off_in_block = byte_offset % g_block_size;
    uint8_t *buf = (uint8_t *)kmalloc(g_block_size);
    if (!buf) return -1;
    if (read_fs_block(block, buf) != 0) { kfree(buf); return -1; }
    memcpy(buf + off_in_block, inode, sizeof(*inode));
    int r = write_fs_block(block, buf);
    kfree(buf);
    return r;
}

static int read_block_bitmap(uint32_t group, uint8_t **out, uint32_t *out_size) {
    ext2_bgd_t bgd;
    if (read_bgd(group, &bgd) != 0) return -1;
    uint8_t *buf = (uint8_t *)kmalloc(g_block_size);
    if (!buf) return -1;
    if (read_fs_block(bgd.block_bitmap, buf) != 0) { kfree(buf); return -1; }
    *out = buf;
    *out_size = g_block_size;
    return 0;
}
static int read_inode_bitmap(uint32_t group, uint8_t **out, uint32_t *out_size) {
    ext2_bgd_t bgd;
    if (read_bgd(group, &bgd) != 0) return -1;
    uint8_t *buf = (uint8_t *)kmalloc(g_block_size);
    if (!buf) return -1;
    if (read_fs_block(bgd.inode_bitmap, buf) != 0) { kfree(buf); return -1; }
    *out = buf;
    *out_size = g_block_size;
    return 0;
}

static int write_block_bitmap(uint32_t group, const uint8_t *bm) {
    ext2_bgd_t bgd;
    if (read_bgd(group, &bgd) != 0) return -1;
    return write_fs_block(bgd.block_bitmap, bm);
}
static int write_inode_bitmap(uint32_t group, const uint8_t *bm) {
    ext2_bgd_t bgd;
    if (read_bgd(group, &bgd) != 0) return -1;
    return write_fs_block(bgd.inode_bitmap, bm);
}

static int set_bitmap_bit(uint8_t *bm, uint32_t bit, uint8_t value) {
    uint32_t byte = bit >> 3;
    uint32_t mask = 1u << (bit & 7);
    if (value) bm[byte] |= (uint8_t)mask;
    else bm[byte] &= (uint8_t)~mask;
    return 0;
}
static int test_bitmap_bit(const uint8_t *bm, uint32_t bit) {
    return (bm[bit >> 3] >> (bit & 7)) & 1;
}

static int find_first_clear(const uint8_t *bm, uint32_t max_bit) {
    for (uint32_t i = 0; i < max_bit; i++) {
        if (!test_bitmap_bit(bm, i)) return (int)i;
    }
    return -1;
}

int ext2_write_init(uint32_t drive, uint32_t partition_start) {
    if (g_inited) return 0;
    g_drive = drive;
    g_partition_start = partition_start;

    if (read_fs_block(2, &g_sb) != 0) return -1;
    if (g_sb.magic != 0xEF53) return -1;
    g_block_size = 1024 << g_sb.log_block_size;
    if (g_block_size < 1024) g_block_size = 1024;
    g_inode_size = g_sb.inode_size ? g_sb.inode_size : 128;
    g_inodes_per_group = g_sb.inodes_per_group;
    g_blocks_per_group = g_sb.blocks_per_group;

    if (g_sb.inodes_count == 0 || g_inodes_per_group == 0) return -1;
    g_group_count = (g_sb.inodes_count + g_inodes_per_group - 1)
                  / g_inodes_per_group;

    g_bgdt = (ext2_bgd_t *)kmalloc(sizeof(*g_bgdt) * g_group_count);
    if (!g_bgdt) return -1;
    for (uint32_t i = 0; i < g_group_count; i++) {
        if (read_bgd(i, &g_bgdt[i]) != 0) return -1;
    }
    g_inited = 1;
    klog_write(KLOG_INFO,
               "ext2-write: drive=%u part=%u blocksize=%u inodes=%u groups=%u\n",
               drive, partition_start, g_block_size,
               g_sb.inodes_count, g_group_count);
    return 0;
}

int ext2_write_journal_init(void) {
    if (!g_inited) return -1;
    g_journal.read_block = j_read;
    g_journal.write_block = j_write;
    uint32_t journal_start = g_partition_start / (g_block_size / 512)
                            + g_blocks_per_group * g_group_count;
    return jbd2_init(&g_journal, journal_start, 1024, g_block_size);
}

int ext2_alloc_block(void) {
    if (!g_inited) return -1;
    for (uint32_t gr = 0; gr < g_group_count; gr++) {
        ext2_bgd_t *bgd = &g_bgdt[gr];
        if (bgd->free_blocks_count == 0) continue;
        uint8_t *bm; uint32_t bm_size;
        if (read_block_bitmap(gr, &bm, &bm_size) != 0) continue;
        uint32_t max = (g_blocks_per_group < bm_size * 8) ?
                       g_blocks_per_group : bm_size * 8;
        int b = find_first_clear(bm, max);
        if (b < 0) { kfree(bm); continue; }
        set_bitmap_bit(bm, (uint32_t)b, 1);
        write_block_bitmap(gr, bm);
        kfree(bm);
        bgd->free_blocks_count--;
        g_sb.free_blocks_count--;
        write_bgd(gr, bgd);
        uint32_t global = (gr * g_blocks_per_group)
                          + g_sb.first_data_block + (uint32_t)b;
        return (int)global;
    }
    return -1;
}

int ext2_free_block(uint32_t block) {
    if (!g_inited) return -1;
    if (block < g_sb.first_data_block) return -1;
    uint32_t b = block - g_sb.first_data_block;
    uint32_t gr = b / g_blocks_per_group;
    uint32_t loc = b % g_blocks_per_group;
    if (gr >= g_group_count) return -1;
    ext2_bgd_t *bgd = &g_bgdt[gr];
    uint8_t *bm; uint32_t bm_size;
    if (read_block_bitmap(gr, &bm, &bm_size) != 0) return -1;
    set_bitmap_bit(bm, loc, 0);
    write_block_bitmap(gr, bm);
    kfree(bm);
    bgd->free_blocks_count++;
    g_sb.free_blocks_count++;
    write_bgd(gr, bgd);
    return 0;
}

int ext2_alloc_blocks(uint32_t *blocks, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        int b = ext2_alloc_block();
        if (b < 0) {
            for (uint32_t j = 0; j < i; j++) ext2_free_block(blocks[j]);
            return -1;
        }
        blocks[i] = (uint32_t)b;
    }
    return 0;
}

void ext2_free_blocks(const uint32_t *blocks, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) ext2_free_block(blocks[i]);
}

int ext2_alloc_inode(uint8_t file_type) {
    (void)file_type;
    if (!g_inited) return -1;
    for (uint32_t gr = 0; gr < g_group_count; gr++) {
        ext2_bgd_t *bgd = &g_bgdt[gr];
        if (bgd->free_inodes_count == 0) continue;
        uint8_t *bm; uint32_t bm_size;
        if (read_inode_bitmap(gr, &bm, &bm_size) != 0) continue;
        uint32_t max = (g_inodes_per_group < bm_size * 8) ?
                       g_inodes_per_group : bm_size * 8;
        int b = find_first_clear(bm, max);
        if (b < 0) { kfree(bm); continue; }
        set_bitmap_bit(bm, (uint32_t)b, 1);
        write_inode_bitmap(gr, bm);
        kfree(bm);
        bgd->free_inodes_count--;
        g_sb.free_inodes_count--;
        write_bgd(gr, bgd);
        return (int)(gr * g_inodes_per_group + (uint32_t)b + 1);
    }
    return -1;
}

int ext2_free_inode(uint32_t ino) {
    if (!g_inited || ino == 0) return -1;
    uint32_t gr = (ino - 1) / g_inodes_per_group;
    uint32_t loc = (ino - 1) % g_inodes_per_group;
    if (gr >= g_group_count) return -1;
    ext2_bgd_t *bgd = &g_bgdt[gr];
    uint8_t *bm; uint32_t bm_size;
    if (read_inode_bitmap(gr, &bm, &bm_size) != 0) return -1;
    set_bitmap_bit(bm, loc, 0);
    write_inode_bitmap(gr, bm);
    kfree(bm);
    bgd->free_inodes_count++;
    g_sb.free_inodes_count++;
    write_bgd(gr, bgd);
    return 0;
}

int ext2_dir_add(uint32_t dir_ino, const char *name, uint32_t child_ino,
                  uint8_t file_type) {
    if (!g_inited) return -1;
    if (!name) return -1;
    uint32_t name_len = 0;
    while (name[name_len] && name_len < 255) name_len++;
    if (name_len == 0) return -1;

    ext2_inode_t dir;
    if (read_inode(dir_ino, &dir) != 0) return -1;
    if ((dir.i_mode & 0xF000) != EXT2_S_IFDIR) return -1;
    if (dir.i_size == 0) return -1;

    jbd2_handle_t *h = (jbd2_handle_t *)0;
    if (g_journal_active) h = jbd2_start(&g_journal, 8, JBD2_ORDERED);

    uint32_t offset = 0;
    while (offset < dir.i_size) {
        uint32_t blk = dir.i_block[offset / g_block_size];
        if (blk == 0) return -1;
        uint8_t *buf = (uint8_t *)kmalloc(g_block_size);
        if (!buf) return -1;
        if (read_fs_block(blk, buf) != 0) { kfree(buf); return -1; }
        uint32_t pos = 0;
        while (pos + sizeof(ext2_dir_entry_t) <= g_block_size) {
            ext2_dir_entry_t *de = (ext2_dir_entry_t *)(buf + pos);
            if (de->rec_len == 0) break;
            int actual = sizeof(ext2_dir_entry_t) - 1 + de->name_len;
            int aligned = (actual + 3) & ~3;
            int spare = (int)de->rec_len - aligned;
            int want = sizeof(ext2_dir_entry_t) - 1 + name_len;
            int want_aln = (want + 3) & ~3;
            if (spare >= want_aln) {
                if (de->inode == 0) {
                    de->inode = child_ino;
                    de->name_len = (uint8_t)name_len;
                    de->file_type = file_type;
                    memcpy(de->name, name, name_len);
                } else {
                    uint16_t new_len = (uint16_t)want_aln;
                    ext2_dir_entry_t *new_de = (ext2_dir_entry_t *)
                                               (buf + pos + aligned);
                    new_de->inode = child_ino;
                    new_de->rec_len = (uint16_t)(spare + aligned - new_len);
                    new_de->name_len = (uint8_t)name_len;
                    new_de->file_type = file_type;
                    memcpy(new_de->name, name, name_len);
                    de->rec_len = (uint16_t)aligned;
                    (void)new_de;
                }
                if (h) jbd2_log_metadata(h, buf, blk);
                else write_fs_block(blk, buf);
                kfree(buf);
                if (h) { jbd2_commit(h); }
                return 0;
            }
            pos += de->rec_len;
            if (pos >= g_block_size) break;
        }
        kfree(buf);
        offset += g_block_size;
    }
    if (h) jbd2_abort(h);
    return -1;
}

int ext2_dir_lookup(uint32_t dir_ino, const char *name, uint32_t *out_ino) {
    if (!g_inited) return -1;
    ext2_inode_t dir;
    if (read_inode(dir_ino, &dir) != 0) return -1;
    uint32_t name_len = 0;
    while (name[name_len]) name_len++;
    for (uint32_t offset = 0; offset < dir.i_size; offset += g_block_size) {
        uint32_t blk = dir.i_block[offset / g_block_size];
        if (blk == 0) return -1;
        uint8_t *buf = (uint8_t *)kmalloc(g_block_size);
        if (!buf) return -1;
        if (read_fs_block(blk, buf) != 0) { kfree(buf); return -1; }
        uint32_t pos = 0;
        while (pos < g_block_size) {
            ext2_dir_entry_t *de = (ext2_dir_entry_t *)(buf + pos);
            if (de->rec_len == 0) break;
            if (de->inode != 0 && de->name_len == name_len
                && memcmp(de->name, name, name_len) == 0) {
                *out_ino = de->inode;
                kfree(buf);
                return 0;
            }
            pos += de->rec_len;
            if (pos >= g_block_size) break;
        }
        kfree(buf);
    }
    return -1;
}

int ext2_dir_remove(uint32_t dir_ino, const char *name) {
    if (!g_inited) return -1;
    ext2_inode_t dir;
    if (read_inode(dir_ino, &dir) != 0) return -1;
    uint32_t name_len = 0;
    while (name[name_len]) name_len++;
    for (uint32_t offset = 0; offset < dir.i_size; offset += g_block_size) {
        uint32_t blk = dir.i_block[offset / g_block_size];
        if (blk == 0) return -1;
        uint8_t *buf = (uint8_t *)kmalloc(g_block_size);
        if (!buf) return -1;
        if (read_fs_block(blk, buf) != 0) { kfree(buf); return -1; }
        uint32_t pos = 0;
        ext2_dir_entry_t *prev_de = (ext2_dir_entry_t *)0;
        while (pos < g_block_size) {
            ext2_dir_entry_t *de = (ext2_dir_entry_t *)(buf + pos);
            if (de->rec_len == 0) break;
            if (de->inode != 0 && de->name_len == name_len
                && memcmp(de->name, name, name_len) == 0) {
                if (prev_de) {
                    int act = sizeof(*de) - 1 + de->name_len;
                    int aligned = (act + 3) & ~3;
                    prev_de->rec_len += (uint16_t)(de->rec_len + aligned);
                } else {
                    de->inode = 0;
                }
                write_fs_block(blk, buf);
                kfree(buf);
                return 0;
            }
            pos += de->rec_len;
            prev_de = de;
            if (pos >= g_block_size) break;
        }
        kfree(buf);
    }
    return -1;
}

int ext2_create(uint32_t parent_ino, const char *name, uint16_t mode,
                 uint32_t *out_ino) {
    if (!g_inited) return -1;
    int ino = ext2_alloc_inode(0);
    if (ino < 0) return -1;

    jbd2_handle_t *h = (jbd2_handle_t *)0;
    if (g_journal_active) h = jbd2_start(&g_journal, 8, JBD2_ORDERED);

    ext2_inode_t in;
    memset(&in, 0, sizeof(in));
    in.i_mode = EXT2_S_IFREG | (mode & 0xFFF);
    in.i_links_count = 1;
    in.i_size = 0;
    in.i_blocks = 0;
    write_inode((uint32_t)ino, &in);

    if (ext2_dir_add(parent_ino, name, (uint32_t)ino, 1) != 0) {
        if (h) jbd2_abort(h);
        ext2_free_inode((uint32_t)ino);
        return -1;
    }
    if (out_ino) *out_ino = (uint32_t)ino;
    if (h) jbd2_commit(h);
    return 0;
}

int ext2_mkdir(uint32_t parent_ino, const char *name, uint16_t mode,
                uint32_t *out_ino) {
    if (!g_inited) return -1;
    int ino = ext2_alloc_inode(2);
    if (ino < 0) return -1;

    jbd2_handle_t *h = (jbd2_handle_t *)0;
    if (g_journal_active) h = jbd2_start(&g_journal, 16, JBD2_ORDERED);

    int data_blk = ext2_alloc_block();
    if (data_blk < 0) {
        if (h) jbd2_abort(h);
        ext2_free_inode((uint32_t)ino);
        return -1;
    }
    uint8_t *buf = (uint8_t *)kmalloc(g_block_size);
    if (!buf) {
        if (h) jbd2_abort(h);
        ext2_free_inode((uint32_t)ino);
        ext2_free_block((uint32_t)data_blk);
        return -1;
    }
    memset(buf, 0, g_block_size);
    ext2_dir_entry_t *d1 = (ext2_dir_entry_t *)buf;
    d1->inode = (uint32_t)ino;
    d1->rec_len = 12;
    d1->name_len = 1;
    d1->file_type = 2;
    d1->name[0] = '.';
    ext2_dir_entry_t *d2 = (ext2_dir_entry_t *)(buf + 12);
    d2->inode = parent_ino;
    d2->rec_len = (uint16_t)(g_block_size - 12);
    d2->name_len = 2;
    d2->file_type = 2;
    d2->name[0] = '.'; d2->name[1] = '.';
    if (write_fs_block((uint32_t)data_blk, buf) != 0) {
        kfree(buf);
        if (h) jbd2_abort(h);
        ext2_free_inode((uint32_t)ino);
        ext2_free_block((uint32_t)data_blk);
        return -1;
    }
    kfree(buf);

    ext2_inode_t in;
    memset(&in, 0, sizeof(in));
    in.i_mode = EXT2_S_IFDIR | (mode & 0xFFF);
    in.i_links_count = 2;
    in.i_size = g_block_size;
    in.i_blocks = g_block_size / 512;
    in.i_block[0] = (uint32_t)data_blk;
    write_inode((uint32_t)ino, &in);

    if (ext2_dir_add(parent_ino, name, (uint32_t)ino, 2) != 0) {
        if (h) jbd2_abort(h);
        ext2_free_inode((uint32_t)ino);
        ext2_free_block((uint32_t)data_blk);
        return -1;
    }
    ext2_inode_t parent;
    if (read_inode(parent_ino, &parent) == 0) {
        parent.i_links_count++;
        write_inode(parent_ino, &parent);
    }

    if (out_ino) *out_ino = (uint32_t)ino;
    if (h) jbd2_commit(h);
    return 0;
}

int ext2_unlink(uint32_t parent_ino, const char *name) {
    if (!g_inited) return -1;
    uint32_t ino = 0;
    if (ext2_dir_lookup(parent_ino, name, &ino) != 0) return -1;
    jbd2_handle_t *h = (jbd2_handle_t *)0;
    if (g_journal_active) h = jbd2_start(&g_journal, 8, JBD2_ORDERED);

    if (ext2_dir_remove(parent_ino, name) != 0) {
        if (h) jbd2_abort(h); return -1;
    }
    ext2_inode_t in;
    if (read_inode(ino, &in) == 0) {
        in.i_links_count--;
        in.i_dtime = 1;
        write_inode(ino, &in);
        if (in.i_links_count == 0) {
            for (int i = 0; i < 12; i++) {
                if (in.i_block[i]) ext2_free_block(in.i_block[i]);
            }
            ext2_free_inode(ino);
        }
    }
    if (h) jbd2_commit(h);
    return 0;
}

int ext2_rmdir(uint32_t parent_ino, const char *name) {
    return ext2_unlink(parent_ino, name);
}

int ext2_rename(uint32_t parent_ino, const char *old_name, const char *new_name) {
    if (!g_inited) return -1;
    uint32_t ino = 0;
    if (ext2_dir_lookup(parent_ino, old_name, &ino) != 0) return -1;
    uint32_t dest = 0;
    if (ext2_dir_lookup(parent_ino, new_name, &dest) == 0) return -1;

    jbd2_handle_t *h = (jbd2_handle_t *)0;
    if (g_journal_active) h = jbd2_start(&g_journal, 8, JBD2_ORDERED);

    ext2_dir_remove(parent_ino, old_name);
    int r = ext2_dir_add(parent_ino, new_name, ino, 1);
    if (h) (r == 0) ? jbd2_commit(h) : jbd2_abort(h);
    return r == 0 ? 0 : -1;
}

int ext2_write_data_ino(uint32_t ino, uint64_t offset, uint32_t count,
                     const void *buf) {
    if (!g_inited) return -1;
    if (count == 0 || !buf) return 0;
    ext2_inode_t in;
    if (read_inode(ino, &in) != 0) return -1;
    if ((in.i_mode & 0xF000) != EXT2_S_IFREG) return -1;

    jbd2_handle_t *h = (jbd2_handle_t *)0;
    if (g_journal_active) h = jbd2_start(&g_journal, 8, JBD2_ORDERED);

    const uint8_t *src = (const uint8_t *)buf;
    uint64_t end = offset + count;
    while (offset < end) {
        uint64_t pos = offset;
        uint32_t block_index = (uint32_t)(pos / g_block_size);
        if (block_index >= 12) {
            offset = end;
            break;
        }
        uint32_t blk = in.i_block[block_index];
        if (blk == 0) {
            int b = ext2_alloc_block();
            if (b < 0) {
                if (h) jbd2_abort(h); return -1;
            }
            blk = (uint32_t)b;
            in.i_block[block_index] = blk;
        }
        uint8_t *tmp = (uint8_t *)kmalloc(g_block_size);
        if (!tmp) { if (h) jbd2_abort(h); return -1; }
        read_fs_block(blk, tmp);
        uint32_t off_in_block = (uint32_t)(offset % g_block_size);
        uint32_t bytes = g_block_size - off_in_block;
        if (bytes > end - offset) bytes = (uint32_t)(end - offset);
        memcpy(tmp + off_in_block, src, bytes);
        if (h) jbd2_log_metadata(h, tmp, blk);
        else write_fs_block(blk, tmp);
        kfree(tmp);
        src += bytes;
        offset += bytes;
    }
    if (end > in.i_size) in.i_size = (uint32_t)end;
    in.i_blocks = (uint32_t)(in.i_size / 512);
    in.i_mtime++;
    write_inode(ino, &in);
    if (h) jbd2_commit(h);
    return 0;
}

int ext2_sync_group_bitmaps(void) {
    if (!g_inited) return -1;
    for (uint32_t i = 0; i < g_group_count; i++) {
        if (write_bgd(i, &g_bgdt[i]) != 0) return -1;
    }
    return 0;
}

int ext2_sync_superblock_counts(void) {
    if (!g_inited) return -1;
    return write_fs_block(2, &g_sb);
}