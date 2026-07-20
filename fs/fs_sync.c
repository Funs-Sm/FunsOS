#include "fs_sync.h"
#include "spinlock.h"
#include "../kernel/klog.h"
#include <string.h>

/* 全局锁 */
static spinlock_t g_sync_lock;

/* 脏 inode 链表 */
#define MAX_DIRTY_INODES  256

typedef struct dirty_inode {
    inode_t *inode;
    struct dirty_inode *next;
    struct dirty_inode *prev;
} dirty_inode_t;

static dirty_inode_t *g_dirty_list = NULL;
static dirty_inode_t *g_dirty_tail = NULL;
static uint32_t g_dirty_count = 0;

/* 脏 inode 槽位池 */
static dirty_inode_t g_dirty_pool[MAX_DIRTY_INODES];
static uint32_t g_pool_used = 0;

/* 统计信息 */
static fs_sync_stats_t g_stats;

/* 初始化标记 */
static int g_initialized = 0;

/* ============================================================
 * 内部辅助函数
 * ============================================================ */

/* 从池中分配一个 dirty_inode */
static dirty_inode_t *alloc_dirty_entry(void) {
    if (g_pool_used >= MAX_DIRTY_INODES) {
        /* 回收最老的脏 inode 条目 */
        if (g_dirty_list) {
            dirty_inode_t *oldest = g_dirty_list;
            g_dirty_list = oldest->next;
            if (g_dirty_list) {
                g_dirty_list->prev = NULL;
            } else {
                g_dirty_tail = NULL;
            }
            g_dirty_count--;
            oldest->next = NULL;
            oldest->prev = NULL;
            return oldest;
        }
        return NULL;
    }

    dirty_inode_t *entry = &g_dirty_pool[g_pool_used++];
    memset(entry, 0, sizeof(dirty_inode_t));
    return entry;
}

