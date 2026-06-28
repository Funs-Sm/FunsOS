#include "ext2.h"
#include "vfs.h"
#include "ide.h"
#include "kheap.h"
#include "string.h"
#include "sync.h"
#include "stddef.h"
#include "rtc.h"

static ext2_superblock_t sb;
static ext2_bgd_t *bgdt;
static uint32_t block_size;
static uint32_t group_count;
static uint32_t inodes_per_group;
static mutex_t ext2_lock;
static uint32_t ext2_drive;
static uint32_t ext2_partition_start;

int32_t ext2_read_block(uint32_t block, void *buf) {
    uint32_t lba = ext2_partition_start + (block * (block_size / 512));
    uint32_t sectors = block_size / 512;
    return ide_read_sectors(ext2_drive, sectors, lba, buf);
}

int32_t ext2_write_block(uint32_t block, const void *buf) {
    uint32_t lba = ext2_partition_start + (block * (block_size / 512));
    uint32_t sectors = block_size / 512;
    return ide_write_sectors(ext2_drive, sectors, lba, (void *)buf);
}

int32_t ext2_read_inode(uint32_t ino, ext2_inode_t *inode) {
    if (ino == 0 || !inode) return -1;

    uint32_t group = (ino - 1) / inodes_per_group;
    uint32_t index = (ino - 1) % inodes_per_group;

    if (group >= group_count) return -1;

    uint32_t inode_table_block = bgdt[group].inode_table;
    uint32_t inode_size = sb.rev_level == 0 ? 128 : sb.inode_size;
    uint32_t inodes_per_block = block_size / inode_size;
    uint32_t block_offset = index / inodes_per_block;
    uint32_t offset_in_block = index % inodes_per_block;

    uint8_t *buf = (uint8_t *)kmalloc(block_size);
    if (!buf) return -1;

    if (ext2_read_block(inode_table_block + block_offset, buf) != 0) {
        kfree(buf);
        return -1;
    }

    memcpy(inode, buf + offset_in_block * inode_size, sizeof(ext2_inode_t));
    kfree(buf);
    return 0;
}

int32_t ext2_write_inode(uint32_t ino, const ext2_inode_t *inode) {
    if (ino == 0 || !inode) return -1;

    uint32_t group = (ino - 1) / inodes_per_group;
    uint32_t index = (ino - 1) % inodes_per_group;

    if (group >= group_count) return -1;

    uint32_t inode_table_block = bgdt[group].inode_table;
    uint32_t inode_size = sb.rev_level == 0 ? 128 : sb.inode_size;
    uint32_t inodes_per_block = block_size / inode_size;
    uint32_t block_offset = index / inodes_per_block;
    uint32_t offset_in_block = index % inodes_per_block;

    uint8_t *buf = (uint8_t *)kmalloc(block_size);
    if (!buf) return -1;

    if (ext2_read_block(inode_table_block + block_offset, buf) != 0) {
        kfree(buf);
        return -1;
    }

    memcpy(buf + offset_in_block * inode_size, inode, sizeof(ext2_inode_t));

    if (ext2_write_block(inode_table_block + block_offset, buf) != 0) {
        kfree(buf);
        return -1;
    }

    kfree(buf);
    return 0;
}

int32_t ext2_init(uint32_t drive, uint32_t partition_start) {
    ext2_drive = drive;
    ext2_partition_start = partition_start;
    mutex_init(&ext2_lock);

    uint8_t *buf = (uint8_t *)kmalloc(1024);
    if (!buf) return -1;

    uint32_t sb_lba = partition_start + 2;
    if (ide_read_sectors(drive, 2, sb_lba, buf) != 0) {
        kfree(buf);
        return -1;
    }

    memcpy(&sb, buf, sizeof(ext2_superblock_t));
    kfree(buf);

    if (sb.magic != EXT2_SUPER_MAGIC) return -1;

    block_size = 1024 << sb.log_block_size;
    inodes_per_group = sb.inodes_per_group;
    group_count = (sb.blocks_count + sb.blocks_per_group - 1) / sb.blocks_per_group;

    uint32_t bgdt_block = sb.first_data_block + 1;
    uint32_t bgdt_size = group_count * sizeof(ext2_bgd_t);
    uint32_t bgdt_blocks = (bgdt_size + block_size - 1) / block_size;

    bgdt = (ext2_bgd_t *)kmalloc(bgdt_blocks * block_size);
    if (!bgdt) return -1;

    for (uint32_t i = 0; i < bgdt_blocks; i++) {
        if (ext2_read_block(bgdt_block + i, (uint8_t *)bgdt + i * block_size) != 0) {
            kfree(bgdt);
            bgdt = NULL;
            return -1;
        }
    }

    return 0;
}

static uint32_t ext2_alloc_block(void) {
    mutex_lock(&ext2_lock);

    for (uint32_t g = 0; g < group_count; g++) {
        uint8_t *bitmap = (uint8_t *)kmalloc(block_size);
        if (!bitmap) {
            mutex_unlock(&ext2_lock);
            return 0;
        }

        if (ext2_read_block(bgdt[g].block_bitmap, bitmap) != 0) {
            kfree(bitmap);
            continue;
        }

        for (uint32_t i = 0; i < block_size * 8; i++) {
            if (!(bitmap[i / 8] & (1 << (i % 8)))) {
                bitmap[i / 8] |= (1 << (i % 8));
                ext2_write_block(bgdt[g].block_bitmap, bitmap);
                bgdt[g].free_blocks_count--;
                kfree(bitmap);
                mutex_unlock(&ext2_lock);
                return sb.first_data_block + g * sb.blocks_per_group + i;
            }
        }

        kfree(bitmap);
    }

    mutex_unlock(&ext2_lock);
    return 0;
}

