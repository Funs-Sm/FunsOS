#ifndef CRON_H
#define CRON_H

#include "stdint.h"
#include "stddef.h"

/* ============================================================
 * Cron Task Scheduler - 定时任务调度子系统
 *
 * 基于 kwork 的周期性定时任务调度器，支持：
 *   - 一次性任务（ONCE）：在指定延迟后执行一次
 *   - 间隔任务（INTERVAL）：每隔 N 秒执行一次
 *   - 每日任务（DAILY）：每天指定时分执行
 *
 * 特性：
 *   - FunDB 持久化（/var/db/cron.db），重启后任务自动恢复
 *   - 通过 kwork 周期性触发（每秒检查一次到期任务）
 *   - 可注册命令执行器回调（shell 注册后可执行任意 shell 命令）
 *   - 运行历史统计（执行次数、错误次数、最后退出码）
 *   - 支持启用/禁用、手动触发
 * ============================================================ */

#define CRON_MAX_JOBS       64      /* 最大任务数 */
#define CRON_MAX_NAME       32      /* 任务名最大长度 */
#define CRON_MAX_CMD_LEN    256     /* 命令最大长度 */
#define CRON_DB_PATH         "/var/db/cron.db"
#define CRON_TICK_MS         1000   /* 调度检查周期（毫秒） */
#define CRON_INVALID_ID      0      /* 0 表示无效 ID（有效 ID 从 1 开始 */

/* 任务类型 */
typedef enum {
    CRON_TYPE_ONCE = 0,        /* 一次性：delay_sec 秒后执行一次 */
    CRON_TYPE_INTERVAL,       /* 间隔型：每隔 interval_sec 秒执行 */
    CRON_TYPE_DAILY,          /* 每日：每天 hour:minute 执行 */
} cron_type_t;

/* 任务状态 */
typedef enum {
    CRON_STATE_DISABLED = 0,   /* 已禁用 */
    CRON_STATE_ENABLED,       /* 已启用，等待到期 */
    CRON_STATE_RUNNING,       /* 正在执行 */
    CRON_STATE_ERROR,         /* 错误状态（执行失败次数过多） */
} cron_state_t;

/* 单个任务定义 */
typedef struct cron_job {
    uint32_t    id;                    /* 任务 ID（>=1） */
    char        name[CRON_MAX_NAME];   /* 任务名称 */
    char        command[CRON_MAX_CMD_LEN]; /* 要执行的命令 */
    cron_type_t type;                  /* 调度类型 */
    cron_state_t state;                /* 当前状态 */

    /* 调度参数 */
    uint32_t    interval_sec;          /* INTERVAL: 间隔秒数；ONCE: 延迟秒数 */
    uint8_t     hour;                  /* DAILY: 小时 (0-23) */
    uint8_t     minute;                /* DAILY: 分钟 (0-59) */

    /* 运行时状态 */
    uint64_t    next_run_tick;         /* 下次执行时刻（ticks） */
    uint64_t    last_run_tick;         /* 上次执行时刻（ticks） */
    uint32_t    run_count;             /* 累计执行次数 */
    uint32_t    error_count;           /* 累计错误次数 */
    int32_t     last_exit_code;        /* 最后一次退出码 */
} cron_job_t;

/* 统计信息 */
typedef struct {
    uint32_t    total_jobs;            /* 总任务数 */
    uint32_t    enabled_jobs;          /* 启用任务数 */
    uint32_t    total_runs;            /* 累计执行次数 */
    uint32_t    total_errors;          /* 累计错误次数 */
    uint32_t    next_id;               /* 下一个待分配的 ID */
} cron_stats_t;

/* 命令执行器回调类型
 * 返回 0 表示成功，非 0 表示失败（错误码）
 * shell 启动时通过 cron_set_executor() 注册，
 * 这样 cron 可以执行任意 shell 命令。
 * 若未注册，cron 仅记录日志不实际执行。 */
typedef int (*cron_executor_t)(const char *command);

/* ---- 初始化 / 关闭 ---- */
void cron_init(void);          /* 启动时调用：加载数据 + 启动 kwork 周期任务 */
void cron_shutdown(void);      /* 关闭前调用：刷盘 + 取消 kwork */

/* ---- 任务管理 ---- */
/* 添加任务（id 字段忽略，由系统分配）
 * 返回分配的任务 ID（>=1），失败返回 CRON_INVALID_ID */
uint32_t cron_add_job(const cron_job_t *job);

/* 删除任务
 * 返回 0 成功，-1 不存在 */
int cron_remove_job(uint32_t id);

/* 启用/禁用任务
 * 返回 0 成功，-1 不存在 */
int cron_enable_job(uint32_t id);
int cron_disable_job(uint32_t id);

/* 手动触发任务（不计入 run_count，但会更新 last_run_tick）
 * 返回 0 成功，-1 不存在，-2 执行器未注册 */
int cron_run_job_now(uint32_t id);

/* 查询：按 ID 查找任务（返回内部指针，不可释放，调用者不应长期持有） */
cron_job_t *cron_find_job(uint32_t id);

/* 查询：列出所有任务（拷贝到调用者缓冲区）
 * 返回实际拷贝的任务数 */
int cron_list_jobs(cron_job_t *out_jobs, uint32_t max_count);

/* ---- 统计 ---- */
void cron_get_stats(cron_stats_t *stats);
void cron_reset_stats(void);

/* ---- 执行器注册 ---- */
void cron_set_executor(cron_executor_t executor);

/* ---- 调度入口（由 kwork 周期调用，不需手动调用） ---- */
void cron_tick(void *unused);

/* ---- 调试 ---- */
void cron_dump_all(void);

#endif /* CRON_H */
