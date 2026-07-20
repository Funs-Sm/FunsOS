/* signal_diag.h - 信号诊断子系统
 *
 * 在既有 signal.c 之上加诊断层：
 *   - 统计每个信号的发送/投递/丢弃次数
 *   - 写入 evlog (source="Signal")
 *   - 持久化信号历史到 FunDB (signal_log 表)
 *   - 提供 sigstat shell 命令查询
 *
 * 与 signal.c 的关系：本子系统不替代信号投递，只做观测记录。
 *   调用 signal_send/kill/signal_deliver 后，调用 sigdiag_record_* 记录。
 */
#ifndef SIGNAL_DIAG_H
#define SIGNAL_DIAG_H

#include "stdint.h"
#include "signal.h"

#define SIGDIAG_DB_PATH       "/var/db/sigdiag.db"
#define SIGDIAG_TABLE_LOG     "signal_log"
#define SIGDIAG_HISTORY_SIZE  64

/* 信号统计 */
typedef struct sigdiag_stat {
    uint32_t sends[NSIG];       /* 每个信号的发送次数 */
    uint32_t delivers[NSIG];    /* 实际投递次数 */
    uint32_t drops[NSIG];       /* 丢弃次数（被忽略/被屏蔽且队列满） */
    uint32_t ignored[NSIG];      /* SIG_IGN 处理次数 */
    uint32_t default_actions[NSIG];
    uint32_t user_handlers[NSIG];
    uint32_t total_sends;
    uint32_t total_delivers;
    uint32_t total_drops;
} sigdiag_stat_t;

/* 历史记录条目 */
typedef struct sigdiag_record {
    uint32_t id;
    pid_t    sender;
    pid_t    target;
    int      signo;
    int      result;       /* 0=ok, -1=error */
    uint32_t timestamp;
    char     reason[32];   /* 调用来源 */
} sigdiag_record_t;

/* ---- 初始化 ---- */
void sigdiag_init(void);
void sigdiag_shutdown(void);

/* ---- 记录信号事件（在 signal.c 投递路径中调用） ---- */
void sigdiag_record_send(pid_t sender, pid_t target, int signo,
                          int result, const char *reason);
void sigdiag_record_deliver(pid_t target, int signo, int handler_kind);
/* handler_kind: 0=default, 1=ignore, 2=user handler */

/* ---- 查询 ---- */
void sigdiag_get_stats(sigdiag_stat_t *stats);
void sigdiag_reset_stats(void);

uint32_t sigdiag_history(sigdiag_record_t *out, uint32_t max_count);

/* 信号名称 */
const char *sigdiag_signal_name(int signo);

#endif /* SIGNAL_DIAG_H */
