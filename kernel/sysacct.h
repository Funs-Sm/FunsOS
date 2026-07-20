/* sysacct.h - 系统账户审计桥接子系统 (System Accountant)
 *
 * 作用：将现有的 user_ext（用户/密码策略/账户锁定/ACL/双因素/资源限制）
 *       与 evlog（持久化事件日志）打通，并提供登录会话的 FunDB 持久化。
 *
 *   - 所有认证/锁定/口令变更/特权提升事件都同时写入 evlog（source="Security"）
 *   - 登录会话写入 FunDB (sysacct_sessions 表)，重启后可查询历史会话
 *   - 提供 sysacct shell 命令统一查询：会话/锁定/审计/统计
 *
 * 与既有模块的关系：
 *   - user_ext : 已有内存中的审计/锁定/会话（重启丢失）
 *   - sysacct  : 在其之上加 evlog 桥接 + 持久化会话历史
 */
#ifndef SYSACCT_H
#define SYSACCT_H

#include "stdint.h"

#define SYSACCT_DB_PATH         "/var/db/sysacct.db"
#define SYSACCT_TABLE_SESSIONS  "sysacct_sessions"
#define SYSACCT_TABLE_AUDIT     "sysacct_audit"
#define SYSACCT_MAX_SESSIONS    64
#define SYSACCT_MAX_USERNAME    64
#define SYSACCT_MAX_DETAIL      128

/* 审计事件类型（与 user_ext 对齐） */
#define SYSACCT_EV_LOGIN_SUCCESS   1
#define SYSACCT_EV_LOGIN_FAILURE   2
#define SYSACCT_EV_LOGOUT          3
#define SYSACCT_EV_SUDO            4
#define SYSACCT_EV_PASS_CHANGE     5
#define SYSACCT_EV_ACCOUNT_LOCK    6
#define SYSACCT_EV_ACCOUNT_UNLOCK  7
#define SYSACCT_EV_USER_CREATE     8
#define SYSACCT_EV_USER_DELETE     9

/* 会话状态 */
typedef enum {
    SYSACCT_SESS_OPEN    = 1,
    SYSACCT_SESS_CLOSED  = 2,
    SYSACCT_SESS_EXPIRED = 3,
} sysacct_sess_state_t;

/* 会话记录 */
typedef struct sysacct_session {
    uint32_t session_id;
    uint32_t uid;
    char     username[SYSACCT_MAX_USERNAME];
    uint32_t login_tick;
    uint32_t logout_tick;
    uint8_t  state;          /* sysacct_sess_state_t */
    char     remote_ip[48];  /* 来源 IP（"local"=控制台） */
} sysacct_session_t;

/* 审计记录 */
typedef struct sysacct_audit {
    uint32_t id;
    uint32_t uid;
    char     username[SYSACCT_MAX_USERNAME];
    uint32_t event_type;
    uint32_t timestamp;
    int      success;
    char     detail[SYSACCT_MAX_DETAIL];
} sysacct_audit_t;

/* 统计 */
typedef struct {
    uint32_t total_logins;
    uint32_t failed_logins;
    uint32_t active_sessions;
    uint32_t total_sessions;
    uint32_t locked_accounts;
    uint32_t pass_changes;
    uint32_t sudo_uses;
} sysacct_stats_t;

/* ---- 初始化 ---- */
void sysacct_init(void);
void sysacct_shutdown(void);

/* ---- 审计桥接（写入 user_ext + evlog + sysacct_audit 表） ---- */
void sysacct_audit_login_success(const char *username, const char *ip);
void sysacct_audit_login_failure(const char *username, const char *ip);
void sysacct_audit_logout(const char *username);
void sysacct_audit_pass_change(const char *username, int success);
void sysacct_audit_sudo(const char *username, int success);
void sysacct_audit_user_create(const char *username, uint32_t uid);
void sysacct_audit_user_delete(const char *username, uint32_t uid);
void sysacct_audit_account_lock(const char *username);
void sysacct_audit_account_unlock(const char *username);

/* ---- 会话管理（持久化） ---- */
uint32_t sysacct_session_open(uint32_t uid, const char *username, const char *ip);
int      sysacct_session_close(uint32_t session_id);
int      sysacct_session_close_user(const char *username);
uint32_t sysacct_session_list(sysacct_session_t *out, uint32_t max_count,
                                uint8_t state_filter /* 0=全部 */);
uint32_t sysacct_active_session_count(void);

/* ---- 锁定管理（包装 user_ext_lockout，加 evlog） ---- */
int sysacct_lockout_record_failure(const char *username);
int sysacct_lockout_record_success(const char *username);
int sysacct_lockout_unlock(const char *username);
int sysacct_lockout_unlock_all(void);

/* ---- 统计 ---- */
void sysacct_get_stats(sysacct_stats_t *stats);
void sysacct_reset_stats(void);

/* ---- 审计查询 ---- */
uint32_t sysacct_audit_query(sysacct_audit_t *out, uint32_t max_count,
                              uint32_t event_type_filter /* 0=全部 */);

/* ---- 事件类型名称 ---- */
const char *sysacct_event_name(uint32_t event_type);
const char *sysacct_session_state_name(uint8_t state);

#endif /* SYSACCT_H */
