#ifndef READAHEAD_H
#define READAHEAD_H

#include "stdint.h"
#include "vfs.h"

/* ============================================================
 * File Readahead - 文件预读机制
 *
 * 功能：
 * - 顺序读取检测
 * - 预读窗口管理
 * - 预读触发与异步预读（简化为同步）
 * - 预读统计
 * ============================================================ */

#define READAHEAD_MIN_PAGES   2   /* 最小预读页数 */
#define READAHEAD_MAX_PAGES  32   /* 最大预读页数 */
#define READAHEAD_DEF_PAGES   4   /* 默认预读页数 */

/* 预读统计 */
typedef struct readahead_stats {
    uint64_t readahead_calls;    /* 预读触发次数 */
    uint64_t pages_readahead;    /* 预读总页数 */
    uint64_t seq_detections;     /* 顺序读取检测次数 */
    uint64_t cache_hits;         /* 预读缓存命中次数 */
} readahead_stats_t;

/* ---- 初始化 ---- */
void readahead_init(void);

/* ---- 核心 API ---- */

/* 触发预读
 * 在读取当前页时调用，预读后续页面
 * 返回预读的页数 */
uint32_t readahead_trigger(inode_t *inode, uint32_t current_page);

/* 记录一次读取（用于顺序性检测）*/
void readahead_record_read(inode_t *inode, uint32_t page_index);

/* ---- 统计 ---- */
void readahead_get_stats(readahead_stats_t *stats);
void readahead_reset_stats(void);

#endif /* READAHEAD_H */
