#include "ext4.h"
#include "vfs.h"
#include "ide.h"
#include "kheap.h"
#include "string.h"
#include "sync.h"
#include "stddef.h"
#include "rtc.h"

static ext2_superblock_t ext4_sb;
static ext2_bgd_t *ext4_bgdt;
static uint32_t ext4_block_size;
static uint32_t ext4_group_count;
static uint32_t ext4_inodes_per_group;
static uint32_t ext4_drive;
static uint32_t ext4_partition_start;
static mutex_t ext4_lock;

static int32_t ext4_read_block(uint32_t block, void *buf) {
    uint32_t lba = ext4_partition_start + (block * (ext4_block_size / 512));
    uint32_t sectors = ext4_block_size / 512;
    return ide_read_sectors(ext4_drive, sectors, lba, buf);
}

static int32_t ext4_write_block(uint32_t block, const void *buf) {
    uint32_t lba = ext4_partition_start + (block * (ext4_block_size / 512));
    uint32_t sectors = ext4_block_size / 512;
    return ide_write_sectors(ext4_drive, sectors, lba, (void *)buf);
}

/* ---- Inode read/write using ext4's own block group descriptors ---- */

static int32_t ext4_read_inode_internal(uint32_t ino, ext2_inode_t *inode) {
    if (ino == 0 || !inode) return -1;

    uint32_t group = (ino - 1) / ext4_inodes_per_group;
    uint32_t index = (ino - 1) % ext4_inodes_per_group;

    if (group >= ext4_group_count) return -1;

    uint32_t inode_table_block = ext4_bgdt[group].inode_table;
    uint32_t inode_size = ext4_sb.rev_level == 0 ? 128 : ext4_sb.inode_size;
    uint32_t inodes_per_block = ext4_block_size / inode_size;
    uint32_t block_offset = index / inodes_per_block;
    uint32_t offset_in_block = index % inodes_per_block;

    uint8_t *buf = (uint8_t *)kmalloc(ext4_block_size);
    if (!buf) return -1;

    if (ext4_read_block(inode_table_block + block_offset, buf) != 0) {
        kfree(buf);
        return -1;
    }

    memcpy(inode, buf + offset_in_block * inode_size, sizeof(ext2_inode_t));
    kfree(buf);
    return 0;
}

static int32_t ext4_write_inode_internal(uint32_t ino, const ext2_inode_t *inode) {
    if (ino == 0 || !inode) return -1;

    uint32_t group = (ino - 1) / ext4_inodes_per_group;
    uint32_t index = (ino - 1) % ext4_inodes_per_group;

    if (group >= ext4_group_count) return -1;

    uint32_t inode_table_block = ext4_bgdt[group].inode_table;
    uint32_t inode_size = ext4_sb.rev_level == 0 ? 128 : ext4_sb.inode_size;
    uint32_t inodes_per_block = ext4_block_size / inode_size;
    uint32_t block_offset = index / inodes_per_block;
    uint32_t offset_in_block = index % inodes_per_block;

    uint8_t *buf = (uint8_t *)kmalloc(ext4_block_size);
    if (!buf) return -1;

    if (ext4_read_block(inode_table_block + block_offset, buf) != 0) {
        kfree(buf);
        return -1;
    }

    memcpy(buf + offset_in_block * inode_size, inode, sizeof(ext2_inode_t));

    if (ext4_write_block(inode_table_block + block_offset, buf) != 0) {
        kfree(buf);
        return -1;
    }

    kfree(buf);
    return 0;
}

/* ---- Block/Inode allocation ---- */

static uint32_t ext4_alloc_block(void) {
    mutex_lock(&ext4_lock);

    for (uint32_t g = 0; g < ext4_group_count; g++) {
        uint8_t *bitmap = (uint8_t *)kmalloc(ext4_block_size);
        if (!bitmap) {
            mutex_unlock(&ext4_lock);
            return 0;
        }

        if (ext4_read_block(ext4_bgdt[g].block_bitmap, bitmap) != 0) {
            kfree(bitmap);
            continue;
        }

        for (uint32_t i = 0; i < ext4_block_size * 8; i++) {
            if (!(bitmap[i / 8] & (1 << (i % 8)))) {
                bitmap[i / 8] |= (1 << (i % 8));
                ext4_write_block(ext4_bgdt[g].block_bitmap, bitmap);
                ext4_bgdt[g].free_blocks_count--;
                kfree(bitmap);
                mutex_unlock(&ext4_lock);
                return ext4_sb.first_data_block + g * ext4_sb.blocks_per_group + i;
            }
        }

        kfree(bitmap);
    }

    mutex_unlock(&ext4_lock);
    return 0;
}

static void ext4_free_block(uint32_t block) {
    mutex_lock(&ext4_lock);

    uint32_t g = (block - ext4_sb.first_data_block) / ext4_sb.blocks_per_group;
    uint32_t i = (block - ext4_sb.first_data_block) % ext4_sb.blocks_per_group;

    uint8_t *bitmap = (uint8_t *)kmalloc(ext4_block_size);
    if (!bitmap) {
        mutex_unlock(&ext4_lock);
        return;
    }

    if (ext4_read_block(ext4_bgdt[g].block_bitmap, bitmap) == 0) {
        bitmap[i / 8] &= ~(1 << (i % 8));
        ext4_write_block(ext4_bgdt[g].block_bitmap, bitmap);
        ext4_bgdt[g].free_blocks_count++;
    }

    kfree(bitmap);
    mutex_unlock(&ext4_lock);
}

