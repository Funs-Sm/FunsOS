/* svcmgr.h - 服务管理器 (Service Manager)
 *
 * 管理"常驻服务"的生命周期：
 *   - 注册：name, command, autostart, restart_policy, dependencies
 *   - 状态机：STOPPED -> STARTING -> RUNNING -> STOPPING -> STOPPED
 *                                     \-> FAILED
 *   - 启动/停止历史记录（持久化到 FunDB）
 *   - 自动重启策略（NEVER/ON_FAILURE/ALWAYS）
 *   - 依赖解析（启动前先启动依赖）
 *   - 与 evlog 联动（source="Service"）
 *
 * 与既有模块的区别：
 *   - system_services : 启动期内核模块服务（vfs/desktop/...）
 *   - cron            : 定时任务
 *   - svcmgr          : 运行时常驻服务（可启动/停止/重启）
 */
#ifndef SVCMGR_H
#define SVCMGR_H

#include "stdint.h"

#define SVCMGR_MAX_SERVICES      64
#define SVCMGR_MAX_NAME          32
#define SVCMGR_MAX_DESC          128
#define SVCMGR_MAX_CMD           256
#define SVCMGR_MAX_DEPS          8
#define SVCMGR_MAX_DEP_NAME      32
#define SVCMGR_HISTORY_SIZE      64
#define SVCMGR_DB_PATH           "/var/db/svcmgr.db"
#define SVCMGR_TABLE_SERVICES    "svcmgr_services"
#define SVCMGR_TABLE_HISTORY    "svcmgr_history"

/* 服务状态 */
typedef enum {
    SVCMGR_STATE_STOPPED   = 0,
    SVCMGR_STATE_STARTING  = 1,
    SVCMGR_STATE_RUNNING   = 2,
    SVCMGR_STATE_STOPPING  = 3,
    SVCMGR_STATE_FAILED    = 4,
} svcmgr_state_t;

#define SVCMGR_STATE_COUNT 5

/* 重启策略 */
typedef enum {
    SVCMGR_RESTART_NEVER       = 0,  /* 不重启 */
    SVCMGR_RESTART_ON_FAILURE  = 1,  /* 失败时重启 */
    SVCMGR_RESTART_ALWAYS      = 2,  /* 总是重启（即使正常退出） */
} svcmgr_restart_policy_t;

/* 服务定义 */
typedef struct svcmgr_service {
    char     name[SVCMGR_MAX_NAME];
    char     description[SVCMGR_MAX_DESC];
    char     command[SVCMGR_MAX_CMD];
    uint8_t  state;                /* svcmgr_state_t */
    uint8_t  restart_policy;       /* svcmgr_restart_policy_t */
    uint8_t  autostart;            /* 启动时自动启动 */
    uint8_t  enabled;
    char     dependencies[SVCMGR_MAX_DEPS][SVCMGR_MAX_DEP_NAME];
    uint8_t  dep_count;
    uint32_t start_tick;           /* 上次启动时刻 */
    uint32_t stop_tick;            /* 上次停止时刻 */
    uint32_t restart_count;        /* 累计重启次数 */
    uint32_t failure_count;        /* 累计失败次数 */
    uint32_t run_count;            /* 累计成功启动次数 */
    int32_t  last_exit_code;
} svcmgr_service_t;

/* 历史记录 */
typedef struct svcmgr_history {
    char     service_name[SVCMGR_MAX_NAME];
    uint32_t start_tick;
    uint32_t stop_tick;
    int32_t  exit_code;
    uint8_t  state;                /* 终态：STOPPED 或 FAILED */
} svcmgr_history_t;

/* 统计 */
typedef struct {
    uint32_t total_services;
    uint32_t running;
    uint32_t stopped;
    uint32_t failed;
    uint32_t total_starts;
    uint32_t total_stops;
    uint32_t total_restarts;
    uint32_t total_failures;
} svcmgr_stats_t;

/* ---- 初始化 ---- */
void svcmgr_init(void);
void svcmgr_shutdown(void);

/* ---- 服务注册 ---- */
int svcmgr_register(const char *name, const char *description, const char *command,
                    int autostart, svcmgr_restart_policy_t restart_policy);
int svcmgr_unregister(const char *name);
int svcmgr_set_command(const char *name, const char *command);
int svcmgr_set_description(const char *name, const char *desc);
int svcmgr_set_autostart(const char *name, int autostart);
int svcmgr_set_restart_policy(const char *name, svcmgr_restart_policy_t policy);
int svcmgr_set_enabled(const char *name, int enabled);
int svcmgr_add_dependency(const char *name, const char *depends_on);
int svcmgr_remove_dependency(const char *name, const char *depends_on);

/* ---- 生命周期 ---- */
int svcmgr_start(const char *name);
int svcmgr_stop(const char *name);
int svcmgr_restart(const char *name);
int svcmgr_start_all(void);     /* 启动所有 autostart=1 的服务 */
int svcmgr_stop_all(void);

/* ---- 查询 ---- */
svcmgr_service_t *svcmgr_find(const char *name);
uint32_t svcmgr_list(svcmgr_service_t *out, uint32_t max_count);
uint32_t svcmgr_history_list(svcmgr_history_t *out, uint32_t max_count);

/* ---- 统计 ---- */
void svcmgr_get_stats(svcmgr_stats_t *stats);
void svcmgr_reset_stats(void);

/* ---- 名称辅助 ---- */
const char *svcmgr_state_name(svcmgr_state_t state);
const char *svcmgr_restart_policy_name(svcmgr_restart_policy_t policy);

/* ---- 状态更新（由调用方在执行结果已知时调用） ---- */
int svcmgr_mark_started(const char *name, int success);
int svcmgr_mark_stopped(const char *name, int exit_code);

#endif /* SVCMGR_H */