static void ext2_free_block(uint32_t block) {
    mutex_lock(&ext2_lock);

    uint32_t g = (block - sb.first_data_block) / sb.blocks_per_group;
    uint32_t i = (block - sb.first_data_block) % sb.blocks_per_group;

    uint8_t *bitmap = (uint8_t *)kmalloc(block_size);
    if (!bitmap) {
        mutex_unlock(&ext2_lock);
        return;
    }

    if (ext2_read_block(bgdt[g].block_bitmap, bitmap) == 0) {
        bitmap[i / 8] &= ~(1 << (i % 8));
        ext2_write_block(bgdt[g].block_bitmap, bitmap);
        bgdt[g].free_blocks_count++;
    }

    kfree(bitmap);
    mutex_unlock(&ext2_lock);
}

static uint32_t ext2_alloc_inode(void) {
    mutex_lock(&ext2_lock);

    for (uint32_t g = 0; g < group_count; g++) {
        uint8_t *bitmap = (uint8_t *)kmalloc(block_size);
        if (!bitmap) {
            mutex_unlock(&ext2_lock);
            return 0;
        }

        if (ext2_read_block(bgdt[g].inode_bitmap, bitmap) != 0) {
            kfree(bitmap);
            continue;
        }

        for (uint32_t i = 0; i < inodes_per_group; i++) {
            if (!(bitmap[i / 8] & (1 << (i % 8)))) {
                bitmap[i / 8] |= (1 << (i % 8));
                ext2_write_block(bgdt[g].inode_bitmap, bitmap);
                bgdt[g].free_inodes_count--;
                kfree(bitmap);
                mutex_unlock(&ext2_lock);
                return g * inodes_per_group + i + 1;
            }
        }

        kfree(bitmap);
    }

    mutex_unlock(&ext2_lock);
    return 0;
}

static void ext2_free_inode(uint32_t ino) {
    mutex_lock(&ext2_lock);

    uint32_t g = (ino - 1) / inodes_per_group;
    uint32_t i = (ino - 1) % inodes_per_group;

    uint8_t *bitmap = (uint8_t *)kmalloc(block_size);
    if (!bitmap) {
        mutex_unlock(&ext2_lock);
        return;
    }

    if (ext2_read_block(bgdt[g].inode_bitmap, bitmap) == 0) {
        bitmap[i / 8] &= ~(1 << (i % 8));
        ext2_write_block(bgdt[g].inode_bitmap, bitmap);
        bgdt[g].free_inodes_count++;
    }

    kfree(bitmap);
    mutex_unlock(&ext2_lock);
}

static int32_t ext2_read_file(ext2_inode_t *e2inode, void *buf, uint32_t offset, uint32_t count) {
    if (!e2inode || !buf) return -1;
    if (offset >= e2inode->i_size) return 0;

    uint32_t remaining = count;
    if (offset + remaining > e2inode->i_size) {
        remaining = e2inode->i_size - offset;
    }

    uint32_t bytes_read = 0;
    uint8_t *dst = (uint8_t *)buf;
    uint8_t *block_buf = (uint8_t *)kmalloc(block_size);
    if (!block_buf) return -1;

    while (remaining > 0) {
        uint32_t file_block = (offset + bytes_read) / block_size;
        uint32_t block_offset = (offset + bytes_read) % block_size;
        uint32_t to_copy = block_size - block_offset;
        if (to_copy > remaining) to_copy = remaining;

        uint32_t physical_block = 0;

        if (file_block < 12) {
            physical_block = e2inode->i_block[file_block];
        } else if (file_block < 12 + (block_size / 4)) {
            uint32_t *indirect = (uint32_t *)kmalloc(block_size);
            if (!indirect) break;
            ext2_read_block(e2inode->i_block[12], indirect);
            physical_block = indirect[file_block - 12];
            kfree(indirect);
        } else if (file_block < 12 + (block_size / 4) + (block_size / 4) * (block_size / 4)) {
            uint32_t *indirect = (uint32_t *)kmalloc(block_size);
            if (!indirect) break;
            ext2_read_block(e2inode->i_block[13], indirect);
            uint32_t idx1 = (file_block - 12 - (block_size / 4)) / (block_size / 4);
            uint32_t idx2 = (file_block - 12 - (block_size / 4)) % (block_size / 4);
            uint32_t *dindirect = (uint32_t *)kmalloc(block_size);
            if (!dindirect) { kfree(indirect); break; }
            ext2_read_block(indirect[idx1], dindirect);
            physical_block = dindirect[idx2];
            kfree(dindirect);
            kfree(indirect);
        }

        if (physical_block == 0) {
            memset(dst + bytes_read, 0, to_copy);
        } else {
            ext2_read_block(physical_block, block_buf);
            memcpy(dst + bytes_read, block_buf + block_offset, to_copy);
        }

        bytes_read += to_copy;
        remaining -= to_copy;
    }

    kfree(block_buf);
    return (int32_t)bytes_read;
}

