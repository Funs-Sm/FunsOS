#ifndef PAGE_CACHE_H
#define PAGE_CACHE_H

#include "stdint.h"
#include "vfs.h"

/* ============================================================
 * Page Cache - 页缓存
 *
 * 功能：
 * - 缓存文件数据页（以页为单位，通常 4KB）
 * - 按 (inode, page_index) 哈希查找
 * - LRU 回收策略
 * - 脏页跟踪（需要写回的页）
 * - 预读支持
 * - 统计信息
 * ============================================================ */

#define PAGE_CACHE_SIZE       4096   /* 页大小 */
#define PAGE_CACHE_MAX_PAGES   256   /* 最大缓存页数 */
#define PAGE_CACHE_HASH_BUCKETS 64   /* 哈希桶数量 */
#define PAGE_CACHE_RECLAIM_BATCH 8   /* 每次回收数量 */

/* 页标志 */
#define PC_PAGE_DIRTY    0x01  /* 脏页（需要写回）*/
#define PC_PAGE_LOCKED   0x02  /* 页被锁定（IO 中）*/
#define PC_PAGE_UPTODATE 0x04  /* 页内容有效（已从磁盘读取）*/
#define PC_PAGE_READAHEAD 0x08  /* 预读页 */

/* 页缓存统计信息 */
typedef struct page_cache_stats {
    uint32_t total_pages;        /* 当前缓存页数 */
    uint32_t max_pages;          /* 历史最大页数 */
    uint32_t dirty_pages;        /* 脏页数 */
    uint64_t lookups;            /* 查找次数 */
    uint64_t hits;               /* 命中次数 */
    uint64_t misses;             /* 未命中次数 */
    uint64_t reclaims;           /* 回收次数 */
    uint64_t writebacks;         /* 写回次数 */
    uint64_t reads;              /* 读取页数 */
    uint64_t writes;             /* 写入页数 */
} page_cache_stats_t;

/* ---- 初始化 ---- */
void page_cache_init(void);

/* ---- 查找/获取 ---- */

/* 查找页缓存（命中返回页数据指针，不增加引用）
 * 返回 NULL 表示未命中 */
void *page_cache_lookup(inode_t *inode, uint32_t page_index);

/* 获取页（命中返回页数据并标记为已使用）
 * 如果不存在，返回 NULL，调用者需负责读取并添加 */
void *page_cache_get_page(inode_t *inode, uint32_t page_index, int *is_new);

/* ---- 添加/更新 ---- */

/* 添加页到缓存
 * 数据从 data 拷贝（如果 data 非空）*/
void *page_cache_add_page(inode_t *inode, uint32_t page_index, const void *data);

/* 释放页引用 */
void page_cache_put_page(inode_t *inode, uint32_t page_index);

/* ---- 脏页管理 ---- */

/* 标记页为脏 */
void page_cache_mark_dirty(inode_t *inode, uint32_t page_index);

/* 标记页为干净 */
void page_cache_mark_clean(inode_t *inode, uint32_t page_index);

/* 检查页是否脏 */
int page_cache_is_dirty(inode_t *inode, uint32_t page_index);

/* 写回指定 inode 的所有脏页 */
uint32_t page_cache_writeback_inode(inode_t *inode);

/* 写回所有脏页 */
uint32_t page_cache_writeback_all(void);

/* ---- 失效 ---- */

/* 使指定 inode 的所有缓存页失效 */
uint32_t page_cache_invalidate_inode(inode_t *inode);

/* 使指定范围的页失效 */
uint32_t page_cache_invalidate_range(inode_t *inode,
                                     uint32_t start_page,
                                     uint32_t end_page);

/* ---- 回收 ---- */

/* 回收 n 个页（只回收未引用的干净页）
 * 返回实际回收数量 */
uint32_t page_cache_reclaim(uint32_t count);

/* ---- 统计 ---- */

void page_cache_get_stats(page_cache_stats_t *stats);
void page_cache_reset_stats(void);

#endif /* PAGE_CACHE_H */
