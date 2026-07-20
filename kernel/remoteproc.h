#ifndef REMOTEPROC_H
#define REMOTEPROC_H

#include "stdint.h"

/* ============================================================
 * Remoteproc 远程处理器框架
 *
 * 管理系统中的远程处理器（协处理器），支持固件加载、
 * 启动、关闭和崩溃恢复。
 * ============================================================ */

#define REMOTEPROC_MAX           16
#define REMOTEPROC_NAME_MAX      64
#define REMOTEPROC_FW_MAX       128

struct rproc;

typedef enum {
    RPROC_OFFLINE = 0,
    RPROC_SUSPENDED,
    RPROC_RUNNING,
    RPROC_CRASHED,
    RPROC_DELETED,
    RPROC_ATTACHED,
} rproc_state_t;

typedef void (*rproc_fw_cb_t)(struct rproc *rproc, void *data);
typedef int (*rproc_start_func_t)(struct rproc *rproc);
typedef int (*rproc_stop_func_t)(struct rproc *rproc);

typedef struct rproc {
    int                  id;
    int                  used;
    char                 name[REMOTEPROC_NAME_MAX];
    rproc_state_t        state;
    char                 firmware[REMOTEPROC_FW_MAX];
    uint32_t             boot_addr;
    uint32_t             load_addr;
    uint32_t             mem_base;
    uint32_t             mem_size;
    rproc_start_func_t   start;
    rproc_stop_func_t    stop;
    uint32_t             refcnt;
    uint64_t             boot_count;
    uint64_t             crash_count;
    uint64_t             uptime;
    uint64_t             last_boot;
    uint64_t             last_crash;
    uint32_t             trace_buf;
} rproc_t;

/* ============================================================
 * 初始化
 * ============================================================ */
int remoteproc_init(void);

/* ============================================================
 * 核心 API
 * ============================================================ */

rproc_t *rproc_alloc(const char *name, const char *firmware);
int rproc_add(rproc_t *rproc);
int rproc_boot(rproc_t *rproc);
int rproc_shutdown(rproc_t *rproc);
int rproc_attach(rproc_t *rproc);
int rproc_detach(rproc_t *rproc);
rproc_t *rproc_find_by_name(const char *name);
rproc_t *rproc_get_by_id(int id);
void rproc_get(rproc_t *rproc);
void rproc_put(rproc_t *rproc);

/* ============================================================
 * 统计与调试
 * ============================================================ */
void remoteproc_print_stats(void);

#endif /* REMOTEPROC_H */
