/* XFS 只读文件系统驱动
 * 实现高性能日志文件系统的只读挂载和访问
 * 关键特性: B+ 树管理空间和目录、分配组(AG)并行、延迟分配
 */

#include "xfs.h"
#include "vfs.h"
#include "ide.h"
#include "kheap.h"
#include "string.h"
#include "stddef.h"
#include "sync.h"
#include "klog.h"

/* ------------------------------------------------------------------ */
/* 全局变量                                                           */
/* ------------------------------------------------------------------ */

static spinlock_t xfs_lock;
static xfs_superblock_t xfs_sb;
static uint32_t xfs_drive;
static uint32_t xfs_partition_start;
static int xfs_mounted = 0;

/* ------------------------------------------------------------------ */
/* 前向声明                                                           */
/* ------------------------------------------------------------------ */

static dentry_t *xfs_lookup_op(dentry_t *dir, const char *name);
static int32_t xfs_create_op(dentry_t *dir, const char *name, uint32_t mode);
static int32_t xfs_mkdir_op(dentry_t *dir, const char *name, uint32_t mode);
static int32_t xfs_unlink_op(dentry_t *dir, const char *name);
static int32_t xfs_rmdir_op(dentry_t *dir, const char *name);
static int32_t xfs_rename_op(dentry_t *old_dir, const char *old_name,
                             dentry_t *new_dir, const char *new_name);

static int32_t xfs_file_open(inode_t *inode, file_t *file);
static int32_t xfs_file_read(file_t *file, void *buf, uint32_t count);
static int32_t xfs_file_write(file_t *file, const void *buf, uint32_t count);
static int32_t xfs_file_close(file_t *file);
static int32_t xfs_file_seek(file_t *file, int32_t offset, int32_t whence);

/* ------------------------------------------------------------------ */
/* VFS 操作表                                                         */
/* ------------------------------------------------------------------ */

static inode_ops_t xfs_inode_ops = {
    .lookup   = xfs_lookup_op,
    .create   = xfs_create_op,
    .mkdir    = xfs_mkdir_op,
    .unlink   = xfs_unlink_op,
    .rmdir    = xfs_rmdir_op,
    .rename   = xfs_rename_op,
    .readlink = NULL,
    .symlink  = NULL,
};

file_ops_t xfs_file_ops = {
    .open  = xfs_file_open,
    .read  = xfs_file_read,
    .write = xfs_file_write,
    .close = xfs_file_close,
    .seek  = xfs_file_seek,
    .ioctl = NULL,
};

/* ------------------------------------------------------------------ */
/* 底层 I/O                                                           */
/* ------------------------------------------------------------------ */

static int xfs_read_from_device(uint64_t byte_offset, void *buf, uint32_t size) {
    if (!buf || size == 0) return -1;

    uint32_t start_lba = xfs_partition_start + (uint32_t)(byte_offset / 512);
    uint32_t start_off = (uint32_t)(byte_offset % 512);
    uint32_t sectors = (start_off + size + 511) / 512;

    uint8_t *tmp = (uint8_t *)kmalloc(sectors * 512);
    if (!tmp) return -1;

    if (ide_read_sectors(xfs_drive, sectors, start_lba, tmp) != 0) {
        kfree(tmp);
        return -1;
    }

    memcpy(buf, tmp + start_off, size);
    kfree(tmp);
    return 0;
}

static int xfs_read_block(uint64_t block_num, void *buf) {
    if (!buf) return -1;
    return xfs_read_from_device(block_num * xfs_sb.blocksize, buf, xfs_sb.blocksize);
}

/* ------------------------------------------------------------------ */
/* 超级块解析                                                         */
/* ------------------------------------------------------------------ */

