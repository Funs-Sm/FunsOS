#include "quota.h"
#include "kheap.h"
#include "string.h"
#include "sync.h"
#include "klog.h"

#define QUOTA_MAX_ENTRIES 128

static quota_entry_t quota_table[QUOTA_MAX_ENTRIES];
static uint32_t quota_count = 0;
static spinlock_t quota_lock;
static int quota_enabled = 0;
static uint32_t default_block_grace = QUOTA_DEFAULT_BTIME;
static uint32_t default_inode_grace = QUOTA_DEFAULT_ITIME;

/* ---- 内部辅助函数 ---- */

static quota_entry_t *quota_find_internal(uint32_t id, uint8_t type) {
    for (uint32_t i = 0; i < quota_count; i++) {
        if (quota_table[i].id == id && quota_table[i].type == type) {
            return &quota_table[i];
        }
    }
    return NULL;
}

static quota_entry_t *quota_find_or_create(uint32_t id, uint8_t type) {
    quota_entry_t *entry = quota_find_internal(id, type);
    if (entry) return entry;

    if (quota_count >= QUOTA_MAX_ENTRIES) return NULL;

    entry = &quota_table[quota_count];
    memset(entry, 0, sizeof(quota_entry_t));
    entry->id = id;
    entry->type = type;
    entry->flags = 0;
    quota_count++;
    return entry;
}

static int quota_remove_internal(uint32_t id, uint8_t type) {
    for (uint32_t i = 0; i < quota_count; i++) {
        if (quota_table[i].id == id && quota_table[i].type == type) {
            /* 用最后一个元素填补空缺 */
            if (i < quota_count - 1) {
                memcpy(&quota_table[i], &quota_table[quota_count - 1],
                       sizeof(quota_entry_t));
            }
            memset(&quota_table[quota_count - 1], 0, sizeof(quota_entry_t));
            quota_count--;
            return 0;
        }
    }
    return -2; /* ENOENT */
}

/* ---- 初始化与控制 ---- */

void quota_init(void) {
    memset(quota_table, 0, sizeof(quota_table));
    quota_count = 0;
    spinlock_init(&quota_lock);
    quota_enabled = 0;
    default_block_grace = QUOTA_DEFAULT_BTIME;
    default_inode_grace = QUOTA_DEFAULT_ITIME;
    klog_info("quota: subsystem initialized (max %d entries)", QUOTA_MAX_ENTRIES);
}

int quota_enable(int enable) {
    spinlock_lock(&quota_lock);
    quota_enabled = enable ? 1 : 0;
    spinlock_unlock(&quota_lock);
    klog_info("quota: %s", enable ? "enabled" : "disabled");
    return 0;
}

int quota_is_enabled(void) {
    return quota_enabled;
}

int quota_set_grace(uint32_t block_grace, uint32_t inode_grace) {
    spinlock_lock(&quota_lock);
    default_block_grace = block_grace;
    default_inode_grace = inode_grace;
    spinlock_unlock(&quota_lock);
    return 0;
}

/* ---- 配额检查 ---- */

static int check_limit(uint64_t used, uint64_t add,
                       uint64_t soft, uint64_t hard,
                       uint8_t *flags, uint8_t soft_flag, uint8_t hard_flag) {
    if (hard == 0) return 0; /* 无限制 */

    uint64_t new_total = used + add;

    if (new_total > hard) {
        *flags |= hard_flag;
        return 2; /* 超过硬限制 */
    }

    if (new_total > soft && soft > 0) {
        *flags |= soft_flag;
        return 1; /* 超过软限制 */
    }

    *flags &= ~(soft_flag | hard_flag);
    return 0; /* 正常 */
}

int quota_check_block(uint32_t uid, uint32_t gid, uint64_t blocks) {
    if (!quota_enabled) return 0;

    spinlock_lock(&quota_lock);

    int user_result = 0;
    int group_result = 0;

    /* 检查用户配额 */
    quota_entry_t *uentry = quota_find_internal(uid, QUOTA_TYPE_USER);
    if (uentry && uentry->blocks_hard > 0) {
        user_result = check_limit(uentry->blocks_used, blocks,
                                   uentry->blocks_soft, uentry->blocks_hard,
                                   &uentry->flags,
                                   QUOTA_FLAG_BLOCK_SOFT, QUOTA_FLAG_BLOCK_HARD);
    }

    /* 检查组配额 */
    quota_entry_t *gentry = quota_find_internal(gid, QUOTA_TYPE_GROUP);
    if (gentry && gentry->blocks_hard > 0) {
        group_result = check_limit(gentry->blocks_used, blocks,
                                    gentry->blocks_soft, gentry->blocks_hard,
                                    &gentry->flags,
                                    QUOTA_FLAG_BLOCK_SOFT, QUOTA_FLAG_BLOCK_HARD);
    }

    spinlock_unlock(&quota_lock);

    /* 返回较严格的结果 */
    return user_result > group_result ? user_result : group_result;
}

