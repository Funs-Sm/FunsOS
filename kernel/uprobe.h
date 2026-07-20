#ifndef UPROBE_H
#define UPROBE_H

#include "stdint.h"

/* ============================================================
 * Uprobes 用户空间探针子系统
 *
 * 提供用户空间动态追踪能力，可以在用户进程的任意地址
 * 插入断点，执行预定义的处理函数。
 * ============================================================ */

#define UPROBE_MAX_PROBES       64
#define UPROBE_MAX_CONSUMERS     8
#define UPROBE_PATH_MAX        256

struct uprobe;
struct inode;
struct pt_regs;

typedef void (*uprobe_handler_t)(struct uprobe *u, uint32_t ip,
                                 struct pt_regs *regs);
typedef int (*uprobe_filter_t)(struct uprobe *u, uint32_t pid);

typedef struct uprobe_consumer {
    int                  used;
    uprobe_handler_t     handler;
    uprobe_filter_t      filter;
    void                *data;
    uint64_t             hit_count;
} uprobe_consumer_t;

typedef struct uprobe {
    int                  id;
    int                  used;
    int                  state;         /* 0=inactive, 1=active */
    char                 path[UPROBE_PATH_MAX];
    uint32_t             inode_nr;      /* 模拟 inode 编号 */
    uint64_t             offset;        /* 文件内偏移 */
    uint32_t             vaddr;         /* 虚拟地址 */
    uprobe_consumer_t    consumers[UPROBE_MAX_CONSUMERS];
    uint32_t             consumer_count;
    uint64_t             total_hits;
    uint64_t             registered_time;
    uint64_t             last_hit_time;
    uint32_t             refcount;
} uprobe_t;

/* ============================================================
 * 初始化
 * ============================================================ */
int uprobe_init(void);

/* ============================================================
 * 核心 API
 * ============================================================ */

int uprobe_register(const char *path, uint64_t offset, uprobe_handler_t handler);
int uprobe_unregister(const char *path, uint64_t offset, uprobe_handler_t handler);
int uprobe_apply(const char *path, uint64_t offset, int add);
uprobe_t *uprobe_find(const char *path, uint64_t offset);
uprobe_t *uprobe_get(int id);
int uprobe_enable(int id);
int uprobe_disable(int id);

uint32_t uprobe_get_active_count(void);
uint64_t uprobe_get_total_hits(void);

/* ============================================================
 * 统计与调试
 * ============================================================ */
void uprobe_print_stats(void);

#endif /* UPROBE_H */
