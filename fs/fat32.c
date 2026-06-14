#include "fat32.h"
#include "vfs.h"
#include "ide.h"
#include "kheap.h"
#include "string.h"
#include "sync.h"
#include "stddef.h"

static fat32_info_t fs_info;
static uint32_t *fat_cache;
static mutex_t fat32_lock;
static uint32_t current_drive;

static int32_t fat32_read_sector(uint32_t lba, void *buf) {
    return ide_read_sectors(current_drive, 1, lba, buf);
}

static int32_t fat32_write_sector(uint32_t lba, void *buf) {
    return ide_write_sectors(current_drive, 1, lba, buf);
}

int32_t fat32_init(uint32_t drive, uint32_t partition_start) {
    current_drive = drive;
    mutex_init(&fat32_lock);

    FAT32_BOOT_SECTOR bs;
    if (fat32_read_sector(partition_start, &bs) != 0) return -1;

    fs_info.bytes_per_sector = bs.bytes_per_sector;
    fs_info.sectors_per_cluster = bs.sectors_per_cluster;
    fs_info.root_cluster = bs.root_cluster;
    fs_info.total_clusters = bs.total_sectors_32 / bs.sectors_per_cluster;
    fs_info.fat_start = partition_start + bs.reserved_sectors;
    fs_info.data_start = fs_info.fat_start + (bs.num_fats * bs.sectors_per_fat_32);
    fs_info.fs_sector_start = partition_start;

    uint32_t fat_sectors = bs.sectors_per_fat_32;
    uint32_t fat_entries_per_sector = fs_info.bytes_per_sector / 4;
    uint32_t total_fat_entries = fat_sectors * fat_entries_per_sector;

    fat_cache = (uint32_t *)kmalloc(fat_sectors * fs_info.bytes_per_sector);
    if (!fat_cache) return -1;

    for (uint32_t i = 0; i < fat_sectors; i++) {
        if (fat32_read_sector(fs_info.fat_start + i, (uint8_t *)fat_cache + i * fs_info.bytes_per_sector) != 0) {
            kfree(fat_cache);
            fat_cache = NULL;
            return -1;
        }
    }

    return 0;
}

int32_t fat32_get_cluster_chain(uint32_t start, uint32_t *chain, uint32_t max) {
    if (!fat_cache || start < 2) return -1;

    uint32_t count = 0;
    uint32_t cluster = start;

    while (cluster >= 2 && cluster < FAT32_EOC && count < max) {
        chain[count++] = cluster;
        cluster = fat_cache[cluster] & 0x0FFFFFFF;
    }

    return (int32_t)count;
}

static uint32_t fat32_cluster_to_lba(uint32_t cluster) {
    return fs_info.data_start + (cluster - 2) * fs_info.sectors_per_cluster;
}

uint32_t fat32_open_dir(uint32_t cluster) {
    if (cluster == 0) return fs_info.root_cluster;
    return cluster;
}

int32_t fat32_read_dir(uint32_t cluster, FAT32_DIR_ENTRY *entries, uint32_t max) {
    if (!fat_cache) return -1;

    uint32_t chain[4096];
    int32_t chain_len = fat32_get_cluster_chain(cluster, chain, 4096);
    if (chain_len < 0) return -1;

    uint32_t entry_count = 0;
    uint32_t entries_per_cluster = (fs_info.sectors_per_cluster * fs_info.bytes_per_sector) / sizeof(FAT32_DIR_ENTRY);

    for (int32_t c = 0; c < chain_len && entry_count < max; c++) {
        uint8_t *buf = (uint8_t *)kmalloc(fs_info.sectors_per_cluster * fs_info.bytes_per_sector);
        if (!buf) return -1;

        uint32_t lba = fat32_cluster_to_lba(chain[c]);
        for (uint32_t s = 0; s < fs_info.sectors_per_cluster; s++) {
            fat32_read_sector(lba + s, buf + s * fs_info.bytes_per_sector);
        }

        FAT32_DIR_ENTRY *dir_entries = (FAT32_DIR_ENTRY *)buf;
        for (uint32_t i = 0; i < entries_per_cluster && entry_count < max; i++) {
            if (dir_entries[i].name[0] == 0x00) {
                kfree(buf);
                return (int32_t)entry_count;
            }
            if (dir_entries[i].name[0] == 0xE5) continue;
            if (dir_entries[i].attrs == FAT_ATTR_LFN) continue;

            memcpy(&entries[entry_count], &dir_entries[i], sizeof(FAT32_DIR_ENTRY));
            entry_count++;
        }

        kfree(buf);
    }

    return (int32_t)entry_count;
}