static int32_t ext2_write_file(ext2_inode_t *e2inode, const void *buf, uint32_t offset, uint32_t count) {
    if (!e2inode || !buf) return -1;

    uint32_t remaining = count;
    uint32_t bytes_written = 0;
    const uint8_t *src = (const uint8_t *)buf;
    uint8_t *block_buf = (uint8_t *)kmalloc(block_size);
    if (!block_buf) return -1;

    uint32_t ptrs_per_block = block_size / 4;

    while (remaining > 0) {
        uint32_t file_block = (offset + bytes_written) / block_size;
        uint32_t block_offset = (offset + bytes_written) % block_size;
        uint32_t to_copy = block_size - block_offset;
        if (to_copy > remaining) to_copy = remaining;

        uint32_t physical_block = 0;

        if (file_block < 12) {
            /* Direct block */
            if (e2inode->i_block[file_block] == 0) {
                e2inode->i_block[file_block] = ext2_alloc_block();
                if (e2inode->i_block[file_block] == 0) break;
            }
            physical_block = e2inode->i_block[file_block];
        } else if (file_block < 12 + ptrs_per_block) {
            /* Single indirect block */
            if (e2inode->i_block[12] == 0) {
                e2inode->i_block[12] = ext2_alloc_block();
                if (e2inode->i_block[12] == 0) break;
                uint8_t *zero = (uint8_t *)kmalloc(block_size);
                memset(zero, 0, block_size);
                ext2_write_block(e2inode->i_block[12], zero);
                kfree(zero);
            }
            uint32_t *indirect = (uint32_t *)kmalloc(block_size);
            if (!indirect) break;
            ext2_read_block(e2inode->i_block[12], indirect);
            if (indirect[file_block - 12] == 0) {
                indirect[file_block - 12] = ext2_alloc_block();
                if (indirect[file_block - 12] == 0) { kfree(indirect); break; }
                ext2_write_block(e2inode->i_block[12], indirect);
            }
            physical_block = indirect[file_block - 12];
            kfree(indirect);
        } else if (file_block < 12 + ptrs_per_block + ptrs_per_block * ptrs_per_block) {
            /* Double indirect block */
            if (e2inode->i_block[13] == 0) {
                e2inode->i_block[13] = ext2_alloc_block();
                if (e2inode->i_block[13] == 0) break;
                uint8_t *zero = (uint8_t *)kmalloc(block_size);
                memset(zero, 0, block_size);
                ext2_write_block(e2inode->i_block[13], zero);
                kfree(zero);
            }
            uint32_t *dindirect = (uint32_t *)kmalloc(block_size);
            if (!dindirect) break;
            ext2_read_block(e2inode->i_block[13], dindirect);

            uint32_t idx1 = (file_block - 12 - ptrs_per_block) / ptrs_per_block;
            uint32_t idx2 = (file_block - 12 - ptrs_per_block) % ptrs_per_block;

            if (dindirect[idx1] == 0) {
                dindirect[idx1] = ext2_alloc_block();
                if (dindirect[idx1] == 0) { kfree(dindirect); break; }
                uint8_t *zero = (uint8_t *)kmalloc(block_size);
                memset(zero, 0, block_size);
                ext2_write_block(dindirect[idx1], zero);
                kfree(zero);
                ext2_write_block(e2inode->i_block[13], dindirect);
            }

            uint32_t *indirect = (uint32_t *)kmalloc(block_size);
            if (!indirect) { kfree(dindirect); break; }
            ext2_read_block(dindirect[idx1], indirect);
            if (indirect[idx2] == 0) {
                indirect[idx2] = ext2_alloc_block();
                if (indirect[idx2] == 0) { kfree(indirect); kfree(dindirect); break; }
                ext2_write_block(dindirect[idx1], indirect);
            }
            physical_block = indirect[idx2];
            kfree(indirect);
            kfree(dindirect);
        }

        if (physical_block == 0) break;

        if (block_offset != 0 || to_copy < block_size) {
            ext2_read_block(physical_block, block_buf);
        }
        memcpy(block_buf + block_offset, src + bytes_written, to_copy);
        ext2_write_block(physical_block, block_buf);

        bytes_written += to_copy;
        remaining -= to_copy;
    }

    if (offset + bytes_written > e2inode->i_size) {
        e2inode->i_size = offset + bytes_written;
    }

    kfree(block_buf);
    return (int32_t)bytes_written;
}