static uint32_t ext4_alloc_inode(void) {
    mutex_lock(&ext4_lock);

    for (uint32_t g = 0; g < ext4_group_count; g++) {
        uint8_t *bitmap = (uint8_t *)kmalloc(ext4_block_size);
        if (!bitmap) {
            mutex_unlock(&ext4_lock);
            return 0;
        }

        if (ext4_read_block(ext4_bgdt[g].inode_bitmap, bitmap) != 0) {
            kfree(bitmap);
            continue;
        }

        for (uint32_t i = 0; i < ext4_inodes_per_group; i++) {
            if (!(bitmap[i / 8] & (1 << (i % 8)))) {
                bitmap[i / 8] |= (1 << (i % 8));
                ext4_write_block(ext4_bgdt[g].inode_bitmap, bitmap);
                ext4_bgdt[g].free_inodes_count--;
                kfree(bitmap);
                mutex_unlock(&ext4_lock);
                return g * ext4_inodes_per_group + i + 1;
            }
        }

        kfree(bitmap);
    }

    mutex_unlock(&ext4_lock);
    return 0;
}

static void ext4_free_inode(uint32_t ino) {
    mutex_lock(&ext4_lock);

    uint32_t g = (ino - 1) / ext4_inodes_per_group;
    uint32_t i = (ino - 1) % ext4_inodes_per_group;

    uint8_t *bitmap = (uint8_t *)kmalloc(ext4_block_size);
    if (!bitmap) {
        mutex_unlock(&ext4_lock);
        return;
    }

    if (ext4_read_block(ext4_bgdt[g].inode_bitmap, bitmap) == 0) {
        bitmap[i / 8] &= ~(1 << (i % 8));
        ext4_write_block(ext4_bgdt[g].inode_bitmap, bitmap);
        ext4_bgdt[g].free_inodes_count++;
    }

    kfree(bitmap);
    mutex_unlock(&ext4_lock);
}

/* ---- Extent tree operations ---- */

static uint64_t ext4_extent_get_phys(ext4_extent *ext) {
    return ((uint64_t)ext->ee_start_hi << 32) | ext->ee_start_lo;
}

static int32_t ext4_find_extent_in_leaf(ext4_extent_header *header, uint32_t logical_block, uint64_t *phys_block, uint32_t *length) {
    ext4_extent *ext = (ext4_extent *)(header + 1);
    uint16_t i;

    for (i = 0; i < header->eh_entries; i++) {
        if (logical_block >= ext[i].ee_block && logical_block < ext[i].ee_block + ext[i].ee_len) {
            *phys_block = ext4_extent_get_phys(&ext[i]) + (logical_block - ext[i].ee_block);
            *length = ext[i].ee_len - (logical_block - ext[i].ee_block);
            return 0;
        }
    }

    return -1;
}

static int32_t ext4_traverse_extent_tree(ext4_extent_header *header, uint32_t logical_block, uint64_t *phys_block, uint32_t *length) {
    if (header->eh_depth == 0) {
        return ext4_find_extent_in_leaf(header, logical_block, phys_block, length);
    }

    ext4_extent_idx *idx = (ext4_extent_idx *)(header + 1);
    uint16_t i;

    ext4_extent_idx *target = NULL;
    for (i = 0; i < header->eh_entries; i++) {
        if (logical_block >= idx[i].ei_block) {
            target = &idx[i];
        } else {
            break;
        }
    }

    if (!target) return -1;

    uint64_t child_block = ((uint64_t)target->ei_leaf_hi << 32) | target->ei_leaf_lo;
    void *child_buf = kmalloc(ext4_block_size);
    if (!child_buf) return -1;

    if (ext4_read_block((uint32_t)child_block, child_buf) != 0) {
        kfree(child_buf);
        return -1;
    }

    ext4_extent_header *child_header = (ext4_extent_header *)child_buf;
    int32_t result = ext4_traverse_extent_tree(child_header, logical_block, phys_block, length);

    kfree(child_buf);
    return result;
}

/* Append a new extent to the leaf node of the extent tree.
 * For simplicity, this only handles depth=0 (leaf) trees. */
static int32_t ext4_append_extent(uint32_t ino, ext2_inode_t *e2inode,
                                  uint32_t logical_block, uint32_t phys_block, uint32_t len) {
    ext4_extent_header *header = (ext4_extent_header *)e2inode->i_block;

    if (header->eh_magic != 0xF30A) {
        /* Initialize extent header */
        header->eh_magic = 0xF30A;
        header->eh_entries = 0;
        header->eh_max = 4; /* max extents in inode inline area */
        header->eh_depth = 0;
        header->eh_generation = 0;
    }

    if (header->eh_depth == 0 && header->eh_entries < header->eh_max) {
        /* Add extent to inline leaf */
        ext4_extent *ext = (ext4_extent *)(header + 1) + header->eh_entries;
        ext->ee_block = logical_block;
        ext->ee_len = (uint16_t)len;
        ext->ee_start_hi = 0; /* high 16 bits - always 0 on 32-bit */
        ext->ee_start_lo = phys_block;
        header->eh_entries++;
        ext4_write_inode_internal(ino, e2inode);
        return 0;
    }

    /* If inline space exhausted, we'd need to grow the extent tree.
     * For now, return error - this is a limitation for very large files. */
    return -1;
}

/* ---- File read/write using extent tree ---- */