/* Convert LFN entry UCS-2 characters to ASCII */
static void fat32_lfn_to_ascii(const FAT32_LFN_ENTRY *lfn, char *out, int *out_len) {
    uint16_t ucs2[13];
    int idx = 0;
    /* name1: 5 chars */
    for (int i = 0; i < 5 && idx < 13; i++) {
        ucs2[idx++] = lfn->name1[i*2] | (lfn->name1[i*2+1] << 8);
    }
    /* name2: 6 chars */
    for (int i = 0; i < 6 && idx < 13; i++) {
        ucs2[idx++] = lfn->name2[i*2] | (lfn->name2[i*2+1] << 8);
    }
    /* name3: 2 chars */
    for (int i = 0; i < 2 && idx < 13; i++) {
        ucs2[idx++] = lfn->name3[i*2] | (lfn->name3[i*2+1] << 8);
    }

    *out_len = 0;
    for (int i = 0; i < idx; i++) {
        if (ucs2[i] == 0x0000 || ucs2[i] == 0xFFFF) break;
        out[*out_len] = (ucs2[i] < 128) ? (char)ucs2[i] : '?';
        (*out_len)++;
    }
}

int32_t fat32_find_entry(uint32_t dir_cluster, const char *name, FAT32_DIR_ENTRY *out) {
    if (!fat_cache) return -1;

    uint32_t chain[4096];
    int32_t chain_len = fat32_get_cluster_chain(dir_cluster, chain, 4096);
    if (chain_len < 0) return -1;

    uint32_t cluster_size = fs_info.sectors_per_cluster * fs_info.bytes_per_sector;
    uint32_t entries_per_cluster = cluster_size / sizeof(FAT32_DIR_ENTRY);

    /* LFN buffer */
    char lfn_buf[256];
    int lfn_len = 0;

    for (int32_t c = 0; c < chain_len; c++) {
        uint8_t *buf = (uint8_t *)kmalloc(cluster_size);
        if (!buf) return -1;

        uint32_t lba = fat32_cluster_to_lba(chain[c]);
        for (uint32_t s = 0; s < fs_info.sectors_per_cluster; s++) {
            fat32_read_sector(lba + s, buf + s * fs_info.bytes_per_sector);
        }

        FAT32_DIR_ENTRY *dir_entries = (FAT32_DIR_ENTRY *)buf;
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            if (dir_entries[i].name[0] == 0x00) {
                kfree(buf);
                return -1; /* End of directory */
            }
            if (dir_entries[i].name[0] == 0xE5) {
                lfn_len = 0; /* Deleted entry, reset LFN */
                continue;
            }

            if (dir_entries[i].attrs == FAT_ATTR_LFN) {
                /* Long File Name entry */
                FAT32_LFN_ENTRY *lfn = (FAT32_LFN_ENTRY *)&dir_entries[i];
                int seq = lfn->order & 0x1F;
                if (lfn->order & 0x40) {
                    /* Start of LFN sequence */
                    lfn_len = 0;
                    lfn_buf[0] = '\0';
                }
                /* Insert LFN chars at correct position */
                char part[14];
                int part_len = 0;
                fat32_lfn_to_ascii(lfn, part, &part_len);
                /* Prepend to lfn_buf (LFN entries are in reverse order) */
                int insert_pos = (seq - 1) * 13;
                if (insert_pos + part_len <= 255) {
                    memcpy(lfn_buf + insert_pos, part, part_len);
                    if (insert_pos + part_len > lfn_len)
                        lfn_len = insert_pos + part_len;
                    lfn_buf[lfn_len] = '\0';
                }
                continue;
            }

            /* Regular 8.3 entry - check both LFN and short name */
            /* First check LFN */
            if (lfn_len > 0 && strcmp(lfn_buf, name) == 0) {
                if (out) memcpy(out, &dir_entries[i], sizeof(FAT32_DIR_ENTRY));
                kfree(buf);
                return 0;
            }

            /* Then check short name */
            char entry_name[13];
            int j = 0;
            for (j = 0; j < 8 && dir_entries[i].name[j] != ' '; j++) {
                entry_name[j] = dir_entries[i].name[j];
            }
            if (dir_entries[i].ext[0] != ' ') {
                entry_name[j++] = '.';
                for (int k = 0; k < 3 && dir_entries[i].ext[k] != ' '; k++) {
                    entry_name[j++] = dir_entries[i].ext[k];
                }
            }
            entry_name[j] = '\0';

            if (strcmp(entry_name, name) == 0) {
                if (out) memcpy(out, &dir_entries[i], sizeof(FAT32_DIR_ENTRY));
                kfree(buf);
                return 0;
            }

            lfn_len = 0; /* Reset LFN for next entry */
        }

        kfree(buf);
    }

    return -1;
}