static int32_t ext2_lookup(uint32_t dir_ino, const char *name, uint32_t *out_ino) {
    ext2_inode_t dir_inode;
    if (ext2_read_inode(dir_ino, &dir_inode) != 0) return -1;
    if ((dir_inode.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR) return -1;

    uint8_t *buf = (uint8_t *)kmalloc(dir_inode.i_size);
    if (!buf) return -1;

    if (ext2_read_file(&dir_inode, buf, 0, dir_inode.i_size) < 0) {
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

static int32_t ext2_create(uint32_t dir_ino, const char *name, uint32_t mode) {
    uint32_t new_ino = ext2_alloc_inode();
    if (new_ino == 0) return -1;

    ext2_inode_t new_inode;
    memset(&new_inode, 0, sizeof(ext2_inode_t));
    new_inode.i_mode = EXT2_S_IFREG | (mode & 0x0FFF);
    new_inode.i_links_count = 1;
    ext2_write_inode(new_ino, &new_inode);

    ext2_inode_t dir_inode;
    ext2_read_inode(dir_ino, &dir_inode);

    uint8_t *dir_buf = (uint8_t *)kmalloc(dir_inode.i_size);
    ext2_read_file(&dir_inode, dir_buf, 0, dir_inode.i_size);

    uint32_t pos = 0;
    uint32_t needed = sizeof(ext2_dir_entry_t) - 255 + strlen(name);
    needed = (needed + 3) & ~3;

    while (pos < dir_inode.i_size) {
        ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(dir_buf + pos);
        if (entry->rec_len == 0) break;

        uint32_t actual_len = sizeof(ext2_dir_entry_t) - 255 + entry->name_len;
        actual_len = (actual_len + 3) & ~3;

        if (entry->rec_len - actual_len >= needed) {
            uint16_t orig_rec_len = entry->rec_len;
            entry->rec_len = (uint16_t)actual_len;

            ext2_dir_entry_t *new_entry = (ext2_dir_entry_t *)(dir_buf + pos + actual_len);
            new_entry->inode = new_ino;
            new_entry->name_len = (uint8_t)strlen(name);
            new_entry->file_type = 1;
            new_entry->rec_len = orig_rec_len - (uint16_t)actual_len;
            memcpy(new_entry->name, name, strlen(name));

            ext2_write_file(&dir_inode, dir_buf, 0, dir_inode.i_size);
            kfree(dir_buf);
            return 0;
        }

        pos += entry->rec_len;
    }

    uint8_t *new_dir_buf = (uint8_t *)kmalloc(dir_inode.i_size + block_size);
    memcpy(new_dir_buf, dir_buf, dir_inode.i_size);
    memset(new_dir_buf + dir_inode.i_size, 0, block_size);

    ext2_dir_entry_t *new_entry = (ext2_dir_entry_t *)(new_dir_buf + dir_inode.i_size);
    new_entry->inode = new_ino;
    new_entry->name_len = (uint8_t)strlen(name);
    new_entry->file_type = 1;
    new_entry->rec_len = (uint16_t)block_size;
    memcpy(new_entry->name, name, strlen(name));

    ext2_write_file(&dir_inode, new_dir_buf, 0, dir_inode.i_size + block_size);
    kfree(dir_buf);
    kfree(new_dir_buf);
    return 0;
}

static int32_t ext2_mkdir_op(uint32_t dir_ino, const char *name, uint32_t mode) {
    uint32_t new_ino = ext2_alloc_inode();
    if (new_ino == 0) return -1;

    uint32_t new_block = ext2_alloc_block();
    if (new_block == 0) {
        ext2_free_inode(new_ino);
        return -1;
    }

    ext2_inode_t new_inode;
    memset(&new_inode, 0, sizeof(ext2_inode_t));
    new_inode.i_mode = EXT2_S_IFDIR | (mode & 0x0FFF);
    new_inode.i_links_count = 2;
    new_inode.i_block[0] = new_block;
    new_inode.i_size = block_size;
    ext2_write_inode(new_ino, &new_inode);

    uint8_t *block_buf = (uint8_t *)kmalloc(block_size);
    memset(block_buf, 0, block_size);

    ext2_dir_entry_t *dot = (ext2_dir_entry_t *)block_buf;
    dot->inode = new_ino;
    dot->name_len = 1;
    dot->file_type = 2;
    dot->rec_len = 12;
    dot->name[0] = '.';

    ext2_dir_entry_t *dotdot = (ext2_dir_entry_t *)(block_buf + 12);
    dotdot->inode = dir_ino;
    dotdot->name_len = 2;
    dotdot->file_type = 2;
    dotdot->rec_len = (uint16_t)(block_size - 12);
    dotdot->name[0] = '.';
    dotdot->name[1] = '.';

    ext2_write_block(new_block, block_buf);
    kfree(block_buf);

    ext2_inode_t dir_inode;
    ext2_read_inode(dir_ino, &dir_inode);
    dir_inode.i_links_count++;
    ext2_write_inode(dir_ino, &dir_inode);

    ext2_create(dir_ino, name, mode);
    return 0;
}

static int32_t ext2_unlink_op(uint32_t dir_ino, const char *name) {
    ext2_inode_t dir_inode;
    if (ext2_read_inode(dir_ino, &dir_inode) != 0) return -1;

    uint8_t *buf = (uint8_t *)kmalloc(dir_inode.i_size);
    if (!buf) return -1;
    ext2_read_file(&dir_inode, buf, 0, dir_inode.i_size);

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

            ext2_write_file(&dir_inode, buf, 0, dir_inode.i_size);
            kfree(buf);
            return 0;
        }

        prev_pos = pos;
        pos += entry->rec_len;
    }

    kfree(buf);
    return -1;
}

static dentry_t *ext2_vfs_lookup(dentry_t *dir, const char *name);
static int32_t ext2_vfs_create(dentry_t *dir, const char *name, uint32_t mode);
static int32_t ext2_vfs_mkdir(dentry_t *dir, const char *name, uint32_t mode);
static int32_t ext2_vfs_unlink(dentry_t *dir, const char *name);
static int32_t ext2_vfs_rmdir(dentry_t *dir, const char *name);
static int32_t ext2_vfs_rename(dentry_t *old_dir, const char *old_name, dentry_t *new_dir, const char *new_name);
static int32_t ext2_vfs_symlink(dentry_t *dir, const char *name, const char *target);
static int32_t ext2_vfs_readlink(dentry_t *dentry, char *buf, uint32_t size);
static int32_t ext2_vfs_link(dentry_t *old_dir, const char *old_name,
                             dentry_t *new_dir, const char *new_name);

static inode_ops_t ext2_inode_ops = {
    .lookup = ext2_vfs_lookup,
    .create = ext2_vfs_create,
    .mkdir = ext2_vfs_mkdir,
    .unlink = ext2_vfs_unlink,
    .rmdir = ext2_vfs_rmdir,
    .rename = ext2_vfs_rename,
    .readlink = ext2_vfs_readlink,
    .symlink = ext2_vfs_symlink,
    .link = ext2_vfs_link
};

static dentry_t *ext2_vfs_lookup(dentry_t *dir, const char *name) {
    if (!dir || !dir->inode) return NULL;

    uint32_t out_ino;
    if (ext2_lookup(dir->inode->ino, name, &out_ino) != 0) return NULL;

    ext2_inode_t e2inode;
    if (ext2_read_inode(out_ino, &e2inode) != 0) return NULL;

    inode_t *vfs_inode = (inode_t *)kmalloc(sizeof(inode_t));
    if (!vfs_inode) return NULL;
    memset(vfs_inode, 0, sizeof(inode_t));

    vfs_inode->ino = out_ino;
    vfs_inode->mode = 0;
    uint32_t fmt = e2inode.i_mode & EXT2_S_IFMT;
    if (fmt == EXT2_S_IFDIR)  vfs_inode->mode |= FILE_MODE_DIR;
    if (fmt == EXT2_S_IFREG)  vfs_inode->mode |= FILE_MODE_REG;
    if (fmt == EXT2_S_IFLNK)  vfs_inode->mode |= FILE_MODE_LNK;
    if (e2inode.i_mode & 0x0100) vfs_inode->mode |= FILE_MODE_READ;  /* owner r */
    if (e2inode.i_mode & 0x0080) vfs_inode->mode |= FILE_MODE_WRITE; /* owner w */
    if (e2inode.i_mode & 0x0040) vfs_inode->mode |= FILE_MODE_EXEC;  /* owner x */
    vfs_inode->uid = e2inode.i_uid;
    vfs_inode->gid = e2inode.i_gid;
    vfs_inode->size = e2inode.i_size;
    vfs_inode->nlinks = e2inode.i_links_count;
    vfs_inode->atime = e2inode.i_atime;
    vfs_inode->mtime = e2inode.i_mtime;
    vfs_inode->ctime = e2inode.i_ctime;
    vfs_inode->sb = dir->inode->sb;
    vfs_inode->ops = &ext2_inode_ops;
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

static int32_t ext2_vfs_create(dentry_t *dir, const char *name, uint32_t mode) {
    if (!dir || !dir->inode) return -1;
    return ext2_create(dir->inode->ino, name, mode);
}

static int32_t ext2_vfs_mkdir(dentry_t *dir, const char *name, uint32_t mode) {
    if (!dir || !dir->inode) return -1;
    return ext2_mkdir_op(dir->inode->ino, name, mode);
}

static int32_t ext2_vfs_unlink(dentry_t *dir, const char *name) {
    if (!dir || !dir->inode) return -1;
    return ext2_unlink_op(dir->inode->ino, name);
}

static int32_t ext2_vfs_rmdir(dentry_t *dir, const char *name) {
    if (!dir || !dir->inode) return -1;

    /* First, look up the target inode */
    uint32_t target_ino;
    if (ext2_lookup(dir->inode->ino, name, &target_ino) != 0) return -1;

    /* Check if the directory is empty (only . and .. entries) */
    ext2_inode_t target_inode;
    if (ext2_read_inode(target_ino, &target_inode) != 0) return -1;

    uint8_t *dir_buf = (uint8_t *)kmalloc(target_inode.i_size);
    if (!dir_buf) return -1;
    ext2_read_file(&target_inode, dir_buf, 0, target_inode.i_size);

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

    /* Decrement parent link count (for ..) */
    ext2_inode_t parent_inode;
    if (ext2_read_inode(dir->inode->ino, &parent_inode) == 0) {
        if (parent_inode.i_links_count > 0) {
            parent_inode.i_links_count--;
            ext2_write_inode(dir->inode->ino, &parent_inode);
        }
    }

    /* Decrement target link count */
    if (target_inode.i_links_count > 0) {
        target_inode.i_links_count--;
        ext2_write_inode(target_ino, &target_inode);
    }

    return ext2_unlink_op(dir->inode->ino, name);
}

/* ---- Rename: unlink old name from target dir, create new entry ---- */
static int32_t ext2_vfs_rename(dentry_t *old_dir, const char *old_name,
                               dentry_t *new_dir, const char *new_name) {
    if (!old_dir || !old_dir->inode || !new_dir || !new_dir->inode) return -1;

    /* Look up the inode of the old name */
    uint32_t target_ino;
    if (ext2_lookup(old_dir->inode->ino, old_name, &target_ino) != 0) return -1;

    /* If new name already exists, remove it first */
    uint32_t existing_ino;
    if (ext2_lookup(new_dir->inode->ino, new_name, &existing_ino) == 0) {
        ext2_unlink_op(new_dir->inode->ino, new_name);
    }

    /* Read the target inode to determine file type */
    ext2_inode_t target_inode;
    if (ext2_read_inode(target_ino, &target_inode) != 0) return -1;

    /* Create new directory entry in the target directory */
    ext2_inode_t new_dir_inode;
    if (ext2_read_inode(new_dir->inode->ino, &new_dir_inode) != 0) return -1;

    /* Add entry to new directory */
    uint8_t *dir_buf = (uint8_t *)kmalloc(new_dir_inode.i_size);
    if (!dir_buf) return -1;
    ext2_read_file(&new_dir_inode, dir_buf, 0, new_dir_inode.i_size);

    uint32_t name_len = strlen(new_name);
    uint32_t needed = sizeof(ext2_dir_entry_t) - 255 + name_len;
    needed = (needed + 3) & ~3;

    /* Determine file_type for the new entry */
    uint8_t file_type = 1; /* regular file */
    uint32_t fmt = target_inode.i_mode & EXT2_S_IFMT;
    if (fmt == EXT2_S_IFDIR) file_type = 2;
    else if (fmt == EXT2_S_IFLNK) file_type = 7;

    int added = 0;
    uint32_t pos = 0;
    while (pos < new_dir_inode.i_size) {
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
            memcpy(new_entry->name, new_name, name_len);

            ext2_write_file(&new_dir_inode, dir_buf, 0, new_dir_inode.i_size);
            added = 1;
            break;
        }

        pos += entry->rec_len;
    }

    if (!added) {
        /* Append a new block */
        uint8_t *new_dir_buf = (uint8_t *)kmalloc(new_dir_inode.i_size + block_size);
        memcpy(new_dir_buf, dir_buf, new_dir_inode.i_size);
        memset(new_dir_buf + new_dir_inode.i_size, 0, block_size);

        ext2_dir_entry_t *new_entry = (ext2_dir_entry_t *)(new_dir_buf + new_dir_inode.i_size);
        new_entry->inode = target_ino;
        new_entry->name_len = (uint8_t)name_len;
        new_entry->file_type = file_type;
        new_entry->rec_len = (uint16_t)block_size;
        memcpy(new_entry->name, new_name, name_len);

        ext2_write_file(&new_dir_inode, new_dir_buf, 0, new_dir_inode.i_size + block_size);
        kfree(new_dir_buf);
    }

    kfree(dir_buf);

    /* Remove old directory entry */
    ext2_unlink_op(old_dir->inode->ino, old_name);

    /* If moving a directory, update .. entry */
    if (fmt == EXT2_S_IFDIR && old_dir->inode->ino != new_dir->inode->ino) {
        ext2_inode_t moved_dir_inode;
        if (ext2_read_inode(target_ino, &moved_dir_inode) == 0 && moved_dir_inode.i_block[0] != 0) {
            uint8_t *block_buf = (uint8_t *)kmalloc(block_size);
            if (block_buf) {
                ext2_read_block(moved_dir_inode.i_block[0], block_buf);
                ext2_dir_entry_t *dotdot = (ext2_dir_entry_t *)(block_buf + 12);
                if (dotdot->name_len == 2 && dotdot->name[0] == '.' && dotdot->name[1] == '.') {
                    dotdot->inode = new_dir->inode->ino;
                    ext2_write_block(moved_dir_inode.i_block[0], block_buf);
                }
                kfree(block_buf);
            }
        }
    }

    return 0;
}

/* ---- Symlink: create a symbolic link ---- */
static int32_t ext2_vfs_symlink(dentry_t *dir, const char *name, const char *target) {
    if (!dir || !dir->inode || !name || !target) return -1;

    uint32_t new_ino = ext2_alloc_inode();
    if (new_ino == 0) return -1;

    ext2_inode_t new_inode;
    memset(&new_inode, 0, sizeof(ext2_inode_t));
    new_inode.i_mode = EXT2_S_IFLNK | 0x01FF; /* 0777 permissions */
    new_inode.i_links_count = 1;
    new_inode.i_mtime = rtc_get_timestamp();
    new_inode.i_atime = new_inode.i_mtime;
    new_inode.i_ctime = new_inode.i_mtime;

    uint32_t target_len = strlen(target);

    /* Fast symlink: if target fits in i_block (60 bytes), store there */
    if (target_len < 60) {
        memcpy(new_inode.i_block, target, target_len);
        new_inode.i_size = target_len;
        ext2_write_inode(new_ino, &new_inode);
    } else {
        /* Slow symlink: allocate a data block */
        uint32_t new_block = ext2_alloc_block();
        if (new_block == 0) {
            ext2_free_inode(new_ino);
            return -1;
        }
        new_inode.i_block[0] = new_block;
        new_inode.i_size = target_len;

        uint8_t *block_buf = (uint8_t *)kmalloc(block_size);
        if (!block_buf) {
            ext2_free_block(new_block);
            ext2_free_inode(new_ino);
            return -1;
        }
        memset(block_buf, 0, block_size);
        memcpy(block_buf, target, target_len);
        ext2_write_block(new_block, block_buf);
        kfree(block_buf);

        ext2_write_inode(new_ino, &new_inode);
    }

    /* Add directory entry */
    ext2_inode_t dir_inode;
    ext2_read_inode(dir->inode->ino, &dir_inode);

    uint8_t *dir_buf = (uint8_t *)kmalloc(dir_inode.i_size);
    if (!dir_buf) {
        ext2_free_inode(new_ino);
        return -1;
    }
    ext2_read_file(&dir_inode, dir_buf, 0, dir_inode.i_size);

    uint32_t name_len = strlen(name);
    uint32_t needed = sizeof(ext2_dir_entry_t) - 255 + name_len;
    needed = (needed + 3) & ~3;

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
            new_entry->inode = new_ino;
            new_entry->name_len = (uint8_t)name_len;
            new_entry->file_type = 7; /* DT_LNK */
            new_entry->rec_len = orig_rec_len - (uint16_t)actual_len;
            memcpy(new_entry->name, name, name_len);

            ext2_write_file(&dir_inode, dir_buf, 0, dir_inode.i_size);
            added = 1;
            break;
        }

        pos += entry->rec_len;
    }

    if (!added) {
        uint8_t *new_dir_buf = (uint8_t *)kmalloc(dir_inode.i_size + block_size);
        memcpy(new_dir_buf, dir_buf, dir_inode.i_size);
        memset(new_dir_buf + dir_inode.i_size, 0, block_size);

        ext2_dir_entry_t *new_entry = (ext2_dir_entry_t *)(new_dir_buf + dir_inode.i_size);
        new_entry->inode = new_ino;
        new_entry->name_len = (uint8_t)name_len;
        new_entry->file_type = 7;
        new_entry->rec_len = (uint16_t)block_size;
        memcpy(new_entry->name, name, name_len);

        ext2_write_file(&dir_inode, new_dir_buf, 0, dir_inode.i_size + block_size);
        kfree(new_dir_buf);
    }

    kfree(dir_buf);
    return 0;
}

