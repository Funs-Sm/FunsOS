#ifndef ICACHE_H
#define ICACHE_H

#include "stdint.h"
#include "vfs.h"

/* ============================================================
 * Inode Cache (icache) with LRU Management
 * inode 缓存 - LRU 管理
 *
 * 功能：
 * - 维护 inode LRU 链表
 * - 按 (dev, ino) 哈希查找
 * - 缓存命中统计
 * - 自动回收最久未使用的 inode
 * - inode 引用计数管理
 * ============================================================ */

#define ICACHE_MAX_ENTRIES   512  /* 最大缓存条目数 */
#define ICACHE_HASH_BUCKETS  64   /* 哈希桶数量 */
#define ICACHE_RECLAIM_BATCH  16  /* 每次回收数量 */

/* icache 统计信息 */
typedef struct icache_stats {
    uint32_t total_entries;      /* 当前缓存条目数 */
    uint32_t max_entries;        /* 历史最大条目数 */
    uint64_t lookups;            /* 查找次数 */
    uint64_t hits;               /* 缓存命中次数 */
    uint64_t misses;             /* 缓存未命中次数 */
    uint64_t reclaims;           /* 回收次数 */
    uint64_t invalidations;      /* 失效次数 */
} icache_stats_t;

/* ---- 初始化 ---- */
void icache_init(void);

/* ---- 查找 ---- */

/* 通过设备号和 inode 号查找缓存
 * 找到返回 inode 指针（增加引用计数），没找到返回 NULL */
inode_t *icache_lookup(uint32_t dev, uint32_t ino);

/* ---- 添加/删除 ---- */

/* 将 inode 添加到缓存（移动到 LRU 头部）*/
void icache_add(uint32_t dev, uint32_t ino, inode_t *inode);

/* 从缓存中移除 inode */
void icache_remove(uint32_t dev, uint32_t ino);

/* ---- 引用计数 ---- */

/* 增加引用计数（移动到 LRU 头部）*/
void icache_get(inode_t *inode);

/* 减少引用计数 */
void icache_put(inode_t *inode);

/* ---- 回收 ---- */

/* 回收最久未使用的 inode（未被引用的）
 * 返回回收的数量 */
uint32_t icache_reclaim(uint32_t count);

/* 回收指定设备的所有 inode */
uint32_t icache_reclaim_dev(uint32_t dev);

/* ---- 统计 ---- */

/* 获取统计信息 */
void icache_get_stats(icache_stats_t *stats);

/* 重置统计信息 */
void icache_reset_stats(void);

#endif /* ICACHE_H */
