#ifndef QUOTA_H
#define QUOTA_H

#include "stdint.h"

/* ============================================================
 * Quota Subsystem - 文件系统配额
 *
 * 支持：
 * - 用户配额 (user quota) 和组配额 (group quota)
 * - 块配额 (blocks) 和 inode 配额
 * - 软限制 (soft limit) 和硬限制 (hard limit)
 * - 宽限期 (grace period)
 * - 配额启用/禁用控制
 * ============================================================ */

#define QUOTA_FLAG_ENABLED     0x01  /* 配额已启用 */
#define QUOTA_FLAG_BLOCK_SOFT  0x02  /* 块软限制已超限 */
#define QUOTA_FLAG_BLOCK_HARD  0x04  /* 块硬限制已超限 */
#define QUOTA_FLAG_INODE_SOFT  0x08  /* inode 软限制已超限 */
#define QUOTA_FLAG_INODE_HARD  0x10  /* inode 硬限制已超限 */

/* 配额类型 */
#define QUOTA_TYPE_USER  0
#define QUOTA_TYPE_GROUP 1
#define QUOTA_TYPE_MAX   2

/* 默认宽限期（秒） */
#define QUOTA_DEFAULT_BTIME  (7 * 24 * 60 * 60)  /* 7 天 */
#define QUOTA_DEFAULT_ITIME  (7 * 24 * 60 * 60)  /* 7 天 */

/* 配额条目 */
typedef struct quota_entry {
    uint32_t id;           /* uid 或 gid */
    uint8_t  type;         /* QUOTA_TYPE_USER 或 QUOTA_TYPE_GROUP */
    uint8_t  flags;        /* 状态标志 */
    uint64_t blocks_used;  /* 已使用块数 */
    uint64_t blocks_soft;  /* 块软限制 */
    uint64_t blocks_hard;  /* 块硬限制 */
    uint64_t inodes_used;  /* 已使用 inode 数 */
    uint64_t inodes_soft;  /* inode 软限制 */
    uint64_t inodes_hard;  /* inode 硬限制 */
    uint32_t btime;        /* 块宽限期到期时间 */
    uint32_t itime;        /* inode 宽限期到期时间 */
} quota_entry_t;

/* 配额统计信息 */
typedef struct quota_stats {
    uint32_t user_entries;    /* 用户配额条目数 */
    uint32_t group_entries;   /* 组配额条目数 */
    uint64_t total_blocks;    /* 总已分配块数 */
    uint64_t total_inodes;    /* 总已分配 inode 数 */
    uint32_t enabled;         /* 配额是否启用 */
    uint32_t grace_btime;     /* 默认块宽限期 */
    uint32_t grace_itime;     /* 默认 inode 宽限期 */
} quota_stats_t;

/* ---- 初始化与控制 ---- */

/* 初始化配额子系统 */
void quota_init(void);

/* 启用/禁用配额 */
int quota_enable(int enable);

/* 检查配额是否启用 */
int quota_is_enabled(void);

/* 设置默认宽限期 */
int quota_set_grace(uint32_t block_grace, uint32_t inode_grace);

/* ---- 配额检查 ---- */

/* 检查块配额：
 * 返回 0 = 正常
 * 返回 1 = 超过软限制
 * 返回 2 = 超过硬限制（不允许分配）
 * 返回负值 = 错误 */
int quota_check_block(uint32_t uid, uint32_t gid, uint64_t blocks);

/* 检查 inode 配额 */
int quota_check_inode(uint32_t uid, uint32_t gid, uint64_t inodes);

/* ---- 使用量更新 ---- */

/* 增加块使用量 */
void quota_add_block(uint32_t uid, uint32_t gid, uint64_t count);

/* 减少块使用量 */
void quota_sub_block(uint32_t uid, uint32_t gid, uint64_t count);

/* 增加 inode 使用量 */
void quota_add_inode(uint32_t uid, uint32_t gid, uint64_t count);

/* 减少 inode 使用量 */
void quota_sub_inode(uint32_t uid, uint32_t gid, uint64_t count);

/* ---- 配额管理 ---- */

/* 设置用户配额 */
int quota_set_user(uint32_t uid, uint64_t bsoft, uint64_t bhard,
                   uint64_t isoft, uint64_t ihard);

/* 设置组配额 */
int quota_set_group(uint32_t gid, uint64_t bsoft, uint64_t bhard,
                    uint64_t isoft, uint64_t ihard);

/* 获取用户配额 */
quota_entry_t *quota_get_user(uint32_t uid);

/* 获取组配额 */
quota_entry_t *quota_get_group(uint32_t gid);

/* 删除用户配额 */
int quota_remove_user(uint32_t uid);

/* 删除组配额 */
int quota_remove_group(uint32_t gid);

/* 重置用户使用量 */
int quota_reset_user(uint32_t uid);

/* 重置组使用量 */
int quota_reset_group(uint32_t gid);

/* ---- 列表与统计 ---- */

/* 获取配额统计 */
int quota_get_stats(quota_stats_t *stats);

/* 列出所有配额条目
 * entries: 输出缓冲区
 * max_entries: 缓冲区大小
 * type: QUOTA_TYPE_USER 或 QUOTA_TYPE_GROUP
 * 返回: 实际条目数 */
int quota_list(quota_entry_t *entries, uint32_t max_entries, uint8_t type);

/* 同步配额信息（未来用于持久化） */
int quota_sync(void);

#endif /* QUOTA_H */