int quota_check_inode(uint32_t uid, uint32_t gid, uint64_t inodes) {
    if (!quota_enabled) return 0;

    spinlock_lock(&quota_lock);

    int user_result = 0;
    int group_result = 0;

    quota_entry_t *uentry = quota_find_internal(uid, QUOTA_TYPE_USER);
    if (uentry && uentry->inodes_hard > 0) {
        user_result = check_limit(uentry->inodes_used, inodes,
                                   uentry->inodes_soft, uentry->inodes_hard,
                                   &uentry->flags,
                                   QUOTA_FLAG_INODE_SOFT, QUOTA_FLAG_INODE_HARD);
    }

    quota_entry_t *gentry = quota_find_internal(gid, QUOTA_TYPE_GROUP);
    if (gentry && gentry->inodes_hard > 0) {
        group_result = check_limit(gentry->inodes_used, inodes,
                                    gentry->inodes_soft, gentry->inodes_hard,
                                    &gentry->flags,
                                    QUOTA_FLAG_INODE_SOFT, QUOTA_FLAG_INODE_HARD);
    }

    spinlock_unlock(&quota_lock);

    return user_result > group_result ? user_result : group_result;
}

/* ---- 使用量更新 ---- */

void quota_add_block(uint32_t uid, uint32_t gid, uint64_t count) {
    if (!quota_enabled) return;

    spinlock_lock(&quota_lock);

    quota_entry_t *uentry = quota_find_internal(uid, QUOTA_TYPE_USER);
    if (uentry) {
        uentry->blocks_used += count;
    }

    quota_entry_t *gentry = quota_find_internal(gid, QUOTA_TYPE_GROUP);
    if (gentry) {
        gentry->blocks_used += count;
    }

    spinlock_unlock(&quota_lock);
}

void quota_sub_block(uint32_t uid, uint32_t gid, uint64_t count) {
    if (!quota_enabled) return;

    spinlock_lock(&quota_lock);

    quota_entry_t *uentry = quota_find_internal(uid, QUOTA_TYPE_USER);
    if (uentry && uentry->blocks_used >= count) {
        uentry->blocks_used -= count;
        /* 检查是否回到软限制以下 */
        if (uentry->blocks_used <= uentry->blocks_soft) {
            uentry->flags &= ~QUOTA_FLAG_BLOCK_SOFT;
        }
    }

    quota_entry_t *gentry = quota_find_internal(gid, QUOTA_TYPE_GROUP);
    if (gentry && gentry->blocks_used >= count) {
        gentry->blocks_used -= count;
        if (gentry->blocks_used <= gentry->blocks_soft) {
            gentry->flags &= ~QUOTA_FLAG_BLOCK_SOFT;
        }
    }

    spinlock_unlock(&quota_lock);
}

void quota_add_inode(uint32_t uid, uint32_t gid, uint64_t count) {
    if (!quota_enabled) return;

    spinlock_lock(&quota_lock);

    quota_entry_t *uentry = quota_find_internal(uid, QUOTA_TYPE_USER);
    if (uentry) {
        uentry->inodes_used += count;
    }

    quota_entry_t *gentry = quota_find_internal(gid, QUOTA_TYPE_GROUP);
    if (gentry) {
        gentry->inodes_used += count;
    }

    spinlock_unlock(&quota_lock);
}

void quota_sub_inode(uint32_t uid, uint32_t gid, uint64_t count) {
    if (!quota_enabled) return;

    spinlock_lock(&quota_lock);

    quota_entry_t *uentry = quota_find_internal(uid, QUOTA_TYPE_USER);
    if (uentry && uentry->inodes_used >= count) {
        uentry->inodes_used -= count;
        if (uentry->inodes_used <= uentry->inodes_soft) {
            uentry->flags &= ~QUOTA_FLAG_INODE_SOFT;
        }
    }

    quota_entry_t *gentry = quota_find_internal(gid, QUOTA_TYPE_GROUP);
    if (gentry && gentry->inodes_used >= count) {
        gentry->inodes_used -= count;
        if (gentry->inodes_used <= gentry->inodes_soft) {
            gentry->flags &= ~QUOTA_FLAG_INODE_SOFT;
        }
    }

    spinlock_unlock(&quota_lock);
}

/* ---- 配额管理 ---- */

int quota_set_user(uint32_t uid, uint64_t bsoft, uint64_t bhard,
                   uint64_t isoft, uint64_t ihard) {
    spinlock_lock(&quota_lock);

    quota_entry_t *entry = quota_find_or_create(uid, QUOTA_TYPE_USER);
    if (!entry) {
        spinlock_unlock(&quota_lock);
        return -12; /* ENOMEM */
    }

    entry->blocks_soft = bsoft;
    entry->blocks_hard = bhard;
    entry->inodes_soft = isoft;
    entry->inodes_hard = ihard;
    entry->flags |= QUOTA_FLAG_ENABLED;

    spinlock_unlock(&quota_lock);
    return 0;
}