static uint32_t fat32_alloc_cluster(void) {
    if (!fat_cache) return 0;

    for (uint32_t i = 2; i < fs_info.total_clusters + 2; i++) {
        if ((fat_cache[i] & 0x0FFFFFFF) == FAT32_FREE) {
            fat_cache[i] = FAT32_EOC;
            uint32_t fat_sector = i / (fs_info.bytes_per_sector / 4);
            fat32_write_sector(fs_info.fat_start + fat_sector,
                (uint8_t *)fat_cache + fat_sector * fs_info.bytes_per_sector);
            return i;
        }
    }

    return 0;
}

static void fat32_free_cluster(uint32_t cluster) {
    if (!fat_cache || cluster < 2) return;

    fat_cache[cluster] = FAT32_FREE;
    uint32_t fat_sector = cluster / (fs_info.bytes_per_sector / 4);
    fat32_write_sector(fs_info.fat_start + fat_sector,
        (uint8_t *)fat_cache + fat_sector * fs_info.bytes_per_sector);
}

int32_t fat32_read(inode_t *inode, void *buf, uint32_t count) {
    if (!inode || !buf || !fat_cache) return -1;

    mutex_lock(&fat32_lock);

    uint32_t start_cluster = (uint32_t)(uintptr_t)inode->private_data;
    uint32_t chain[4096];
    int32_t chain_len = fat32_get_cluster_chain(start_cluster, chain, 4096);
    if (chain_len < 0) {
        mutex_unlock(&fat32_lock);
        return -1;
    }

    uint32_t cluster_size = fs_info.sectors_per_cluster * fs_info.bytes_per_sector;
    uint32_t bytes_read = 0;
    uint32_t remaining = count;
    if (remaining > inode->size) remaining = inode->size;

    uint8_t *dst = (uint8_t *)buf;
    uint8_t *tmp = (uint8_t *)kmalloc(cluster_size);
    if (!tmp) {
        mutex_unlock(&fat32_lock);
        return -1;
    }

    for (int32_t i = 0; i < chain_len && remaining > 0; i++) {
        uint32_t lba = fat32_cluster_to_lba(chain[i]);
        for (uint32_t s = 0; s < fs_info.sectors_per_cluster; s++) {
            fat32_read_sector(lba + s, tmp + s * fs_info.bytes_per_sector);
        }

        uint32_t to_copy = remaining > cluster_size ? cluster_size : remaining;
        memcpy(dst, tmp, to_copy);
        dst += to_copy;
        bytes_read += to_copy;
        remaining -= to_copy;
    }

    kfree(tmp);
    mutex_unlock(&fat32_lock);
    return (int32_t)bytes_read;
}

int32_t fat32_write(inode_t *inode, const void *buf, uint32_t count) {
    if (!inode || !buf || !fat_cache) return -1;

    mutex_lock(&fat32_lock);

    uint32_t start_cluster = (uint32_t)(uintptr_t)inode->private_data;
    uint32_t cluster_size = fs_info.sectors_per_cluster * fs_info.bytes_per_sector;
    uint32_t clusters_needed = (count + cluster_size - 1) / cluster_size;

    uint32_t chain[4096];
    int32_t chain_len = 0;

    if (start_cluster >= 2) {
        chain_len = fat32_get_cluster_chain(start_cluster, chain, 4096);
    }

    while (chain_len < (int32_t)clusters_needed) {
        uint32_t new_cluster = fat32_alloc_cluster();
        if (new_cluster == 0) {
            mutex_unlock(&fat32_lock);
            return -1;
        }
        if (chain_len > 0) {
            fat_cache[chain[chain_len - 1]] = new_cluster;
        } else {
            start_cluster = new_cluster;
            inode->private_data = (void *)(uintptr_t)new_cluster;
        }
        chain[chain_len++] = new_cluster;
    }

    const uint8_t *src = (const uint8_t *)buf;
    uint32_t bytes_written = 0;
    uint32_t remaining = count;

    for (int32_t i = 0; i < chain_len && remaining > 0; i++) {
        uint8_t *tmp = (uint8_t *)kmalloc(cluster_size);
        if (!tmp) {
            mutex_unlock(&fat32_lock);
            return (int32_t)bytes_written;
        }
        memset(tmp, 0, cluster_size);

        uint32_t to_write = remaining > cluster_size ? cluster_size : remaining;
        memcpy(tmp, src, to_write);

        uint32_t lba = fat32_cluster_to_lba(chain[i]);
        for (uint32_t s = 0; s < fs_info.sectors_per_cluster; s++) {
            fat32_write_sector(lba + s, tmp + s * fs_info.bytes_per_sector);
        }

        src += to_write;
        bytes_written += to_write;
        remaining -= to_write;
        kfree(tmp);
    }

    inode->size = bytes_written > inode->size ? bytes_written : inode->size;
    mutex_unlock(&fat32_lock);
    return (int32_t)bytes_written;
}