/* 查找 inode 在脏链表中的条目 */
static dirty_inode_t *find_dirty_entry(inode_t *inode) {
    dirty_inode_t *entry = g_dirty_list;
    while (entry) {
        if (entry->inode == inode) {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

/* 从脏链表中移除条目 */
static void remove_dirty_entry(dirty_inode_t *entry) {
    if (entry->prev) {
        entry->prev->next = entry->next;
    } else {
        g_dirty_list = entry->next;
    }

    if (entry->next) {
        entry->next->prev = entry->prev;
    } else {
        g_dirty_tail = entry->prev;
    }

    entry->prev = NULL;
    entry->next = NULL;
    g_dirty_count--;
}

/* 添加到脏链表尾部（最新的在尾部）*/
static void add_dirty_tail(dirty_inode_t *entry) {
    entry->prev = g_dirty_tail;
    entry->next = NULL;

    if (g_dirty_tail) {
        g_dirty_tail->next = entry;
    } else {
        g_dirty_list = entry;
    }

    g_dirty_tail = entry;
    g_dirty_count++;

    if (g_dirty_count > g_stats.max_dirty_inodes) {
        g_stats.max_dirty_inodes = g_dirty_count;
    }
}

/* ============================================================
 * 公共 API
 * ============================================================ */

void fs_sync_init(void) {
    if (g_initialized) return;

    spinlock_init(&g_sync_lock);
    g_dirty_list = NULL;
    g_dirty_tail = NULL;
    g_dirty_count = 0;
    g_pool_used = 0;
    memset(&g_stats, 0, sizeof(g_stats));
    g_initialized = 1;
}

void fs_sync_mark_dirty(inode_t *inode) {
    if (!g_initialized) fs_sync_init();
    if (!inode) return;

    spinlock_lock(&g_sync_lock);

    /* 检查是否已经在脏链表中 */
    dirty_inode_t *entry = find_dirty_entry(inode);
    if (entry) {
        /* 移到尾部（表示最新修改）*/
        remove_dirty_entry(entry);
        add_dirty_tail(entry);
    } else {
        /* 添加新条目 */
        entry = alloc_dirty_entry();
        if (entry) {
            entry->inode = inode;
            add_dirty_tail(entry);
            g_stats.dirty_inodes = g_dirty_count;
        }
    }

    spinlock_unlock(&g_sync_lock);
}

void fs_sync_mark_clean(inode_t *inode) {
    if (!g_initialized) fs_sync_init();
    if (!inode) return;

    spinlock_lock(&g_sync_lock);

    dirty_inode_t *entry = find_dirty_entry(inode);
    if (entry) {
        remove_dirty_entry(entry);
        g_stats.dirty_inodes = g_dirty_count;
    }

    spinlock_unlock(&g_sync_lock);
}

int fs_sync_is_dirty(inode_t *inode) {
    if (!g_initialized) fs_sync_init();
    if (!inode) return 0;

    spinlock_lock(&g_sync_lock);
    int dirty = find_dirty_entry(inode) != NULL;
    spinlock_unlock(&g_sync_lock);

    return dirty;
}

int32_t fs_sync_fsync(file_t *file) {
    if (!file || !file->inode) return -1;

    spinlock_lock(&g_sync_lock);
    g_stats.fsync_calls++;
    spinlock_unlock(&g_sync_lock);

    /* 调用底层 fsync */
    int32_t ret = 0;
    if (file->inode->sb) {
        superblock_t *sb = file->inode->sb;
        switch (sb->fs_type) {
            case FS_TYPE_EXT2: {
                extern int32_t ext2_fsync(uint32_t ino);
                ret = ext2_fsync(file->inode->ino);
                break;
            }
            case FS_TYPE_EXT4: {
                extern int32_t ext4_fsync(uint32_t ino);
                ret = ext4_fsync(file->inode->ino);
                break;
            }
            default:
                ret = 0;
                break;
        }
    }

    if (ret == 0) {
        fs_sync_mark_clean(file->inode);
        spinlock_lock(&g_sync_lock);
        g_stats.inodes_synced++;
        spinlock_unlock(&g_sync_lock);
    }

    return ret;
}

int32_t fs_sync_fdatasync(file_t *file) {
    if (!file || !file->inode) return -1;

    spinlock_lock(&g_sync_lock);
    g_stats.fdatasync_calls++;
    spinlock_unlock(&g_sync_lock);

    /* 对于简单实现，fdatasync 和 fsync 相同
     * 实际系统中 fdatasync 只同步数据，不同步 mtime/ctime 等元数据 */
    return fs_sync_fsync(file);
}

int32_t fs_sync_sb(superblock_t *sb) {
    if (!sb) return -1;

    spinlock_lock(&g_sync_lock);
    g_stats.syncfs_calls++;
    spinlock_unlock(&g_sync_lock);

    spinlock_lock(&g_sync_lock);

    /* 遍历脏 inode 列表，同步属于该 sb 的 */
    dirty_inode_t *entry = g_dirty_list;
    int32_t synced = 0;

    while (entry) {
        dirty_inode_t *next = entry->next;

        if (entry->inode && entry->inode->sb == sb) {
            /* 同步这个 inode */
            spinlock_unlock(&g_sync_lock);

            int32_t ret = 0;
            switch (sb->fs_type) {
                case FS_TYPE_EXT2: {
                    extern int32_t ext2_fsync(uint32_t ino);
                    ret = ext2_fsync(entry->inode->ino);
                    break;
                }
                case FS_TYPE_EXT4: {
                    extern int32_t ext4_fsync(uint32_t ino);
                    ret = ext4_fsync(entry->inode->ino);
                    break;
                }
                default:
                    ret = 0;
                    break;
            }

            spinlock_lock(&g_sync_lock);

            if (ret == 0) {
                remove_dirty_entry(entry);
                synced++;
            }
        }

        entry = next;
    }

    g_stats.inodes_synced += synced;
    g_stats.dirty_inodes = g_dirty_count;

    spinlock_unlock(&g_sync_lock);
    return 0;
}

int32_t fs_sync_all(void) {
    if (!g_initialized) fs_sync_init();

    spinlock_lock(&g_sync_lock);
    g_stats.sync_calls++;
    spinlock_unlock(&g_sync_lock);

    spinlock_lock(&g_sync_lock);

    /* 遍历所有脏 inode 并同步 */
    dirty_inode_t *entry = g_dirty_list;
    int32_t synced = 0;

    while (entry) {
        dirty_inode_t *next = entry->next;

        if (entry->inode && entry->inode->sb) {
            spinlock_unlock(&g_sync_lock);

            int32_t ret = 0;
            superblock_t *sb = entry->inode->sb;
            switch (sb->fs_type) {
                case FS_TYPE_EXT2: {
                    extern int32_t ext2_fsync(uint32_t ino);
                    ret = ext2_fsync(entry->inode->ino);
                    break;
                }
                case FS_TYPE_EXT4: {
                    extern int32_t ext4_fsync(uint32_t ino);
                    ret = ext4_fsync(entry->inode->ino);
                    break;
                }
                default:
                    ret = 0;
                    break;
            }

            spinlock_lock(&g_sync_lock);

            if (ret == 0) {
                remove_dirty_entry(entry);
                synced++;
            }
        }

        entry = next;
    }

    g_stats.inodes_synced += synced;
    g_stats.dirty_inodes = g_dirty_count;

    spinlock_unlock(&g_sync_lock);
    return 0;
}

void fs_sync_get_stats(fs_sync_stats_t *stats) {
    if (!g_initialized) fs_sync_init();
    if (!stats) return;

    spinlock_lock(&g_sync_lock);
    memcpy(stats, &g_stats, sizeof(g_stats));
    stats->dirty_inodes = g_dirty_count;
    spinlock_unlock(&g_sync_lock);
}

void fs_sync_reset_stats(void) {
    if (!g_initialized) fs_sync_init();

    spinlock_lock(&g_sync_lock);
    g_stats.sync_calls = 0;
    g_stats.fsync_calls = 0;
    g_stats.fdatasync_calls = 0;
    g_stats.syncfs_calls = 0;
    g_stats.inodes_synced = 0;
    g_stats.blocks_synced = 0;
    g_stats.max_dirty_inodes = g_dirty_count;
    spinlock_unlock(&g_sync_lock);
}