int quota_set_group(uint32_t gid, uint64_t bsoft, uint64_t bhard,
                    uint64_t isoft, uint64_t ihard) {
    spinlock_lock(&quota_lock);

    quota_entry_t *entry = quota_find_or_create(gid, QUOTA_TYPE_GROUP);
    if (!entry) {
        spinlock_unlock(&quota_lock);
        return -12;
    }

    entry->blocks_soft = bsoft;
    entry->blocks_hard = bhard;
    entry->inodes_soft = isoft;
    entry->inodes_hard = ihard;
    entry->flags |= QUOTA_FLAG_ENABLED;

    spinlock_unlock(&quota_lock);
    return 0;
}

quota_entry_t *quota_get_user(uint32_t uid) {
    /* 注意：返回指针在锁外使用有风险，这里只用于快速查询 */
    spinlock_lock(&quota_lock);
    quota_entry_t *entry = quota_find_internal(uid, QUOTA_TYPE_USER);
    spinlock_unlock(&quota_lock);
    return entry;
}

quota_entry_t *quota_get_group(uint32_t gid) {
    spinlock_lock(&quota_lock);
    quota_entry_t *entry = quota_find_internal(gid, QUOTA_TYPE_GROUP);
    spinlock_unlock(&quota_lock);
    return entry;
}

int quota_remove_user(uint32_t uid) {
    spinlock_lock(&quota_lock);
    int ret = quota_remove_internal(uid, QUOTA_TYPE_USER);
    spinlock_unlock(&quota_lock);
    return ret;
}

int quota_remove_group(uint32_t gid) {
    spinlock_lock(&quota_lock);
    int ret = quota_remove_internal(gid, QUOTA_TYPE_GROUP);
    spinlock_unlock(&quota_lock);
    return ret;
}

int quota_reset_user(uint32_t uid) {
    spinlock_lock(&quota_lock);
    quota_entry_t *entry = quota_find_internal(uid, QUOTA_TYPE_USER);
    if (!entry) {
        spinlock_unlock(&quota_lock);
        return -2;
    }
    entry->blocks_used = 0;
    entry->inodes_used = 0;
    entry->flags &= ~(QUOTA_FLAG_BLOCK_SOFT | QUOTA_FLAG_BLOCK_HARD |
                      QUOTA_FLAG_INODE_SOFT | QUOTA_FLAG_INODE_HARD);
    spinlock_unlock(&quota_lock);
    return 0;
}

int quota_reset_group(uint32_t gid) {
    spinlock_lock(&quota_lock);
    quota_entry_t *entry = quota_find_internal(gid, QUOTA_TYPE_GROUP);
    if (!entry) {
        spinlock_unlock(&quota_lock);
        return -2;
    }
    entry->blocks_used = 0;
    entry->inodes_used = 0;
    entry->flags &= ~(QUOTA_FLAG_BLOCK_SOFT | QUOTA_FLAG_BLOCK_HARD |
                      QUOTA_FLAG_INODE_SOFT | QUOTA_FLAG_INODE_HARD);
    spinlock_unlock(&quota_lock);
    return 0;
}

/* ---- 列表与统计 ---- */

int quota_get_stats(quota_stats_t *stats) {
    if (!stats) return -22; /* EINVAL */

    spinlock_lock(&quota_lock);

    memset(stats, 0, sizeof(quota_stats_t));
    stats->enabled = quota_enabled;
    stats->grace_btime = default_block_grace;
    stats->grace_itime = default_inode_grace;

    for (uint32_t i = 0; i < quota_count; i++) {
        if (quota_table[i].type == QUOTA_TYPE_USER) {
            stats->user_entries++;
        } else if (quota_table[i].type == QUOTA_TYPE_GROUP) {
            stats->group_entries++;
        }
        stats->total_blocks += quota_table[i].blocks_used;
        stats->total_inodes += quota_table[i].inodes_used;
    }

    spinlock_unlock(&quota_lock);
    return 0;
}

int quota_list(quota_entry_t *entries, uint32_t max_entries, uint8_t type) {
    if (!entries || type >= QUOTA_TYPE_MAX) return -22;

    spinlock_lock(&quota_lock);

    uint32_t count = 0;
    for (uint32_t i = 0; i < quota_count && count < max_entries; i++) {
        if (quota_table[i].type == type) {
            memcpy(&entries[count], &quota_table[i], sizeof(quota_entry_t));
            count++;
        }
    }

    spinlock_unlock(&quota_lock);
    return (int)count;
}

int quota_sync(void) {
    /* 预留：未来用于将配额信息持久化到磁盘 */
    return 0;
}