/* ---- Readlink: read the target of a symbolic link ---- */
static int32_t ext2_vfs_readlink(dentry_t *dentry, char *buf, uint32_t size) {
    if (!dentry || !dentry->inode || !buf || size == 0) return -1;

    ext2_inode_t e2inode;
    if (ext2_read_inode(dentry->inode->ino, &e2inode) != 0) return -1;

    if ((e2inode.i_mode & EXT2_S_IFMT) != EXT2_S_IFLNK) return -1;

    uint32_t link_len = e2inode.i_size;
    if (link_len == 0) return -1;
    if (link_len > size - 1) link_len = size - 1;

    /* Fast symlink: stored in i_block */
    if (link_len < 60) {
        memcpy(buf, e2inode.i_block, link_len);
    } else {
        /* Slow symlink: stored in data block */
        if (e2inode.i_block[0] == 0) return -1;
        uint8_t *block_buf = (uint8_t *)kmalloc(block_size);
        if (!block_buf) return -1;
        ext2_read_block(e2inode.i_block[0], block_buf);
        memcpy(buf, block_buf, link_len);
        kfree(block_buf);
    }

    buf[link_len] = '\0';
    return (int32_t)link_len;
}

/* ---- Truncate: shrink a file to the given size ---- */
int32_t ext2_truncate(uint32_t ino, uint32_t new_size) {
    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) != 0) return -1;

    if (new_size >= inode.i_size) {
        /* Extending: just update size (zeros on read) */
        inode.i_size = new_size;
        ext2_write_inode(ino, &inode);
        return 0;
    }

    /* Shrinking: free blocks beyond new_size */
    uint32_t old_blocks = (inode.i_size + block_size - 1) / block_size;
    uint32_t new_blocks = (new_size + block_size - 1) / block_size;
    uint32_t ptrs_per_block = block_size / 4;

    /* Free direct blocks (0-11) */
    for (uint32_t i = new_blocks; i < old_blocks && i < 12; i++) {
        if (inode.i_block[i] != 0) {
            ext2_free_block(inode.i_block[i]);
            inode.i_block[i] = 0;
        }
    }

    /* Free indirect blocks (12) */
    if (old_blocks > 12 && inode.i_block[12] != 0) {
        uint32_t *indirect = (uint32_t *)kmalloc(block_size);
        if (indirect) {
            ext2_read_block(inode.i_block[12], indirect);
            uint32_t start = (new_blocks > 12) ? new_blocks - 12 : 0;
            uint32_t end = (old_blocks > 12) ? old_blocks - 12 : 0;
            if (end > ptrs_per_block) end = ptrs_per_block;
            for (uint32_t i = start; i < end; i++) {
                if (indirect[i] != 0) ext2_free_block(indirect[i]);
            }
            /* If all indirect entries freed, free the indirect block itself */
            if (new_blocks <= 12) {
                ext2_free_block(inode.i_block[12]);
                inode.i_block[12] = 0;
            } else {
                ext2_write_block(inode.i_block[12], indirect);
            }
            kfree(indirect);
        }
    }

    /* Free double indirect blocks (13) */
    if (old_blocks > 12 + ptrs_per_block && inode.i_block[13] != 0) {
        uint32_t *dindirect = (uint32_t *)kmalloc(block_size);
        if (dindirect) {
            ext2_read_block(inode.i_block[13], dindirect);
            uint32_t dind_start = (new_blocks > 12 + ptrs_per_block) ?
                (new_blocks - 12 - ptrs_per_block) / ptrs_per_block : 0;
            uint32_t dind_end = (old_blocks - 12 - ptrs_per_block + ptrs_per_block - 1) / ptrs_per_block;
            if (dind_end > ptrs_per_block) dind_end = ptrs_per_block;

            for (uint32_t di = dind_start; di < dind_end; di++) {
                if (dindirect[di] != 0) {
                    uint32_t *indirect = (uint32_t *)kmalloc(block_size);
                    if (indirect) {
                        ext2_read_block(dindirect[di], indirect);
                        uint32_t ind_start = 0;
                        if (di == dind_start && new_blocks > 12 + ptrs_per_block) {
                            ind_start = (new_blocks - 12 - ptrs_per_block) % ptrs_per_block;
                        }
                        for (uint32_t j = ind_start; j < ptrs_per_block; j++) {
                            if (indirect[j] != 0) ext2_free_block(indirect[j]);
                        }
                        ext2_free_block(dindirect[di]);
                        kfree(indirect);
                    }
                }
            }

            if (new_blocks <= 12 + ptrs_per_block) {
                ext2_free_block(inode.i_block[13]);
                inode.i_block[13] = 0;
            } else {
                ext2_write_block(inode.i_block[13], dindirect);
            }
            kfree(dindirect);
        }
    }

    inode.i_size = new_size;
    inode.i_mtime = rtc_get_timestamp();
    ext2_write_inode(ino, &inode);
    return 0;
}

