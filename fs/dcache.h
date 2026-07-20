#ifndef DCACHE_H
#define DCACHE_H

#include "stdint.h"
#include "vfs.h"

/* ============================================================
 * Directory Entry Cache (dcache) with LRU Management
 * 目录项缓存 - LRU 管理
 *
 * 功能：
 * - 维护 dentry LRU 链表
 * - 缓存命中统计
 * - 自动回收最久未使用的 dentry
 * - dentry 引用计数管理
 * ============================================================ */

#define DCACHE_MAX_ENTRIES  1024  /* 最大缓存条目数 */
#define DCACHE_RECLAIM_BATCH  16  /* 每次回收数量 */

/* dcache 统计信息 */
typedef struct dcache_stats {
    uint32_t total_entries;      /* 当前缓存条目数 */
    uint32_t max_entries;        /* 历史最大条目数 */
    uint64_t lookups;            /* 查找次数 */
    uint64_t hits;               /* 缓存命中次数 */
    uint64_t misses;             /* 缓存未命中次数 */
    uint64_t reclaims;           /* 回收次数 */
    uint64_t invalidations;      /* 失效次数 */
} dcache_stats_t;

/* ---- 初始化 ---- */
void dcache_init(void);

/* ---- 查找 ---- */

/* 通过父 dentry 和名称查找缓存
 * 找到返回 dentry 指针（增加引用计数），没找到返回 NULL */
dentry_t *dcache_lookup(dentry_t *parent, const char *name);

/* ---- 添加/删除 ---- */

/* 将 dentry 添加到缓存（移动到 LRU 头部）*/
void dcache_add(dentry_t *dentry);

/* 从缓存中移除 dentry */
void dcache_remove(dentry_t *dentry);

/* ---- 引用计数 ---- */

/* 增加引用计数（移动到 LRU 头部）*/
void dcache_get(dentry_t *dentry);

/* 减少引用计数 */
void dcache_put(dentry_t *dentry);

/* ---- 回收 ---- */

/* 回收最久未使用的 dentry（未被引用的）
 * 返回回收的数量 */
uint32_t dcache_reclaim(uint32_t count);

/* ---- 统计 ---- */

/* 获取统计信息 */
void dcache_get_stats(dcache_stats_t *stats);

/* 重置统计信息 */
void dcache_reset_stats(void);

/* 打印 dcache 状态（调试用）*/
void dcache_dump(void);

#endif /* DCACHE_H */
