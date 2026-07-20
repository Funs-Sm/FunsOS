/* fim.h - 文件完整性监视子系统
 *
 * 监视指定文件/目录的元数据变化（大小、修改时间、权限、链接数）。
 *   - 监视列表与基线存储在 FunDB（/var/db/fim.db）
 *   - fim_scan() 对比当前与基线，检测新增/修改/删除
 *   - 变更事件写入 evlog（source="FileMon"）
 *   - fim_baseline() 以当前状态重置基线
 *
 * 典型用法：
 *   fim_add("/etc/init.rc");
 *   fim_add("/system/shell.bin");
 *   fim_scan();              // 检测变化
 *   fim_baseline();          // 接受当前状态为新基线
 */
#ifndef FIM_H
#define FIM_H

#include "stdint.h"
#include "stddef.h"

#define FIM_MAX_PATH       256
#define FIM_MAX_ENTRIES    128
#define FIM_DB_PATH        "/var/db/fim.db"
#define FIM_TABLE_WATCH    "fim_watch"
#define FIM_TABLE_BASELINE "fim_baseline"

/* 文件状态 */
typedef enum {
    FIM_STATE_NEW = 0,       /* 新增：当前存在但基线中没有 */
    FIM_STATE_UNCHANGED,     /* 未变化 */
    FIM_STATE_MODIFIED,      /* 已修改：size/mtime 变化 */
    FIM_STATE_DELETED,       /* 已删除：基线中有但当前不存在 */
    FIM_STATE_ERROR,         /* stat 失败 */
} fim_state_t;

/* 监视条目（用于 fim_list 返回） */
typedef struct fim_entry {
    char        path[FIM_MAX_PATH];
    uint32_t    baseline_size;
    uint32_t    baseline_mtime;
    uint32_t    baseline_mode;
    uint32_t    current_size;
    uint32_t    current_mtime;
    uint32_t    current_mode;
    fim_state_t last_state;
} fim_entry_t;

/* 统计 */
typedef struct {
    uint32_t watched;        /* 监视的文件数 */
    uint32_t unchanged;
    uint32_t modified;
    uint32_t added;
    uint32_t deleted;
    uint32_t errors;
    uint32_t last_scan_tick;
    uint32_t total_scans;
} fim_stats_t;

/* ---- 初始化 ---- */
void fim_init(void);
void fim_shutdown(void);

/* ---- 监视列表管理 ---- */
int  fim_add(const char *path);
int  fim_remove(const char *path);
int  fim_clear(void);
uint32_t fim_count(void);
int  fim_list(fim_entry_t *out, uint32_t max_count);

/* ---- 扫描与基线 ---- */
/* 扫描所有监视文件，检测变化并写入 evlog */
void fim_scan(void);
/* 以当前文件状态重置基线（接受所有当前状态为正常） */
void fim_baseline(void);

/* ---- 统计 ---- */
void fim_get_stats(fim_stats_t *stats);
void fim_reset_stats(void);

#endif /* FIM_H */