static int32_t ext2_vfs_link(dentry_t *old_dir, const char *old_name,
                             dentry_t *new_dir, const char *new_name) {
    if (!old_dir || !old_dir->inode || !new_dir || !new_dir->inode) return -1;

    /* Look up the existing inode */
    uint32_t target_ino;
    if (ext2_lookup(old_dir->inode->ino, old_name, &target_ino) != 0) return -1;

    ext2_inode_t target_inode;
    if (ext2_read_inode(target_ino, &target_inode) != 0) return -1;

    /* Don't allow hard links to directories */
    if ((target_inode.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR) return -1;

    /* Add new directory entry pointing to the same inode */
    ext2_inode_t new_dir_inode;
    if (ext2_read_inode(new_dir->inode->ino, &new_dir_inode) != 0) return -1;

    uint8_t *dir_buf = (uint8_t *)kmalloc(new_dir_inode.i_size + block_size);
    if (!dir_buf) return -1;
    ext2_read_file(&new_dir_inode, dir_buf, 0, new_dir_inode.i_size);

    uint32_t name_len = strlen(new_name);
    uint32_t needed = sizeof(ext2_dir_entry_t) - 255 + name_len;
    needed = (needed + 3) & ~3;

    uint8_t file_type = 1; /* regular file */
    int added = 0;
    uint32_t pos = 0;
    while (pos < new_dir_inode.i_size) {
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
            memcpy(new_entry->name, new_name, name_len);

            ext2_write_file(&new_dir_inode, dir_buf, 0, new_dir_inode.i_size);
            added = 1;
            break;
        }

        pos += entry->rec_len;
    }

    if (!added) {
        memset(dir_buf + new_dir_inode.i_size, 0, block_size);
        ext2_dir_entry_t *new_entry = (ext2_dir_entry_t *)(dir_buf + new_dir_inode.i_size);
        new_entry->inode = target_ino;
        new_entry->name_len = (uint8_t)name_len;
        new_entry->file_type = file_type;
        new_entry->rec_len = (uint16_t)block_size;
        memcpy(new_entry->name, new_name, name_len);

        ext2_write_file(&new_dir_inode, dir_buf, 0, new_dir_inode.i_size + block_size);
    }

    kfree(dir_buf);

    /* Increment link count */
    target_inode.i_links_count++;
    ext2_write_inode(target_ino, &target_inode);

    return 0;
}