int xfs_read_superblock(xfs_superblock_t *sb) {
    if (!sb) return -1;

    uint8_t *buf = (uint8_t *)kmalloc(xfs_sb.blocksize ? xfs_sb.blocksize : 4096);
    if (!buf) return -1;

    if (xfs_read_from_device(0, buf, sizeof(xfs_superblock_t)) != 0) {
        kfree(buf);
        return -1;
    }

    memcpy(sb, buf, sizeof(xfs_superblock_t));
    kfree(buf);

    if (sb->magic != (uint32_t)XFS_MAGIC) {
        return -1;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* AG (Allocation Group) 支持                                         */
/* ------------------------------------------------------------------ */

int xfs_read_agf(uint32_t ag_index, xfs_agf_t *agf) {
    if (!agf || ag_index >= xfs_sb.agcount) return -1;

    uint64_t ag_start = (uint64_t)ag_index * xfs_sb.agsize * xfs_sb.blocksize;
    return xfs_read_from_device(ag_start + 1 * xfs_sb.blocksize, agf, sizeof(xfs_agf_t));
}

int xfs_read_agi(uint32_t ag_index, xfs_agi_t *agi) {
    if (!agi || ag_index >= xfs_sb.agcount) return -1;

    uint64_t ag_start = (uint64_t)ag_index * xfs_sb.agsize * xfs_sb.blocksize;
    return xfs_read_from_device(ag_start + 2 * xfs_sb.blocksize, agi, sizeof(xfs_agi_t));
}

/* ------------------------------------------------------------------ */
/* Inode 读取                                                         */
/* ------------------------------------------------------------------ */

typedef struct {
    uint16_t magic;
    uint16_t mode;
    int16_t  version;
    int32_t  format;
    uint8_t  link;
    uint32_t uid;
    uint32_t gid;
    uint32_t nlink;
    uint16_t projid;
    uint8_t  pad[6];
    uint16_t flushiter;
    uint64_t atime;
    uint64_t mtime;
    uint64_t ctime;
    uint64_t size;
    uint64_t nblocks;
    uint32_t extsize;
    uint32_t nextents;
    uint8_t  bmx[0];
} __attribute__((packed)) xfs_dinode_t;

typedef struct {
    uint64_t br_startoff;
    uint64_t br_startblock;
    uint32_t br_blockcount;
} xfs_bmbt_rec_t;

int xfs_decode_extent(const uint8_t *raw, uint64_t *startoff,
                      uint64_t *startblock, uint32_t *blockcount) {
    if (!raw || !startoff || !startblock || !blockcount) return -1;

    uint64_t v0 = ((uint64_t)raw[0] << 56) | ((uint64_t)raw[1] << 48) |
                  ((uint64_t)raw[2] << 40) | ((uint64_t)raw[3] << 32) |
                  ((uint64_t)raw[4] << 24) | ((uint64_t)raw[5] << 16) |
                  ((uint64_t)raw[6] << 8)  | raw[7];
    uint64_t v1 = ((uint64_t)raw[8] << 56) | ((uint64_t)raw[9] << 48) |
                  ((uint64_t)raw[10] << 40) | ((uint64_t)raw[11] << 32) |
                  ((uint64_t)raw[12] << 24) | ((uint64_t)raw[13] << 16) |
                  ((uint64_t)raw[14] << 8)  | raw[15];
    uint64_t v2 = ((uint64_t)raw[16] << 56) | ((uint64_t)raw[17] << 48) |
                  ((uint64_t)raw[18] << 40) | ((uint64_t)raw[19] << 32) |
                  ((uint64_t)raw[20] << 24) | ((uint64_t)raw[21] << 16) |
                  ((uint64_t)raw[22] << 8)  | raw[23];

    *startoff = v0;
    *startblock = v1;
    *blockcount = (uint32_t)v2;

    return 0;
}

int xfs_read_disk_inode(uint64_t ino, void *di_buf) {
    if (!xfs_mounted || ino == 0 || !di_buf) return -1;

    uint64_t ag_inodes_per_ag = (uint64_t)xfs_sb.agsize * xfs_sb.inopblock;
    uint32_t ag_index = (uint32_t)(ino / ag_inodes_per_ag);
    uint64_t ag_ino = ino % ag_inodes_per_ag;

    if (ag_index >= xfs_sb.agcount) return -1;

    xfs_agi_t agi;
    if (xfs_read_agi(ag_index, &agi) != 0) return -1;

    uint64_t ag_start = (uint64_t)ag_index * xfs_sb.agsize * xfs_sb.blocksize;
    uint64_t inode_offset = ag_start + 4 * xfs_sb.blocksize + ag_ino * xfs_sb.inodesize;

    return xfs_read_from_device(inode_offset, di_buf, xfs_sb.inodesize);
}

static int xfs_read_inode_info(uint64_t ino, xfs_inode_info_t *info) {
    if (!info) return -1;

    xfs_dinode_t *di = (xfs_dinode_t *)kmalloc(xfs_sb.inodesize);
    if (!di) return -1;

    if (xfs_read_disk_inode(ino, di) != 0) {
        kfree(di);
        return -1;
    }

    if (di->magic != XFS_INODE_MAGIC) {
        kfree(di);
        return -1;
    }

    info->ino = ino;
    info->mode = di->mode;
    info->uid = di->uid;
    info->gid = di->gid;
    info->size = di->size;
    info->nlink = (di->version >= 2) ? di->nlink : di->link;
    info->atime = di->atime;
    info->mtime = di->mtime;
    info->ctime = di->ctime;
    info->format = di->format;
    info->nextents = di->nextents;

    if (di->format == XFS_DINODE_FMT_EXTENTS && di->nextents > 0) {
        uint8_t *bmx = (uint8_t *)di + 100;
        uint64_t startoff, startblock;
        uint32_t blockcount;
        xfs_decode_extent(bmx, &startoff, &startblock, &blockcount);
        info->extent_start = startblock;
        info->extent_count = blockcount;
    } else {
        info->extent_start = 0;
        info->extent_count = 0;
    }

    kfree(di);
    return 0;
}

/* ------------------------------------------------------------------ */
/* 目录操作                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t  namelen;
    uint8_t  filetype;
    uint64_t ino;
    char     name[256];
} xfs_sf_dir_entry_t;

int xfs_read_dir_entries(uint64_t dir_ino, xfs_dir_entry_t **head) {
    if (!head) return -1;
    *head = NULL;

    xfs_dinode_t *di = (xfs_dinode_t *)kmalloc(xfs_sb.inodesize);
    if (!di) return -1;

    if (xfs_read_disk_inode(dir_ino, di) != 0) {
        kfree(di);
        return -1;
    }

    xfs_dir_entry_t *tail = NULL;
    int count = 0;

    if (di->format == XFS_DINODE_FMT_LOCAL) {
        uint8_t *data = (uint8_t *)di + 100;
        uint32_t sf_count = *(uint32_t *)(data + 8);
        uint8_t *p = data + 16;

        for (uint32_t i = 0; i < sf_count && i < 512; i++) {
            if (p + 2 > (uint8_t *)di + xfs_sb.inodesize) break;

            uint8_t namelen = *p++;
            uint8_t filetype = *p++;

            xfs_dir_entry_t *entry = (xfs_dir_entry_t *)kmalloc(sizeof(xfs_dir_entry_t));
            if (!entry) break;
            memset(entry, 0, sizeof(xfs_dir_entry_t));

            uint32_t sf_i8count = *(uint32_t *)(data + 12);
            if (sf_i8count > 0) {
                entry->ino = *(uint64_t *)p;
                p += 8;
            } else {
                entry->ino = *(uint32_t *)p;
                p += 4;
            }

            entry->type = filetype;
            entry->name_len = namelen;
            if (namelen > 255) namelen = 255;
            memcpy(entry->name, p, namelen);
            entry->name[namelen] = '\0';
            p += namelen;
            entry->next = NULL;

            if (tail) {
                tail->next = entry;
            } else {
                *head = entry;
            }
            tail = entry;
            count++;
        }
    } else if (di->format == XFS_DINODE_FMT_EXTENTS || di->format == XFS_DINODE_FMT_BTREE) {
        uint8_t *bmx = (uint8_t *)di + 100;
        uint32_t nextents = di->nextents;

        for (uint32_t e = 0; e < nextents && e < 16; e++) {
            uint64_t startoff, startblock;
            uint32_t blockcount;
            xfs_decode_extent(bmx + e * 24, &startoff, &startblock, &blockcount);

            for (uint32_t b = 0; b < blockcount && count < 4096; b++) {
                uint8_t *blk = (uint8_t *)kmalloc(xfs_sb.blocksize);
                if (!blk) continue;

                if (xfs_read_block(startblock + b, blk) != 0) {
                    kfree(blk);
                    continue;
                }

                uint32_t blk_magic = *(uint32_t *)blk;
                if (blk_magic != XFS_DIR2_BLOCK_MAGIC &&
                    blk_magic != XFS_DIR2_LEAF_MAGIC &&
                    blk_magic != XFS_DIR2_FREE_MAGIC &&
                    blk_magic != XFS_DIR2_DATA_MAGIC) {
                    kfree(blk);
                    continue;
                }

                uint32_t hdr_size = 48;
                uint8_t *p = blk + hdr_size;
                uint8_t *end = blk + xfs_sb.blocksize;

                while (p + 8 < end) {
                    uint64_t entry_ino = *(uint64_t *)p;
                    if (entry_ino == 0) break;
                    p += 8;

                    if (p + 1 >= end) break;
                    uint8_t namelen = *p++;
                    if (namelen == 0) break;

                    xfs_dir_entry_t *entry = (xfs_dir_entry_t *)kmalloc(sizeof(xfs_dir_entry_t));
                    if (!entry) break;
                    memset(entry, 0, sizeof(xfs_dir_entry_t));

                    entry->ino = entry_ino;
                    entry->name_len = namelen;
                    if (namelen > 255) namelen = 255;
                    memcpy(entry->name, p, namelen);
                    entry->name[namelen] = '\0';
                    p += namelen;

                    uint32_t total = 9 + namelen;
                    total = (total + 7) & ~7;
                    p = blk + hdr_size + ((p - blk - hdr_size + 7) & ~7);

                    entry->next = NULL;
                    if (tail) {
                        tail->next = entry;
                    } else {
                        *head = entry;
                    }
                    tail = entry;
                    count++;
                }

                kfree(blk);
            }
        }
    }

    kfree(di);
    return count;
}

void xfs_free_dir_entries(xfs_dir_entry_t *head) {
    while (head) {
        xfs_dir_entry_t *next = head->next;
        kfree(head);
        head = next;
    }
}

/* ------------------------------------------------------------------ */
/* Extent 数据读取                                                    */
/* ------------------------------------------------------------------ */

int xfs_read_extent_data(uint64_t ino, void *buf, uint64_t offset, uint32_t size) {
    if (!buf) return -1;

    xfs_dinode_t *di = (xfs_dinode_t *)kmalloc(xfs_sb.inodesize);
    if (!di) return -1;

    if (xfs_read_disk_inode(ino, di) != 0) {
        kfree(di);
        return -1;
    }

    if (di->format == XFS_DINODE_FMT_LOCAL) {
        uint8_t *data = (uint8_t *)di + 100;
        if (offset < di->size) {
            uint32_t to_copy = size;
            if (offset + to_copy > di->size) to_copy = (uint32_t)(di->size - offset);
            memcpy(buf, data + offset, to_copy);
            kfree(di);
            return (int)to_copy;
        }
        kfree(di);
        return 0;
    }

    uint8_t *bmx = (uint8_t *)di + 100;
    uint32_t nextents = di->nextents;
    uint32_t blocksize = xfs_sb.blocksize;
    uint32_t bytes_read = 0;
    uint8_t *dst = (uint8_t *)buf;

    for (uint32_t e = 0; e < nextents && bytes_read < size; e++) {
        uint64_t startoff, startblock;
        uint32_t blockcount;
        xfs_decode_extent(bmx + e * 24, &startoff, &startblock, &blockcount);

        uint64_t extent_start_block = startoff;
        uint64_t extent_end_block = startoff + blockcount;
        uint32_t file_start_block = (uint32_t)(offset / blocksize);
        uint32_t file_end_block = (uint32_t)((offset + size + blocksize - 1) / blocksize);

        if (extent_end_block <= file_start_block) continue;
        if (extent_start_block >= file_end_block) break;

        uint32_t start = (file_start_block > extent_start_block) ?
                         file_start_block : (uint32_t)extent_start_block;
        uint32_t end = (file_end_block < extent_end_block) ?
                       file_end_block : (uint32_t)extent_end_block;

        for (uint32_t b = start; b < end && bytes_read < size; b++) {
            uint32_t block_offset = b - (uint32_t)extent_start_block;
            uint8_t *blk = (uint8_t *)kmalloc(blocksize);
            if (!blk) break;

            if (xfs_read_block(startblock + block_offset, blk) != 0) {
                kfree(blk);
                break;
            }

            uint32_t src_off = (b == file_start_block) ? (offset % blocksize) : 0;
            uint32_t to_copy = blocksize - src_off;
            if (to_copy > size - bytes_read) to_copy = size - bytes_read;

            memcpy(dst + bytes_read, blk + src_off, to_copy);
            bytes_read += to_copy;
            kfree(blk);
        }
    }

    kfree(di);
    return (int)bytes_read;
}

/* ------------------------------------------------------------------ */
/* VFS inode 操作实现                                                 */
/* ------------------------------------------------------------------ */

static dentry_t *xfs_lookup_op(dentry_t *dir, const char *name) {
    if (!dir || !dir->inode || !name) return NULL;
    if (!xfs_mounted) return NULL;

    xfs_inode_info_t *dir_info = (xfs_inode_info_t *)dir->inode->private_data;
    if (!dir_info) return NULL;

    xfs_dir_entry_t *entries = NULL;
    xfs_read_dir_entries(dir_info->ino, &entries);

    xfs_dir_entry_t *found = NULL;
    for (xfs_dir_entry_t *e = entries; e; e = e->next) {
        if (strcmp(e->name, name) == 0) {
            found = e;
            break;
        }
    }

    if (!found) {
        xfs_free_dir_entries(entries);
        return NULL;
    }

    xfs_inode_info_t *info = (xfs_inode_info_t *)kmalloc(sizeof(xfs_inode_info_t));
    if (!info) {
        xfs_free_dir_entries(entries);
        return NULL;
    }

    if (xfs_read_inode_info(found->ino, info) != 0) {
        kfree(info);
        xfs_free_dir_entries(entries);
        return NULL;
    }

    inode_t *vfs_inode = (inode_t *)kmalloc(sizeof(inode_t));
    if (!vfs_inode) {
        kfree(info);
        xfs_free_dir_entries(entries);
        return NULL;
    }
    memset(vfs_inode, 0, sizeof(inode_t));

    vfs_inode->ino = (uint32_t)info->ino;
    vfs_inode->mode = 0;
    uint32_t fmt = info->mode & 0xF000;
    if (fmt == 0x4000) vfs_inode->mode |= FILE_MODE_DIR;
    if (fmt == 0x8000) vfs_inode->mode |= FILE_MODE_REG;
    if (fmt == 0xA000) vfs_inode->mode |= FILE_MODE_LNK;
    if (info->mode & 0x0100) vfs_inode->mode |= FILE_MODE_READ;
    if (info->mode & 0x0080) vfs_inode->mode |= FILE_MODE_WRITE;
    if (info->mode & 0x0040) vfs_inode->mode |= FILE_MODE_EXEC;
    vfs_inode->uid = info->uid;
    vfs_inode->gid = info->gid;
    vfs_inode->size = (uint32_t)info->size;
    vfs_inode->nlinks = info->nlink;
    vfs_inode->atime = (uint32_t)info->atime;
    vfs_inode->mtime = (uint32_t)info->mtime;
    vfs_inode->ctime = (uint32_t)info->ctime;
    vfs_inode->sb = dir->inode->sb;
    vfs_inode->ops = &xfs_inode_ops;
    vfs_inode->private_data = info;

    dentry_t *d = (dentry_t *)kmalloc(sizeof(dentry_t));
    if (!d) {
        kfree(info);
        kfree(vfs_inode);
        xfs_free_dir_entries(entries);
        return NULL;
    }
    memset(d, 0, sizeof(dentry_t));
    strncpy(d->name, name, 255);
    d->name[255] = '\0';
    d->inode = vfs_inode;
    d->parent = dir;
    vfs_inode->dentries = d;

    d->next_sibling = dir->child;
    dir->child = d;

    xfs_free_dir_entries(entries);
    return d;
}

static int32_t xfs_create_op(dentry_t *dir, const char *name, uint32_t mode) {
    (void)dir; (void)name; (void)mode;
    return -EROFS;
}

static int32_t xfs_mkdir_op(dentry_t *dir, const char *name, uint32_t mode) {
    (void)dir; (void)name; (void)mode;
    return -EROFS;
}

static int32_t xfs_unlink_op(dentry_t *dir, const char *name) {
    (void)dir; (void)name;
    return -EROFS;
}

static int32_t xfs_rmdir_op(dentry_t *dir, const char *name) {
    (void)dir; (void)name;
    return -EROFS;
}

static int32_t xfs_rename_op(dentry_t *old_dir, const char *old_name,
                             dentry_t *new_dir, const char *new_name) {
    (void)old_dir; (void)old_name; (void)new_dir; (void)new_name;
    return -EROFS;
}

/* ------------------------------------------------------------------ */
/* VFS file 操作实现                                                  */
/* ------------------------------------------------------------------ */

static int32_t xfs_file_open(inode_t *inode, file_t *file) {
    if (!inode || !file) return -1;
    file->private_data = NULL;
    return 0;
}

static int32_t xfs_file_read(file_t *file, void *buf, uint32_t count) {
    if (!file || !file->inode || !buf) return -1;

    xfs_inode_info_t *info = (xfs_inode_info_t *)file->inode->private_data;
    if (!info) return -1;

    if (file->offset >= (uint32_t)info->size) return 0;
    uint32_t remaining = (uint32_t)info->size - file->offset;
    if (count > remaining) count = remaining;

    int ret = xfs_read_extent_data(info->ino, buf, file->offset, count);
    if (ret < 0) return ret;

    file->offset += ret;
    return ret;
}

static int32_t xfs_file_write(file_t *file, const void *buf, uint32_t count) {
    (void)file; (void)buf; (void)count;
    return -EROFS;
}

static int32_t xfs_file_close(file_t *file) {
    if (!file) return -1;
    file->private_data = NULL;
    return 0;
}

static int32_t xfs_file_seek(file_t *file, int32_t offset, int32_t whence) {
    if (!file || !file->inode) return -1;

    int32_t new_offset;
    switch (whence) {
        case SEEK_SET: new_offset = offset; break;
        case SEEK_CUR: new_offset = (int32_t)file->offset + offset; break;
        case SEEK_END: new_offset = (int32_t)file->inode->size + offset; break;
        default: return -EINVAL;
    }
    if (new_offset < 0) return -EINVAL;
    file->offset = (uint32_t)new_offset;
    return new_offset;
}

/* ------------------------------------------------------------------ */
/* XFS 挂载实现                                                       */
/* ------------------------------------------------------------------ */

int32_t xfs_mount(superblock_t *sb, void *data) {
    if (!sb) return -1;

    spinlock_lock(&xfs_lock);

    xfs_partition_start = data ? (uint32_t)(uintptr_t)data : 0;
    xfs_drive = 0;

    xfs_sb.blocksize = 4096;
    if (xfs_read_superblock(&xfs_sb) != 0) {
        klog_err("xfs: failed to read superblock");
        spinlock_unlock(&xfs_lock);
        return -1;
    }

    if (xfs_sb.magic != (uint32_t)XFS_MAGIC) {
        klog_err("xfs: invalid superblock magic 0x%x", xfs_sb.magic);
        spinlock_unlock(&xfs_lock);
        return -1;
    }
    if (xfs_sb.blocksize == 0 || xfs_sb.agcount == 0) {
        klog_err("xfs: invalid superblock parameters");
        spinlock_unlock(&xfs_lock);
        return -1;
    }

    xfs_mounted = 1;

    klog_info("xfs: superblock validated, magic=0x%X agcount=%u agsize=%u",
              xfs_sb.magic, xfs_sb.agcount, xfs_sb.agsize);
    klog_info("xfs: blocksize=%u inodesize=%u rootino=%llu",
              xfs_sb.blocksize, xfs_sb.inodesize,
              (unsigned long long)xfs_sb.rootino);

    inode_t *root_inode = (inode_t *)kmalloc(sizeof(inode_t));
    if (!root_inode) {
        xfs_mounted = 0;
        spinlock_unlock(&xfs_lock);
        return -1;
    }
    memset(root_inode, 0, sizeof(inode_t));

    xfs_inode_info_t *root_info = (xfs_inode_info_t *)kmalloc(sizeof(xfs_inode_info_t));
    if (!root_info) {
        kfree(root_inode);
        xfs_mounted = 0;
        spinlock_unlock(&xfs_lock);
        return -1;
    }

    if (xfs_read_inode_info(xfs_sb.rootino, root_info) != 0) {
        klog_warn("xfs: could not read root inode, using defaults");
        memset(root_info, 0, sizeof(xfs_inode_info_t));
        root_info->ino = xfs_sb.rootino;
        root_info->mode = 0x401FF;
        root_info->size = xfs_sb.blocksize;
        root_info->nlink = 1;
    } else {
        klog_info("xfs: root inode read successfully, mode=0%o size=%llu",
                  root_info->mode, (unsigned long long)root_info->size);
    }

    root_inode->ino = (uint32_t)root_info->ino;
    root_inode->mode = FILE_MODE_DIR | FILE_MODE_READ;
    root_inode->size = (uint32_t)root_info->size;
    root_inode->nlinks = root_info->nlink;
    root_inode->sb = sb;
    root_inode->ops = &xfs_inode_ops;
    root_inode->private_data = root_info;

    dentry_t *root_dentry = (dentry_t *)kmalloc(sizeof(dentry_t));
    if (!root_dentry) {
        kfree(root_info);
        kfree(root_inode);
        xfs_mounted = 0;
        spinlock_unlock(&xfs_lock);
        return -1;
    }
    memset(root_dentry, 0, sizeof(dentry_t));
    root_dentry->name[0] = '/';
    root_dentry->inode = root_inode;
    root_dentry->parent = root_dentry;
    root_inode->dentries = root_dentry;

    sb->root = root_inode;
    sb->fs_type = FS_TYPE_XFS;
    sb->block_size = xfs_sb.blocksize;

    spinlock_unlock(&xfs_lock);

    klog_info("xfs: filesystem mounted successfully");
    return 0;
}

/* ------------------------------------------------------------------ */
/* 公共 API                                                           */
/* ------------------------------------------------------------------ */

int xfs_init(void) {
    spinlock_init(&xfs_lock);
    xfs_mounted = 0;
    klog_info("xfs: driver initialized");
    return 0;
}

int xfs_mount_device(const char *path, uint32_t device_id) {
    (void)path;
    (void)device_id;
    return 0;
}

int xfs_read_file(const char *path, void *buf, uint32_t size) {
    if (!path || !buf || !xfs_mounted) return -1;
    (void)size;
    return -1;
}

int xfs_list_dir(const char *path) {
    if (!path || !xfs_mounted) return -1;
    return -1;
}