int32_t ext4_read_extent(inode_t *inode, void *buf, uint32_t offset, uint32_t count) {
    if (!inode || !buf) return -1;

    mutex_lock(&ext4_lock);

    ext2_inode_t ext4_inode;
    if (ext4_read_inode_internal(inode->ino, &ext4_inode) != 0) {
        mutex_unlock(&ext4_lock);
        return -1;
    }

    if (offset >= ext4_inode.i_size) {
        mutex_unlock(&ext4_lock);
        return 0;
    }
    if (offset + count > ext4_inode.i_size) {
        count = ext4_inode.i_size - offset;
    }

    ext4_extent_header *header = (ext4_extent_header *)ext4_inode.i_block;
    if (header->eh_magic != 0xF30A) {
        mutex_unlock(&ext4_lock);
        return -1;
    }

    uint32_t block_offset = offset / ext4_block_size;
    uint32_t byte_offset = offset % ext4_block_size;
    uint32_t bytes_remaining = count;
    uint8_t *dst = (uint8_t *)buf;
    uint32_t blocks_read = 0;

    while (bytes_remaining > 0) {
        uint64_t phys_block;
        uint32_t extent_length;

        if (ext4_traverse_extent_tree(header, block_offset + blocks_read, &phys_block, &extent_length) != 0) {
            /* Hole: fill with zeros */
            uint32_t chunk = ext4_block_size - byte_offset;
            if (chunk > bytes_remaining) chunk = bytes_remaining;
            memset(dst, 0, chunk);
            dst += chunk;
            bytes_remaining -= chunk;
            blocks_read++;
            byte_offset = 0;
            continue;
        }

        void *block_buf = kmalloc(ext4_block_size);
        if (!block_buf) {
            mutex_unlock(&ext4_lock);
            return (int32_t)(count - bytes_remaining);
        }

        uint32_t i;
        for (i = 0; i < extent_length && bytes_remaining > 0; i++) {
            if (ext4_read_block((uint32_t)(phys_block + i), block_buf) != 0) {
                kfree(block_buf);
                mutex_unlock(&ext4_lock);
                return (int32_t)(count - bytes_remaining);
            }

            uint32_t src_offset = (i == 0) ? byte_offset : 0;
            uint32_t chunk = ext4_block_size - src_offset;
            if (chunk > bytes_remaining) chunk = bytes_remaining;

            memcpy(dst, (uint8_t *)block_buf + src_offset, chunk);
            dst += chunk;
            bytes_remaining -= chunk;
        }

        kfree(block_buf);
        blocks_read += extent_length;
        byte_offset = 0;
    }

    mutex_unlock(&ext4_lock);
    return (int32_t)(count - bytes_remaining);
}

int32_t ext4_write_extent(inode_t *inode, const void *buf, uint32_t offset, uint32_t count) {
    if (!inode || !buf) return -1;

    mutex_lock(&ext4_lock);

    ext2_inode_t ext4_inode;
    if (ext4_read_inode_internal(inode->ino, &ext4_inode) != 0) {
        mutex_unlock(&ext4_lock);
        return -1;
    }

    ext4_extent_header *header = (ext4_extent_header *)ext4_inode.i_block;

    /* If no extent header yet, initialize one */
    if (header->eh_magic != 0xF30A) {
        memset(ext4_inode.i_block, 0, sizeof(ext4_inode.i_block));
        header->eh_magic = 0xF30A;
        header->eh_entries = 0;
        header->eh_max = 4;
        header->eh_depth = 0;
        header->eh_generation = 0;
    }

    uint32_t block_offset = offset / ext4_block_size;
    uint32_t byte_offset = offset % ext4_block_size;
    uint32_t bytes_remaining = count;
    const uint8_t *src = (const uint8_t *)buf;
    uint32_t blocks_written = 0;

    while (bytes_remaining > 0) {
        uint64_t phys_block;
        uint32_t extent_length;

        /* Try to find existing extent */
        if (ext4_traverse_extent_tree(header, block_offset + blocks_written, &phys_block, &extent_length) != 0) {
            /* No extent for this block - allocate a new one */
            uint32_t new_block = ext4_alloc_block();
            if (new_block == 0) break;

            /* Append extent */
            if (ext4_append_extent(inode->ino, &ext4_inode, block_offset + blocks_written, new_block, 1) != 0) {
                ext4_free_block(new_block);
                break;
            }

            /* Re-read the inode after modification */
            ext4_read_inode_internal(inode->ino, &ext4_inode);
            header = (ext4_extent_header *)ext4_inode.i_block;
            phys_block = new_block;
            extent_length = 1;
        }

        void *block_buf = kmalloc(ext4_block_size);
        if (!block_buf) break;

        uint32_t i;
        for (i = 0; i < extent_length && bytes_remaining > 0; i++) {
            uint32_t dst_offset = (i == 0) ? byte_offset : 0;
            uint32_t chunk = ext4_block_size - dst_offset;
            if (chunk > bytes_remaining) chunk = bytes_remaining;

            if (dst_offset != 0 || chunk < ext4_block_size) {
                if (ext4_read_block((uint32_t)(phys_block + i), block_buf) != 0) {
                    kfree(block_buf);
                    mutex_unlock(&ext4_lock);
                    return (int32_t)(count - bytes_remaining);
                }
            } else {
                memset(block_buf, 0, ext4_block_size);
            }

            memcpy((uint8_t *)block_buf + dst_offset, src, chunk);
            src += chunk;

            if (ext4_write_block((uint32_t)(phys_block + i), block_buf) != 0) {
                kfree(block_buf);
                mutex_unlock(&ext4_lock);
                return (int32_t)(count - bytes_remaining);
            }

            bytes_remaining -= chunk;
        }

        kfree(block_buf);
        blocks_written += extent_length;
        byte_offset = 0;
    }

    /* Update inode size */
    if (offset + (count - bytes_remaining) > ext4_inode.i_size) {
        ext4_inode.i_size = offset + (count - bytes_remaining);
    }
    ext4_inode.i_mtime = rtc_get_timestamp();
    ext4_write_inode_internal(inode->ino, &ext4_inode);

    mutex_unlock(&ext4_lock);
    return (int32_t)(count - bytes_remaining);
}