int32_t ext2_chmod(uint32_t ino, uint32_t mode) {
    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) != 0) return -1;

    /* Preserve file type, replace permission bits */
    inode.i_mode = (inode.i_mode & EXT2_S_IFMT) | (mode & 0x0FFF);
    inode.i_ctime = rtc_get_timestamp();
    ext2_write_inode(ino, &inode);
    return 0;
}

int32_t ext2_chown(uint32_t ino, uint32_t uid, uint32_t gid) {
    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) != 0) return -1;

    inode.i_uid = (uint16_t)uid;
    inode.i_gid = (uint16_t)gid;
    inode.i_ctime = rtc_get_timestamp();
    ext2_write_inode(ino, &inode);
    return 0;
}

int32_t ext2_utimes(uint32_t ino, uint32_t atime, uint32_t mtime) {
    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) != 0) return -1;

    inode.i_atime = atime;
    inode.i_mtime = mtime;
    inode.i_ctime = rtc_get_timestamp();
    ext2_write_inode(ino, &inode);
    return 0;
}

static int32_t ext2_file_open(inode_t *inode, file_t *file) {
    (void)inode;
    (void)file;
    return 0;
}

static int32_t ext2_file_read(file_t *file, void *buf, uint32_t count) {
    if (!file || !file->inode) return -1;

    ext2_inode_t e2inode;
    if (ext2_read_inode(file->inode->ino, &e2inode) != 0) return -1;

    int32_t ret = ext2_read_file(&e2inode, buf, file->offset, count);
    if (ret > 0) file->offset += ret;
    return ret;
}