static int32_t fat32_file_open(inode_t *inode, file_t *file) {
    (void)inode;
    (void)file;
    return 0;
}

static int32_t fat32_file_read(file_t *file, void *buf, uint32_t count) {
    if (!file || !file->inode) return -1;
    int32_t ret = fat32_read(file->inode, buf, count);
    if (ret > 0) file->offset += ret;
    return ret;
}

static int32_t fat32_file_write(file_t *file, const void *buf, uint32_t count) {
    if (!file || !file->inode) return -1;
    int32_t ret = fat32_write(file->inode, buf, count);
    if (ret > 0) file->offset += ret;
    return ret;
}

static int32_t fat32_file_close(file_t *file) {
    (void)file;
    return 0;
}

static int32_t fat32_file_seek(file_t *file, int32_t offset, int32_t whence) {
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

/* ------------------------------------------------------------------ */
/* 辅助函数：将文件名转换为FAT32 8.3格式                                  */
/* ------------------------------------------------------------------ */

static void fat32_make_short_name(const char *name, FAT32_DIR_ENTRY *entry) {
    int i;

    /* 初始化name和ext为空格 */
    for (i = 0; i < 8; i++) entry->name[i] = ' ';
    for (i = 0; i < 3; i++) entry->ext[i] = ' ';

    /* 填充主名（最多8字符） */
    for (i = 0; i < 8 && name[i] != '\0' && name[i] != '.'; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c -= 32; /* 转大写 */
        entry->name[i] = (uint8_t)c;
    }

    /* 查找扩展名 */
    const char *dot = NULL;
    for (int j = 0; name[j]; j++) {
        if (name[j] == '.') dot = &name[j];
    }

    if (dot) {
        dot++; /* 跳过'.' */
        for (i = 0; i < 3 && dot[i] != '\0'; i++) {
            char c = dot[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            entry->ext[i] = (uint8_t)c;
        }
    }
}

/* ------------------------------------------------------------------ */
/* 辅助函数：在目录中写入/修改一个目录项                                   */
/* ------------------------------------------------------------------ */

static int32_t fat32_write_dir_entry(uint32_t dir_cluster,
                                      const char *name,
                                      uint32_t cluster,
                                      uint32_t file_size,
                                      uint8_t attrs) {
    if (!fat_cache) return -1;

    /* 读取整个目录的原始数据以便找到空位或追加 */
    FAT32_DIR_ENTRY entries[512];
    int32_t count = fat32_read_dir(dir_cluster, entries, 512);
    if (count < 0) count = 0;

    /* 查找已删除的条目(0xE5)或末尾空位(0x00)来复用 */
    int32_t slot = -1;
    for (int32_t i = 0; i < count; i++) {
        if (entries[i].name[0] == 0xE5) {
            slot = i;
            break;
        }
    }

    FAT32_DIR_ENTRY new_entry;
    memset(&new_entry, 0, sizeof(FAT32_DIR_ENTRY));
    fat32_make_short_name(name, &new_entry);
    new_entry.attrs = attrs;
    new_entry.cluster_hi = (uint16_t)(cluster >> 16);
    new_entry.cluster_lo = (uint16_t)(cluster & 0xFFFF);
    new_entry.file_size = file_size;

    uint32_t cluster_size = fs_info.sectors_per_cluster * fs_info.bytes_per_sector;
    uint32_t max_entries_per_cluster = cluster_size / sizeof(FAT32_DIR_ENTRY);

    if (slot >= 0) {
        /* 复用已删除的槽位：直接覆盖对应扇区 */
        uint32_t chain[4096];
        int32_t chain_len = fat32_get_cluster_chain(dir_cluster, chain, 4096);
        if (chain_len <= 0) return -1;

        uint32_t target_cluster_idx = (uint32_t)slot / max_entries_per_cluster;
        uint32_t entry_offset_in_cluster = (uint32_t)slot % max_entries_per_cluster;

        if (target_cluster_idx >= (uint32_t)chain_len) return -1;

        uint8_t *buf = (uint8_t *)kmalloc(cluster_size);
        if (!buf) return -1;

        uint32_t lba = fat32_cluster_to_lba(chain[target_cluster_idx]);
        for (uint32_t s = 0; s < fs_info.sectors_per_cluster; s++) {
            fat32_read_sector(lba + s, buf + s * fs_info.bytes_per_sector);
        }

        FAT32_DIR_ENTRY *dir_entries = (FAT32_DIR_ENTRY *)buf;
        memcpy(&dir_entries[entry_offset_in_cluster], &new_entry, sizeof(FAT32_DIR_ENTRY));

        for (uint32_t s = 0; s < fs_info.sectors_per_cluster; s++) {
            fat32_write_sector(lba + s, buf + s * fs_info.bytes_per_sector);
        }
        kfree(buf);
    } else {
        /* 追加到目录末尾 */
        uint32_t total_slots = (uint32_t)count;
        uint32_t target_cluster_idx = total_slots / max_entries_per_cluster;
        uint32_t entry_offset_in_cluster = total_slots % max_entries_per_cluster;

        uint32_t chain[4096];
        int32_t chain_len = fat32_get_cluster_chain(dir_cluster, chain, 4096);
        if (chain_len < 0) return -1;

        /* 如果当前cluster不够，分配新cluster并链接 */
        while (target_cluster_idx >= (uint32_t)chain_len) {
            uint32_t new_clu = fat32_alloc_cluster();
            if (new_clu == 0) return -1;
            fat_cache[chain[chain_len - 1]] = new_clu;
            /* 写回FAT表更新链 */
            uint32_t fat_sector = chain[chain_len - 1] / (fs_info.bytes_per_sector / 4);
            fat32_write_sector(fs_info.fat_start + fat_sector,
                (uint8_t *)fat_cache + fat_sector * fs_info.bytes_per_sector);
            chain[chain_len] = new_clu;
            chain_len++;

            /* 清零新cluster */
            uint8_t *zero_buf = (uint8_t *)kmalloc(cluster_size);
            if (zero_buf) {
                memset(zero_buf, 0, cluster_size);
                uint32_t nlba = fat32_cluster_to_lba(new_clu);
                for (uint32_t s = 0; s < fs_info.sectors_per_cluster; s++) {
                    fat32_write_sector(nlba + s, zero_buf + s * fs_info.bytes_per_sector);
                }
                kfree(zero_buf);
            }
        }

        uint8_t *buf = (uint8_t *)kmalloc(cluster_size);
        if (!buf) return -1;

        uint32_t lba = fat32_cluster_to_lba(chain[target_cluster_idx]);
        for (uint32_t s = 0; s < fs_info.sectors_per_cluster; s++) {
            fat32_read_sector(lba + s, buf + s * fs_info.bytes_per_sector);
        }

        FAT32_DIR_ENTRY *dir_entries = (FAT32_DIR_ENTRY *)buf;
        memcpy(&dir_entries[entry_offset_in_cluster], &new_entry, sizeof(FAT32_DIR_ENTRY));

        for (uint32_t s = 0; s < fs_info.sectors_per_cluster; s++) {
            fat32_write_sector(lba + s, buf + s * fs_info.bytes_per_sector);
        }
        kfree(buf);
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* 辅助函数：删除目录中的一个条目                                       */
/* ------------------------------------------------------------------ */

static int32_t fat32_remove_dir_entry(uint32_t dir_cluster, const char *name) {
    if (!fat_cache) return -1;

    uint32_t cluster_size = fs_info.sectors_per_cluster * fs_info.bytes_per_sector;
    uint32_t entries_per_cluster = cluster_size / sizeof(FAT32_DIR_ENTRY);

    uint32_t chain[4096];
    int32_t chain_len = fat32_get_cluster_chain(dir_cluster, chain, 4096);
    if (chain_len < 0) return -1;

    for (int32_t c = 0; c < chain_len; c++) {
        uint8_t *buf = (uint8_t *)kmalloc(cluster_size);
        if (!buf) return -1;

        uint32_t lba = fat32_cluster_to_lba(chain[c]);
        for (uint32_t s = 0; s < fs_info.sectors_per_cluster; s++) {
            fat32_read_sector(lba + s, buf + s * fs_info.bytes_per_sector);
        }

        FAT32_DIR_ENTRY *dir_entries = (FAT32_DIR_ENTRY *)buf;
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            if (dir_entries[i].name[0] == 0x00) break;
            if (dir_entries[i].name[0] == 0xE5) continue;
            if (dir_entries[i].attrs == FAT_ATTR_LFN) continue;

            /* 将条目转为名字进行比较 */
            char entry_name[13];
            int j = 0;
            for (j = 0; j < 8 && dir_entries[i].name[j] != ' '; j++)
                entry_name[j] = dir_entries[i].name[j];
            if (dir_entries[i].ext[0] != ' ') {
                entry_name[j++] = '.';
                for (int k = 0; k < 3 && dir_entries[i].ext[k] != ' '; k++)
                    entry_name[j++] = dir_entries[i].ext[k];
            }
            entry_name[j] = '\0';

            if (strcmp(entry_name, name) == 0) {
                /* 标记为已删除 */
                dir_entries[i].name[0] = 0xE5;

                for (uint32_t s = 0; s < fs_info.sectors_per_cluster; s++) {
                    fat32_write_sector(lba + s, buf + s * fs_info.bytes_per_sector);
                }
                kfree(buf);
                return 0;
            }
        }

        kfree(buf);
    }

    return -1;
}

/* ------------------------------------------------------------------ */
/* FAT32 inode_ops VFS 回调实现                                          */
/* ------------------------------------------------------------------ */

static dentry_t *fat32_vfs_lookup(dentry_t *dir, const char *name);
static int32_t fat32_vfs_create(dentry_t *dir, const char *name, uint32_t mode);
static int32_t fat32_vfs_mkdir(dentry_t *dir, const char *name, uint32_t mode);
static int32_t fat32_vfs_unlink(dentry_t *dir, const char *name);
static int32_t fat32_vfs_rmdir(dentry_t *dir, const char *name);
static int32_t fat32_vfs_rename(dentry_t *old_dir, const char *old_name, dentry_t *new_dir, const char *new_name);

static inode_ops_t fat32_inode_ops = {
    .lookup = fat32_vfs_lookup,
    .create = fat32_vfs_create,
    .mkdir = fat32_vfs_mkdir,
    .unlink = fat32_vfs_unlink,
    .rmdir = fat32_vfs_rmdir,
    .rename = fat32_vfs_rename,
    .readlink = NULL,
    .symlink = NULL
};

/* lookup: 在FAT32目录中查找指定名称的条目 */
static dentry_t *fat32_vfs_lookup(dentry_t *dir, const char *name) {
    if (!dir || !dir->inode || !name) return NULL;

    uint32_t dir_cluster = (uint32_t)(uintptr_t)dir->inode->private_data;
    if (dir_cluster == 0) dir_cluster = fs_info.root_cluster;

    FAT32_DIR_ENTRY entry;
    if (fat32_find_entry(dir_cluster, name, &entry) != 0) return NULL;

    /* 构建VFS inode */
    inode_t *vfs_inode = (inode_t *)kmalloc(sizeof(inode_t));
    if (!vfs_inode) return NULL;
    memset(vfs_inode, 0, sizeof(inode_t));

    uint32_t entry_cluster = ((uint32_t)entry.cluster_hi << 16) | entry.cluster_lo;
    vfs_inode->ino = entry_cluster;
    vfs_inode->mode = FILE_MODE_READ | FILE_MODE_EXEC;
    if (!(entry.attrs & FAT_ATTR_READ_ONLY))
        vfs_inode->mode |= FILE_MODE_WRITE;
    if (entry.attrs & FAT_ATTR_DIRECTORY)
        vfs_inode->mode |= FILE_MODE_DIR;
    else
        vfs_inode->mode |= FILE_MODE_REG;
    vfs_inode->size = entry.file_size;
    vfs_inode->nlinks = 1;
    vfs_inode->sb = dir->inode->sb;
    vfs_inode->ops = &fat32_inode_ops;
    vfs_inode->private_data = (void *)(uintptr_t)entry_cluster;

    /* 构建dentry */
    dentry_t *d = (dentry_t *)kmalloc(sizeof(dentry_t));
    if (!d) { kfree(vfs_inode); return NULL; }
    memset(d, 0, sizeof(dentry_t));
    strncpy(d->name, name, 255);
    d->name[255] = '\0';
    d->inode = vfs_inode;
    d->parent = dir;
    vfs_inode->dentries = d;

    /* 挂入父目录子节点链表 */
    d->next_sibling = dir->child;
    dir->child = d;

    return d;
}

/* create: 在目录中创建新文件（分配cluster + 写目录项） */
static int32_t fat32_vfs_create(dentry_t *dir, const char *name, uint32_t mode) {
    if (!dir || !dir->inode || !name) return -1;
    (void)mode;

    uint32_t dir_cluster = (uint32_t)(uintptr_t)dir->inode->private_data;
    if (dir_cluster == 0) dir_cluster = fs_info.root_cluster;

    mutex_lock(&fat32_lock);

    /* 分配一个新cluster给新文件 */
    uint32_t new_cluster = fat32_alloc_cluster();
    if (new_cluster == 0) {
        mutex_unlock(&fat32_lock);
        return -1;
    }

    /* 在父目录中写入目录项 */
    int32_t ret = fat32_write_dir_entry(dir_cluster, name, new_cluster, 0,
                                         FAT_ATTR_ARCHIVE);
    if (ret != 0) {
        fat32_free_cluster(new_cluster);
        mutex_unlock(&fat32_lock);
        return -1;
    }

    mutex_unlock(&fat32_lock);
    return 0;
}

/* mkdir: 创建目录（创建特殊目录项 . 和 ..） */
static int32_t fat32_vfs_mkdir(dentry_t *dir, const char *name, uint32_t mode) {
    if (!dir || !dir->inode || !name) return -1;
    (void)mode;

    uint32_t dir_cluster = (uint32_t)(uintptr_t)dir->inode->private_data;
    if (dir_cluster == 0) dir_cluster = fs_info.root_cluster;

    mutex_lock(&fat32_lock);

    /* 为新目录分配cluster */
    uint32_t new_cluster = fat32_alloc_cluster();
    if (new_cluster == 0) {
        mutex_unlock(&fat32_lock);
        return -1;
    }

    uint32_t cluster_size = fs_info.sectors_per_cluster * fs_info.bytes_per_sector;

    /* 清零新目录cluster */
    uint8_t *buf = (uint8_t *)kmalloc(cluster_size);
    if (!buf) {
        fat32_free_cluster(new_cluster);
        mutex_unlock(&fat32_lock);
        return -1;
    }
    memset(buf, 0, cluster_size);

    /* 写入 "." 目录项 */
    FAT32_DIR_ENTRY *dot = (FAT32_DIR_ENTRY *)buf;
    memset(dot, 0, sizeof(FAT32_DIR_ENTRY));
    dot->name[0] = '.';
    dot->attrs = FAT_ATTR_DIRECTORY;
    dot->cluster_hi = (uint16_t)(new_cluster >> 16);
    dot->cluster_lo = (uint16_t)(new_cluster & 0xFFFF);

    /* 写入 ".." 目录项 */
    FAT32_DIR_ENTRY *dotdot = (FAT32_DIR_ENTRY *)(buf + sizeof(FAT32_DIR_ENTRY));
    memset(dotdot, 0, sizeof(FAT32_DIR_ENTRY));
    dotdot->name[0] = '.';
    dotdot->name[1] = '.';
    dotdot->attrs = FAT_ATTR_DIRECTORY;
    dotdot->cluster_hi = (uint16_t)(dir_cluster >> 16);
    dotdot->cluster_lo = (uint16_t)(dir_cluster & 0xFFFF);

    /* 将目录数据写回磁盘 */
    uint32_t lba = fat32_cluster_to_lba(new_cluster);
    for (uint32_t s = 0; s < fs_info.sectors_per_cluster; s++) {
        fat32_write_sector(lba + s, buf + s * fs_info.bytes_per_sector);
    }
    kfree(buf);

    /* 在父目录中创建目录项 */
    int32_t ret = fat32_write_dir_entry(dir_cluster, name, new_cluster, 0,
                                         FAT_ATTR_DIRECTORY);
    if (ret != 0) {
        fat32_free_cluster(new_cluster);
        mutex_unlock(&fat32_lock);
        return -1;
    }

    mutex_unlock(&fat32_lock);
    return 0;
}

/* unlink: 删除文件/目录项 */
static int32_t fat32_vfs_unlink(dentry_t *dir, const char *name) {
    if (!dir || !dir->inode || !name) return -1;

    uint32_t dir_cluster = (uint32_t)(uintptr_t)dir->inode->private_data;
    if (dir_cluster == 0) dir_cluster = fs_info.root_cluster;

    mutex_lock(&fat32_lock);

    /* 先查找条目以获取其cluster，释放数据空间 */
    FAT32_DIR_ENTRY entry;
    if (fat32_find_entry(dir_cluster, name, &entry) == 0) {
        uint32_t entry_cluster = ((uint32_t)entry.cluster_hi << 16) | entry.cluster_lo;
        if (entry_cluster >= 2) {
            /* 释放该文件的cluster链 */
            uint32_t chain[4096];
            int32_t chain_len = fat32_get_cluster_chain(entry_cluster, chain, 4096);
            for (int32_t i = 0; i < chain_len; i++) {
                fat32_free_cluster(chain[i]);
            }
        }
    }

    int32_t ret = fat32_remove_dir_entry(dir_cluster, name);
    mutex_unlock(&fat32_lock);
    return ret;
}

/* rmdir: 删除空目录 */
static int32_t fat32_vfs_rmdir(dentry_t *dir, const char *name) {
    if (!dir || !dir->inode || !name) return -1;

    uint32_t dir_cluster = (uint32_t)(uintptr_t)dir->inode->private_data;
    if (dir_cluster == 0) dir_cluster = fs_info.root_cluster;

    mutex_lock(&fat32_lock);

    /* 查找目标目录项 */
    FAT32_DIR_ENTRY entry;
    if (fat32_find_entry(dir_cluster, name, &entry) != 0) {
        mutex_unlock(&fat32_lock);
        return -1;
    }

    uint32_t target_cluster = ((uint32_t)entry.cluster_hi << 16) | entry.cluster_lo;

    /* 检查目录是否为空（只应有 . 和 .. 条目） */
    FAT32_DIR_ENTRY check_entries[64];
    int32_t check_count = fat32_read_dir(target_cluster, check_entries, 64);
    if (check_count > 2) {
        /* 目录非空 */
        mutex_unlock(&fat32_lock);
        return -1;
    }

    /* 释放目标目录的cluster */
    if (target_cluster >= 2) {
        uint32_t chain[4096];
        int32_t chain_len = fat32_get_cluster_chain(target_cluster, chain, 4096);
        for (int32_t i = 0; i < chain_len; i++) {
            fat32_free_cluster(chain[i]);
        }
    }

    /* 从父目录中删除目录项 */
    int32_t ret = fat32_remove_dir_entry(dir_cluster, name);
    mutex_unlock(&fat32_lock);
    return ret;
}

/* rename: 重命名/移动文件或目录 */
static int32_t fat32_vfs_rename(dentry_t *old_dir, const char *old_name,
                                dentry_t *new_dir, const char *new_name) {
    if (!old_dir || !old_dir->inode || !new_dir || !new_dir->inode) return -1;
    if (!old_name || !new_name) return -1;

    uint32_t old_dir_cluster = (uint32_t)(uintptr_t)old_dir->inode->private_data;
    uint32_t new_dir_cluster = (uint32_t)(uintptr_t)new_dir->inode->private_data;
    if (old_dir_cluster == 0) old_dir_cluster = fs_info.root_cluster;
    if (new_dir_cluster == 0) new_dir_cluster = fs_info.root_cluster;

    mutex_lock(&fat32_lock);

    /* Find the source entry */
    FAT32_DIR_ENTRY entry;
    if (fat32_find_entry(old_dir_cluster, old_name, &entry) != 0) {
        mutex_unlock(&fat32_lock);
        return -1;
    }

    uint32_t entry_cluster = ((uint32_t)entry.cluster_hi << 16) | entry.cluster_lo;

    /* If target already exists, remove it first */
    FAT32_DIR_ENTRY existing;
    if (fat32_find_entry(new_dir_cluster, new_name, &existing) == 0) {
        fat32_remove_dir_entry(new_dir_cluster, new_name);
    }

    /* Create new entry in target directory */
    int32_t ret = fat32_write_dir_entry(new_dir_cluster, new_name,
                                         entry_cluster, entry.file_size, entry.attrs);
    if (ret != 0) {
        mutex_unlock(&fat32_lock);
        return -1;
    }

    /* Remove old entry */
    fat32_remove_dir_entry(old_dir_cluster, old_name);

    mutex_unlock(&fat32_lock);
    return 0;
}

file_ops_t fat32_file_ops = {
    .open = fat32_file_open,
    .read = fat32_file_read,
    .write = fat32_file_write,
    .close = fat32_file_close,
    .seek = fat32_file_seek,
    .ioctl = NULL
};

int32_t fat32_mount(superblock_t *sb, void *data) {
    fat32_info_t *mount_data = (fat32_info_t *)data;

    if (fat32_init(0, mount_data ? mount_data->fs_sector_start : 0) != 0) return -1;

    sb->fs_type = FS_TYPE_FAT32;
    sb->block_size = fs_info.bytes_per_sector;
    sb->total_blocks = fs_info.total_clusters * fs_info.sectors_per_cluster;
    sb->fs_data = &fs_info;

    inode_t *root_inode = (inode_t *)kmalloc(sizeof(inode_t));
    if (!root_inode) return -1;
    memset(root_inode, 0, sizeof(inode_t));
    root_inode->ino = fs_info.root_cluster;
    root_inode->mode = FILE_MODE_DIR | FILE_MODE_READ | FILE_MODE_WRITE;
    root_inode->nlinks = 1;
    root_inode->size = 0;
    root_inode->sb = sb;
    root_inode->ops = &fat32_inode_ops;
    root_inode->private_data = (void *)(uintptr_t)fs_info.root_cluster;

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