/* ---- Directory operations ---- */

static int32_t ext4_lookup(uint32_t dir_ino, const char *name, uint32_t *out_ino) {
    ext2_inode_t dir_inode;
    if (ext4_read_inode_internal(dir_ino, &dir_inode) != 0) return -1;
    if ((dir_inode.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR) return -1;

    /* Read directory data using extent tree */
    inode_t tmp_inode;
    memset(&tmp_inode, 0, sizeof(inode_t));
    tmp_inode.ino = dir_ino;

    uint8_t *buf = (uint8_t *)kmalloc(dir_inode.i_size);
    if (!buf) return -1;

    if (ext4_read_extent(&tmp_inode, buf, 0, dir_inode.i_size) < 0) {
        kfree(buf);
        return -1;
    }

    uint32_t pos = 0;
    while (pos < dir_inode.i_size) {
        ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(buf + pos);
        if (entry->rec_len == 0) break;

        if (entry->inode != 0) {
            if (entry->name_len == strlen(name) &&
                memcmp(entry->name, name, entry->name_len) == 0) {
                *out_ino = entry->inode;
                kfree(buf);
                return 0;
            }
        }

        pos += entry->rec_len;
    }

    kfree(buf);
    return -1;
}

static int32_t ext4_add_dir_entry(uint32_t dir_ino, uint32_t target_ino,
                                  const char *name, uint8_t file_type) {
    ext2_inode_t dir_inode;
    if (ext4_read_inode_internal(dir_ino, &dir_inode) != 0) return -1;

    inode_t tmp_inode;
    memset(&tmp_inode, 0, sizeof(inode_t));
    tmp_inode.ino = dir_ino;

    uint8_t *dir_buf = (uint8_t *)kmalloc(dir_inode.i_size + ext4_block_size);
    if (!dir_buf) return -1;

    if (dir_inode.i_size > 0) {
        ext4_read_extent(&tmp_inode, dir_buf, 0, dir_inode.i_size);
    }

    uint32_t name_len = strlen(name);
    uint32_t needed = sizeof(ext2_dir_entry_t) - 255 + name_len;
    needed = (needed + 3) & ~3;

    /* Try to fit in existing directory blocks */
    int added = 0;
    uint32_t pos = 0;
    while (pos < dir_inode.i_size) {
        ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(dir_buf + pos);
        if (entry->rec_len == 0) break;

        uint32_t actual_len = sizeof(ext2_dir_entry_t) - 255 + entry->name_len;
        actual_len = (actual_len + 3) & ~3;

        if (entry->rec_len - actual_len >= needed) {
            uint16_t orig_rec_len = entry->rec_len;
            entry->rec_len = (uint16_t)actual_len;

            ext2_dir_entry_t *new_entry = (ext2_dir_entry_t *)(dir_buf + pos + actual_len);
            new_entry->inode = target_ino;
            new_entry->name_len = (uint8_t)name_len;
            new_entry->file_type = file_type;
            new_entry->rec_len = orig_rec_len - (uint16_t)actual_len;
            memcpy(new_entry->name, name, name_len);

            ext4_write_extent(&tmp_inode, dir_buf, 0, dir_inode.i_size);
            added = 1;
            break;
        }

        pos += entry->rec_len;
    }

    if (!added) {
        /* Append new block */
        memset(dir_buf + dir_inode.i_size, 0, ext4_block_size);

        ext2_dir_entry_t *new_entry = (ext2_dir_entry_t *)(dir_buf + dir_inode.i_size);
        new_entry->inode = target_ino;
        new_entry->name_len = (uint8_t)name_len;
        new_entry->file_type = file_type;
        new_entry->rec_len = (uint16_t)ext4_block_size;
        memcpy(new_entry->name, name, name_len);

        ext4_write_extent(&tmp_inode, dir_buf, 0, dir_inode.i_size + ext4_block_size);
    }

    kfree(dir_buf);
    return 0;
}

static int32_t ext4_unlink_op(uint32_t dir_ino, const char *name) {
    ext2_inode_t dir_inode;
    if (ext4_read_inode_internal(dir_ino, &dir_inode) != 0) return -1;

    inode_t tmp_inode;
    memset(&tmp_inode, 0, sizeof(inode_t));
    tmp_inode.ino = dir_ino;

    uint8_t *buf = (uint8_t *)kmalloc(dir_inode.i_size);
    if (!buf) return -1;
    ext4_read_extent(&tmp_inode, buf, 0, dir_inode.i_size);

    uint32_t pos = 0;
    uint32_t prev_pos = 0;

    while (pos < dir_inode.i_size) {
        ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(buf + pos);
        if (entry->rec_len == 0) break;

        if (entry->inode != 0 && entry->name_len == strlen(name) &&
            memcmp(entry->name, name, entry->name_len) == 0) {

            entry->inode = 0;

            if (prev_pos != pos) {
                ext2_dir_entry_t *prev = (ext2_dir_entry_t *)(buf + prev_pos);
                prev->rec_len += entry->rec_len;
            }

            ext4_write_extent(&tmp_inode, buf, 0, dir_inode.i_size);
            kfree(buf);
            return 0;
        }

        prev_pos = pos;
        pos += entry->rec_len;
    }

    kfree(buf);
    return -1;
}

/* ---- VFS inode operations ---- */

static dentry_t *ext4_vfs_lookup(dentry_t *dir, const char *name);
static int32_t ext4_vfs_create(dentry_t *dir, const char *name, uint32_t mode);
static int32_t ext4_vfs_mkdir(dentry_t *dir, const char *name, uint32_t mode);
static int32_t ext4_vfs_unlink(dentry_t *dir, const char *name);
static int32_t ext4_vfs_rmdir(dentry_t *dir, const char *name);
static int32_t ext4_vfs_rename(dentry_t *old_dir, const char *old_name, dentry_t *new_dir, const char *new_name);
static int32_t ext4_vfs_symlink(dentry_t *dir, const char *name, const char *target);
static int32_t ext4_vfs_readlink(dentry_t *dentry, char *buf, uint32_t size);
static int32_t ext4_vfs_link(dentry_t *old_dir, const char *old_name,
                             dentry_t *new_dir, const char *new_name);

static inode_ops_t ext4_inode_ops = {
    .lookup = ext4_vfs_lookup,
    .create = ext4_vfs_create,
    .mkdir = ext4_vfs_mkdir,
    .unlink = ext4_vfs_unlink,
    .rmdir = ext4_vfs_rmdir,
    .rename = ext4_vfs_rename,
    .readlink = ext4_vfs_readlink,
    .symlink = ext4_vfs_symlink,
    .link = ext4_vfs_link
};

static dentry_t *ext4_vfs_lookup(dentry_t *dir, const char *name) {
    if (!dir || !dir->inode) return NULL;

    uint32_t out_ino;
    if (ext4_lookup(dir->inode->ino, name, &out_ino) != 0) return NULL;

    ext2_inode_t e2inode;
    if (ext4_read_inode_internal(out_ino, &e2inode) != 0) return NULL;

    inode_t *vfs_inode = (inode_t *)kmalloc(sizeof(inode_t));
    if (!vfs_inode) return NULL;
    memset(vfs_inode, 0, sizeof(inode_t));

    vfs_inode->ino = out_ino;
    vfs_inode->mode = 0;
    uint32_t fmt = e2inode.i_mode & EXT2_S_IFMT;
    if (fmt == EXT2_S_IFDIR)  vfs_inode->mode |= FILE_MODE_DIR;
    if (fmt == EXT2_S_IFREG)  vfs_inode->mode |= FILE_MODE_REG;
    if (fmt == EXT2_S_IFLNK)  vfs_inode->mode |= FILE_MODE_LNK;
    if (e2inode.i_mode & 0x0100) vfs_inode->mode |= FILE_MODE_READ;
    if (e2inode.i_mode & 0x0080) vfs_inode->mode |= FILE_MODE_WRITE;
    if (e2inode.i_mode & 0x0040) vfs_inode->mode |= FILE_MODE_EXEC;
    vfs_inode->uid = e2inode.i_uid;
    vfs_inode->gid = e2inode.i_gid;
    vfs_inode->size = e2inode.i_size;
    vfs_inode->nlinks = e2inode.i_links_count;
    vfs_inode->atime = e2inode.i_atime;
    vfs_inode->mtime = e2inode.i_mtime;
    vfs_inode->ctime = e2inode.i_ctime;
    vfs_inode->sb = dir->inode->sb;
    vfs_inode->ops = &ext4_inode_ops;
    vfs_inode->private_data = (void *)(uintptr_t)out_ino;

    dentry_t *d = (dentry_t *)kmalloc(sizeof(dentry_t));
    if (!d) { kfree(vfs_inode); return NULL; }
    memset(d, 0, sizeof(dentry_t));
    strncpy(d->name, name, 255);
    d->name[255] = '\0';
    d->inode = vfs_inode;
    d->parent = dir;
    vfs_inode->dentries = d;

    d->next_sibling = dir->child;
    dir->child = d;

    return d;
}

static int32_t ext4_vfs_create(dentry_t *dir, const char *name, uint32_t mode) {
    if (!dir || !dir->inode) return -1;

    uint32_t new_ino = ext4_alloc_inode();
    if (new_ino == 0) return -1;

    ext2_inode_t new_inode;
    memset(&new_inode, 0, sizeof(ext2_inode_t));
    new_inode.i_mode = EXT2_S_IFREG | (mode & 0x0FFF);
    new_inode.i_links_count = 1;
    new_inode.i_mtime = rtc_get_timestamp();
    new_inode.i_atime = new_inode.i_mtime;
    new_inode.i_ctime = new_inode.i_mtime;
    ext4_write_inode_internal(new_ino, &new_inode);

    return ext4_add_dir_entry(dir->inode->ino, new_ino, name, 1);
}

static int32_t ext4_vfs_mkdir(dentry_t *dir, const char *name, uint32_t mode) {
    if (!dir || !dir->inode) return -1;

    uint32_t new_ino = ext4_alloc_inode();
    if (new_ino == 0) return -1;

    uint32_t new_block = ext4_alloc_block();
    if (new_block == 0) {
        ext4_free_inode(new_ino);
        return -1;
    }

    ext2_inode_t new_inode;
    memset(&new_inode, 0, sizeof(ext2_inode_t));
    new_inode.i_mode = EXT2_S_IFDIR | (mode & 0x0FFF);
    new_inode.i_links_count = 2;
    new_inode.i_block[0] = new_block;
    new_inode.i_size = ext4_block_size;
    new_inode.i_mtime = rtc_get_timestamp();
    new_inode.i_atime = new_inode.i_mtime;
    new_inode.i_ctime = new_inode.i_mtime;

    /* Initialize extent header for the new directory */
    ext4_extent_header *header = (ext4_extent_header *)new_inode.i_block;
    header->eh_magic = 0xF30A;
    header->eh_entries = 0;
    header->eh_max = 4;
    header->eh_depth = 0;
    header->eh_generation = 0;

    ext4_write_inode_internal(new_ino, &new_inode);

    /* Write . and .. entries */
    uint8_t *block_buf = (uint8_t *)kmalloc(ext4_block_size);
    memset(block_buf, 0, ext4_block_size);

    ext2_dir_entry_t *dot = (ext2_dir_entry_t *)block_buf;
    dot->inode = new_ino;
    dot->name_len = 1;
    dot->file_type = 2;
    dot->rec_len = 12;
    dot->name[0] = '.';

    ext2_dir_entry_t *dotdot = (ext2_dir_entry_t *)(block_buf + 12);
    dotdot->inode = dir->inode->ino;
    dotdot->name_len = 2;
    dotdot->file_type = 2;
    dotdot->rec_len = (uint16_t)(ext4_block_size - 12);
    dotdot->name[0] = '.';
    dotdot->name[1] = '.';

    ext4_write_block(new_block, block_buf);
    kfree(block_buf);

    /* Update parent link count */
    ext2_inode_t dir_inode;
    ext4_read_inode_internal(dir->inode->ino, &dir_inode);
    dir_inode.i_links_count++;
    ext4_write_inode_internal(dir->inode->ino, &dir_inode);

    return ext4_add_dir_entry(dir->inode->ino, new_ino, name, 2);
}

static int32_t ext4_vfs_unlink(dentry_t *dir, const char *name) {
    if (!dir || !dir->inode) return -1;
    return ext4_unlink_op(dir->inode->ino, name);
}

static int32_t ext4_vfs_rmdir(dentry_t *dir, const char *name) {
    if (!dir || !dir->inode) return -1;

    /* Look up the target inode */
    uint32_t target_ino;
    if (ext4_lookup(dir->inode->ino, name, &target_ino) != 0) return -1;

    /* Check if the directory is empty (only . and .. entries) */
    ext2_inode_t target_inode;
    if (ext4_read_inode_internal(target_ino, &target_inode) != 0) return -1;

    inode_t tmp_inode;
    memset(&tmp_inode, 0, sizeof(inode_t));
    tmp_inode.ino = target_ino;

    uint8_t *dir_buf = (uint8_t *)kmalloc(target_inode.i_size);
    if (!dir_buf) return -1;
    ext4_read_extent(&tmp_inode, dir_buf, 0, target_inode.i_size);

    uint32_t pos = 0;
    while (pos < target_inode.i_size) {
        ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(dir_buf + pos);
        if (entry->rec_len == 0) break;
        if (entry->inode != 0 && !(entry->name_len == 1 && entry->name[0] == '.') &&
            !(entry->name_len == 2 && entry->name[0] == '.' && entry->name[1] == '.')) {
            kfree(dir_buf);
            return -1; /* Directory not empty */
        }
        pos += entry->rec_len;
    }
    kfree(dir_buf);

    /* Decrement parent link count */
    ext2_inode_t parent_inode;
    if (ext4_read_inode_internal(dir->inode->ino, &parent_inode) == 0) {
        if (parent_inode.i_links_count > 0) {
            parent_inode.i_links_count--;
            ext4_write_inode_internal(dir->inode->ino, &parent_inode);
        }
    }

    /* Decrement target link count */
    if (target_inode.i_links_count > 0) {
        target_inode.i_links_count--;
        ext4_write_inode_internal(target_ino, &target_inode);
    }

    return ext4_unlink_op(dir->inode->ino, name);
}

static int32_t ext4_vfs_rename(dentry_t *old_dir, const char *old_name,
                               dentry_t *new_dir, const char *new_name) {
    if (!old_dir || !old_dir->inode || !new_dir || !new_dir->inode) return -1;

    uint32_t target_ino;
    if (ext4_lookup(old_dir->inode->ino, old_name, &target_ino) != 0) return -1;

    /* Remove existing new name if present */
    uint32_t existing_ino;
    if (ext4_lookup(new_dir->inode->ino, new_name, &existing_ino) == 0) {
        ext4_unlink_op(new_dir->inode->ino, new_name);
    }

    ext2_inode_t target_inode;
    if (ext4_read_inode_internal(target_ino, &target_inode) != 0) return -1;

    uint8_t file_type = 1;
    uint32_t fmt = target_inode.i_mode & EXT2_S_IFMT;
    if (fmt == EXT2_S_IFDIR) file_type = 2;
    else if (fmt == EXT2_S_IFLNK) file_type = 7;

    /* Add new entry */
    ext4_add_dir_entry(new_dir->inode->ino, target_ino, new_name, file_type);

    /* Remove old entry */
    ext4_unlink_op(old_dir->inode->ino, old_name);

    /* Update .. if directory moved */
    if (fmt == EXT2_S_IFDIR && old_dir->inode->ino != new_dir->inode->ino) {
        ext2_inode_t moved_inode;
        if (ext4_read_inode_internal(target_ino, &moved_inode) == 0) {
            /* For extent-based dirs, read first block */
            ext4_extent_header *hdr = (ext4_extent_header *)moved_inode.i_block;
            if (hdr->eh_magic == 0xF30A && hdr->eh_entries > 0) {
                ext4_extent *ext = (ext4_extent *)(hdr + 1);
                uint32_t first_block = (uint32_t)ext4_extent_get_phys(ext);
                uint8_t *block_buf = (uint8_t *)kmalloc(ext4_block_size);
                if (block_buf) {
                    ext4_read_block(first_block, block_buf);
                    ext2_dir_entry_t *dotdot = (ext2_dir_entry_t *)(block_buf + 12);
                    if (dotdot->name_len == 2 && dotdot->name[0] == '.' && dotdot->name[1] == '.') {
                        dotdot->inode = new_dir->inode->ino;
                        ext4_write_block(first_block, block_buf);
                    }
                    kfree(block_buf);
                }
            }
        }
    }

    return 0;
}

static int32_t ext4_vfs_symlink(dentry_t *dir, const char *name, const char *target) {
    if (!dir || !dir->inode || !name || !target) return -1;

    uint32_t new_ino = ext4_alloc_inode();
    if (new_ino == 0) return -1;

    ext2_inode_t new_inode;
    memset(&new_inode, 0, sizeof(ext2_inode_t));
    new_inode.i_mode = EXT2_S_IFLNK | 0x01FF;
    new_inode.i_links_count = 1;
    uint32_t now = rtc_get_timestamp();
    new_inode.i_mtime = now;
    new_inode.i_atime = now;
    new_inode.i_ctime = now;

    uint32_t target_len = strlen(target);

    if (target_len < 60) {
        memcpy(new_inode.i_block, target, target_len);
        new_inode.i_size = target_len;
        ext4_write_inode_internal(new_ino, &new_inode);
    } else {
        uint32_t new_block = ext4_alloc_block();
        if (new_block == 0) {
            ext4_free_inode(new_ino);
            return -1;
        }
        new_inode.i_block[0] = new_block;
        new_inode.i_size = target_len;

        uint8_t *block_buf = (uint8_t *)kmalloc(ext4_block_size);
        if (!block_buf) {
            ext4_free_block(new_block);
            ext4_free_inode(new_ino);
            return -1;
        }
        memset(block_buf, 0, ext4_block_size);
        memcpy(block_buf, target, target_len);
        ext4_write_block(new_block, block_buf);
        kfree(block_buf);

        ext4_write_inode_internal(new_ino, &new_inode);
    }

    return ext4_add_dir_entry(dir->inode->ino, new_ino, name, 7);
}

static int32_t ext4_vfs_readlink(dentry_t *dentry, char *buf, uint32_t size) {
    if (!dentry || !dentry->inode || !buf || size == 0) return -1;

    ext2_inode_t e2inode;
    if (ext4_read_inode_internal(dentry->inode->ino, &e2inode) != 0) return -1;

    if ((e2inode.i_mode & EXT2_S_IFMT) != EXT2_S_IFLNK) return -1;

    uint32_t link_len = e2inode.i_size;
    if (link_len == 0) return -1;
    if (link_len > size - 1) link_len = size - 1;

    if (link_len < 60) {
        memcpy(buf, e2inode.i_block, link_len);
    } else {
        if (e2inode.i_block[0] == 0) return -1;
        uint8_t *block_buf = (uint8_t *)kmalloc(ext4_block_size);
        if (!block_buf) return -1;
        ext4_read_block(e2inode.i_block[0], block_buf);
        memcpy(buf, block_buf, link_len);
        kfree(block_buf);
    }

    buf[link_len] = '\0';
    return (int32_t)link_len;
}

static int32_t ext4_vfs_link(dentry_t *old_dir, const char *old_name,
                             dentry_t *new_dir, const char *new_name) {
    if (!old_dir || !old_dir->inode || !new_dir || !new_dir->inode) return -1;

    uint32_t target_ino;
    if (ext4_lookup(old_dir->inode->ino, old_name, &target_ino) != 0) return -1;

    ext2_inode_t target_inode;
    if (ext4_read_inode_internal(target_ino, &target_inode) != 0) return -1;

    /* Don't allow hard links to directories */
    if ((target_inode.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR) return -1;

    /* Add new directory entry */
    ext4_add_dir_entry(new_dir->inode->ino, target_ino, new_name, 1);

    /* Increment link count */
    target_inode.i_links_count++;
    ext4_write_inode_internal(target_ino, &target_inode);

    return 0;
}

int32_t ext4_chmod(uint32_t ino, uint32_t mode) {
    ext2_inode_t inode;
    if (ext4_read_inode_internal(ino, &inode) != 0) return -1;
    inode.i_mode = (inode.i_mode & EXT2_S_IFMT) | (mode & 0x0FFF);
    inode.i_ctime = rtc_get_timestamp();
    ext4_write_inode_internal(ino, &inode);
    return 0;
}

int32_t ext4_chown(uint32_t ino, uint32_t uid, uint32_t gid) {
    ext2_inode_t inode;
    if (ext4_read_inode_internal(ino, &inode) != 0) return -1;
    inode.i_uid = (uint16_t)uid;
    inode.i_gid = (uint16_t)gid;
    inode.i_ctime = rtc_get_timestamp();
    ext4_write_inode_internal(ino, &inode);
    return 0;
}

int32_t ext4_utimes(uint32_t ino, uint32_t atime, uint32_t mtime) {
    ext2_inode_t inode;
    if (ext4_read_inode_internal(ino, &inode) != 0) return -1;
    inode.i_atime = atime;
    inode.i_mtime = mtime;
    inode.i_ctime = rtc_get_timestamp();
    ext4_write_inode_internal(ino, &inode);
    return 0;
}

/* ---- VFS file operations ---- */

static int32_t ext4_file_open(inode_t *inode, file_t *file) {
    (void)inode;
    (void)file;
    return 0;
}

static int32_t ext4_file_read(file_t *file, void *buf, uint32_t count) {
    if (!file || !file->inode) return -1;
    int32_t ret = ext4_read_extent(file->inode, buf, file->offset, count);
    if (ret > 0) file->offset += ret;
    return ret;
}

static int32_t ext4_file_write(file_t *file, const void *buf, uint32_t count) {
    if (!file || !file->inode) return -1;
    int32_t ret = ext4_write_extent(file->inode, buf, file->offset, count);
    if (ret > 0) {
        file->offset += ret;
        file->inode->size = file->offset > file->inode->size ? file->offset : file->inode->size;
    }
    return ret;
}

static int32_t ext4_file_close(file_t *file) {
    (void)file;
    return 0;
}

static int32_t ext4_file_seek(file_t *file, int32_t offset, int32_t whence) {
    if (!file || !file->inode) return -1;
    int32_t new_offset;
    switch (whence) {
        case SEEK_SET: new_offset = offset; break;
        case SEEK_CUR: new_offset = (int32_t)file->offset + offset; break;
        case SEEK_END: new_offset = (int32_t)file->inode->size + offset; break;
        default: return -1;
    }
    if (new_offset < 0) return -1;
    file->offset = (uint32_t)new_offset;
    return new_offset;
}

int32_t ext4_fsync(uint32_t ino) {
    /* EXT4 with journaling: ensure all data is committed to disk.
     * For now, writes are synchronous. Journal checkpoint could be added here. */
    (void)ino;
    return 0;
}

file_ops_t ext4_file_ops = {
    .open = ext4_file_open,
    .read = ext4_file_read,
    .write = ext4_file_write,
    .close = ext4_file_close,
    .seek = ext4_file_seek,
    .ioctl = NULL
};

/* ---- Init and mount ---- */

int32_t ext4_init(uint32_t drive, uint32_t partition_start) {
    ext4_drive = drive;
    ext4_partition_start = partition_start;

    mutex_init(&ext4_lock);

    void *buf = kmalloc(1024);
    if (!buf) return -1;

    uint32_t lba = partition_start + 2;
    if (ide_read_sectors(drive, 2, lba, buf) != 0) {
        kfree(buf);
        return -1;
    }

    memcpy(&ext4_sb, buf, sizeof(ext2_superblock_t));
    kfree(buf);

    if (ext4_sb.magic != EXT4_SUPER_MAGIC) return -1;

    ext4_block_size = 1024 << ext4_sb.log_block_size;
    ext4_inodes_per_group = ext4_sb.inodes_per_group;
    ext4_group_count = (ext4_sb.blocks_count + ext4_sb.blocks_per_group - 1) / ext4_sb.blocks_per_group;

    uint32_t bgdt_block = ext4_sb.first_data_block + 1;
    uint32_t bgdt_size = ext4_group_count * sizeof(ext2_bgd_t);
    uint32_t bgdt_blocks = (bgdt_size + ext4_block_size - 1) / ext4_block_size;

    ext4_bgdt = (ext2_bgd_t *)kmalloc(bgdt_blocks * ext4_block_size);
    if (!ext4_bgdt) return -1;

    for (uint32_t i = 0; i < bgdt_blocks; i++) {
        if (ext4_read_block(bgdt_block + i, (uint8_t *)ext4_bgdt + i * ext4_block_size) != 0) {
            kfree(ext4_bgdt);
            ext4_bgdt = NULL;
            return -1;
        }
    }

    if (ext4_sb.feature_compat & EXT4_FEATURE_COMPAT_HAS_JOURNAL) {
        ext4_journal_recover();
    }

    return 0;
}

int32_t ext4_mount(superblock_t *sb, void *data) {
    (void)data;

    if (!sb) return -1;

    if (ext4_init(0, data ? (uint32_t)(uintptr_t)data : 0) != 0) return -1;

    sb->fs_type = FS_TYPE_EXT4;
    sb->block_size = ext4_block_size;
    sb->total_blocks = ext4_sb.blocks_count;
    sb->free_blocks = ext4_sb.free_blocks_count;
    sb->fs_data = &ext4_sb;

    ext2_inode_t root_e2inode;
    if (ext4_read_inode_internal(2, &root_e2inode) != 0) return -1;

    inode_t *root_inode = (inode_t *)kmalloc(sizeof(inode_t));
    if (!root_inode) return -1;
    memset(root_inode, 0, sizeof(inode_t));
    root_inode->ino = 2;
    root_inode->mode = FILE_MODE_DIR | FILE_MODE_READ | FILE_MODE_WRITE;
    root_inode->size = root_e2inode.i_size;
    root_inode->nlinks = root_e2inode.i_links_count;
    root_inode->uid = root_e2inode.i_uid;
    root_inode->gid = root_e2inode.i_gid;
    root_inode->atime = root_e2inode.i_atime;
    root_inode->mtime = root_e2inode.i_mtime;
    root_inode->ctime = root_e2inode.i_ctime;
    root_inode->sb = sb;
    root_inode->ops = &ext4_inode_ops;
    root_inode->private_data = (void *)(uintptr_t)2;

    dentry_t *root_d = (dentry_t *)kmalloc(sizeof(dentry_t));
    if (!root_d) {
        kfree(root_inode);
        return -1;
    }
    memset(root_d, 0, sizeof(dentry_t));
    root_d->name[0] = '/';
    root_d->inode = root_inode;
    root_d->parent = root_d;
    root_inode->dentries = root_d;

    sb->root = root_inode;
    return 0;
}