static int32_t ext2_file_write(file_t *file, const void *buf, uint32_t count) {
    if (!file || !file->inode) return -1;

    ext2_inode_t e2inode;
    if (ext2_read_inode(file->inode->ino, &e2inode) != 0) return -1;

    int32_t ret = ext2_write_file(&e2inode, buf, file->offset, count);
    if (ret > 0) {
        file->offset += ret;
        ext2_write_inode(file->inode->ino, &e2inode);
    }
    return ret;
}

static int32_t ext2_file_close(file_t *file) {
    (void)file;
    return 0;
}

static int32_t ext2_file_seek(file_t *file, int32_t offset, int32_t whence) {
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

int32_t ext2_fsync(uint32_t ino) {
    /* EXT2 writes are synchronous - data is already on disk after each write.
     * This function exists for VFS compatibility. */
    (void)ino;
    return 0;
}

static int32_t ext2_file_ioctl(file_t *file, uint32_t cmd, void *arg) {
    if (!file || !file->inode) return -EBADF;
    switch (cmd) {
        case FIONREAD:
            if (arg) {
                int32_t avail = (int32_t)file->inode->size - (int32_t)file->offset;
                if (avail < 0) avail = 0;
                *(int32_t *)arg = avail;
                return 0;
            }
            return -EINVAL;
        case FIONBIO:
            return 0;
        default:
            return -ENOSYS;
    }
}

file_ops_t ext2_file_ops = {
    .open = ext2_file_open,
    .read = ext2_file_read,
    .write = ext2_file_write,
    .close = ext2_file_close,
    .seek = ext2_file_seek,
    .ioctl = ext2_file_ioctl
};

int32_t ext2_mount(superblock_t *sb_vfs, void *data) {
    if (ext2_init(0, data ? (uint32_t)(uintptr_t)data : 0) != 0) return -1;

    sb_vfs->fs_type = FS_TYPE_EXT2;
    sb_vfs->block_size = block_size;
    sb_vfs->total_blocks = sb.blocks_count;
    sb_vfs->free_blocks = sb.free_blocks_count;
    sb_vfs->fs_data = &sb;

    ext2_inode_t root_e2inode;
    if (ext2_read_inode(2, &root_e2inode) != 0) return -1;

    inode_t *root_inode = (inode_t *)kmalloc(sizeof(inode_t));
    if (!root_inode) return -1;
    memset(root_inode, 0, sizeof(inode_t));
    root_inode->ino = 2;
    root_inode->mode = FILE_MODE_DIR | FILE_MODE_READ | FILE_MODE_WRITE;
    root_inode->size = root_e2inode.i_size;
    root_inode->nlinks = root_e2inode.i_links_count;
    root_inode->sb = sb_vfs;
    root_inode->ops = &ext2_inode_ops;
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

    sb_vfs->root = root_inode;
    return 0;
}
