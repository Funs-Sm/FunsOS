/* sysacct.c - 系统账户审计桥接子系统实现
 *
 * 设计：
 *   - 使用 evlog 作为审计事件的主持久化（source="Security", category=SECURITY）
 *   - 使用 FunDB sysacct_sessions 表持久化会话历史（重启后可查）
 *   - 使用 FunDB sysacct_audit 表作为审计事件的可查询镜像
 *   - 所有函数前向调用 user_ext 已有 API（不重复实现用户/锁定逻辑）
 */
#include "sysacct.h"
#include "evlog.h"
#include "user_ext.h"
#include "fundb.h"
#include "klog.h"
#include "spinlock.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "timer.h"

#define SYSACCT_EV_SOURCE "Security"

/* 全局状态 */
static struct {
    spinlock_t      lock;
    fundb_handle_t  db;
    int             initialized;
    uint32_t        next_session_id;
    uint32_t        next_audit_id;
    sysacct_stats_t stats;
} g_acct;

/* ---- 内部辅助 ---- */

static void sysacct_ip_or_local(const char *ip, char *buf, uint32_t size) {
    if (!ip || !*ip) {
        strncpy(buf, "local", size - 1);
        buf[size - 1] = '\0';
    } else {
        strncpy(buf, ip, size - 1);
        buf[size - 1] = '\0';
    }
}

/* ---- FunDB 表初始化 ---- */

static void sysacct_db_init_tables(void) {
    if (!g_acct.db) return;

    /* 会话表 */
    if (!fundb_table_exists(g_acct.db, SYSACCT_TABLE_SESSIONS)) {
        fundb_column_t cols[6];
        memset(cols, 0, sizeof(cols));

        strcpy(cols[0].name, "session_id");
        cols[0].type = FUNDB_TYPE_INT; cols[0].size = 4;
        cols[0].not_null = 1; cols[0].primary_key = 1;

        strcpy(cols[1].name, "uid");
        cols[1].type = FUNDB_TYPE_INT; cols[1].size = 4;

        strcpy(cols[2].name, "username");
        cols[2].type = FUNDB_TYPE_TEXT; cols[2].size = SYSACCT_MAX_USERNAME;

        strcpy(cols[3].name, "login_tick");
        cols[3].type = FUNDB_TYPE_INT; cols[3].size = 4;

        strcpy(cols[4].name, "logout_tick");
        cols[4].type = FUNDB_TYPE_INT; cols[4].size = 4;

        strcpy(cols[5].name, "state");
        cols[5].type = FUNDB_TYPE_INT; cols[5].size = 4;

        int rc = fundb_create_table(g_acct.db, SYSACCT_TABLE_SESSIONS, cols, 6);
        if (rc != FUNDB_OK) {
            klog_warn("sysacct: failed to create %s (%s)",
                      SYSACCT_TABLE_SESSIONS, fundb_error_string(rc));
        }
    }

    /* 审计表 */
    if (!fundb_table_exists(g_acct.db, SYSACCT_TABLE_AUDIT)) {
        fundb_column_t cols[6];
        memset(cols, 0, sizeof(cols));

        strcpy(cols[0].name, "id");
        cols[0].type = FUNDB_TYPE_INT; cols[0].size = 4;
        cols[0].not_null = 1; cols[0].primary_key = 1;

        strcpy(cols[1].name, "uid");
        cols[1].type = FUNDB_TYPE_INT; cols[1].size = 4;

        strcpy(cols[2].name, "username");
        cols[2].type = FUNDB_TYPE_TEXT; cols[2].size = SYSACCT_MAX_USERNAME;

        strcpy(cols[3].name, "event_type");
        cols[3].type = FUNDB_TYPE_INT; cols[3].size = 4;

        strcpy(cols[4].name, "timestamp");
        cols[4].type = FUNDB_TYPE_INT; cols[4].size = 4;

        strcpy(cols[5].name, "detail");
        cols[5].type = FUNDB_TYPE_TEXT; cols[5].size = SYSACCT_MAX_DETAIL;

        int rc = fundb_create_table(g_acct.db, SYSACCT_TABLE_AUDIT, cols, 6);
        if (rc != FUNDB_OK) {
            klog_warn("sysacct: failed to create %s (%s)",
                      SYSACCT_TABLE_AUDIT, fundb_error_string(rc));
        }
    }
}

/* 写入审计到 FunDB（同时已写入 evlog） */
static void sysacct_db_insert_audit(uint32_t uid, const char *username,
                                     uint32_t event_type, const char *detail) {
    if (!g_acct.db) return;

    uint32_t id = ++g_acct.next_audit_id;
    uint32_t ts = (uint32_t)timer_get_ticks();

    void *vals[6];
    uint32_t sizes[6];
    uint32_t types[6];

    vals[0] = &id;       sizes[0] = 4;                 types[0] = FUNDB_TYPE_INT;
    vals[1] = &uid;      sizes[1] = 4;                 types[1] = FUNDB_TYPE_INT;
    vals[2] = (void *)username; sizes[2] = (uint32_t)strlen(username) + 1; types[2] = FUNDB_TYPE_TEXT;
    vals[3] = &event_type; sizes[3] = 4;              types[3] = FUNDB_TYPE_INT;
    vals[4] = &ts;        sizes[4] = 4;                 types[4] = FUNDB_TYPE_INT;
    vals[5] = (void *)detail; sizes[5] = (uint32_t)strlen(detail) + 1; types[5] = FUNDB_TYPE_TEXT;

    fundb_row_t row;
    row.values = vals; row.sizes = sizes; row.types = types;
    fundb_insert(g_acct.db, SYSACCT_TABLE_AUDIT, &row);
}

/* 更新会话为已关闭（delete-then-insert 模式） */
static void sysacct_db_close_session(uint32_t session_id, uint32_t logout_tick) {
    if (!g_acct.db) return;

    /* 先查询原会话 */
    char where[64];
    snprintf(where, sizeof(where), "session_id = %u", session_id);
    fundb_result_t *r = fundb_select(g_acct.db, SYSACCT_TABLE_SESSIONS, "*", where, NULL, 1);
    if (!r || r->row_count == 0) {
        if (r) fundb_free_result(r);
        return;
    }

    /* 读取原数据 */
    uint32_t uid = r->rows[0].values[1] ? *(uint32_t *)r->rows[0].values[1] : 0;
    const char *username = r->rows[0].values[2] ? (const char *)r->rows[0].values[2] : "";
    uint32_t login_tick = r->rows[0].values[3] ? *(uint32_t *)r->rows[0].values[3] : 0;
    fundb_free_result(r);

    /* 删除旧行 */
    fundb_delete(g_acct.db, SYSACCT_TABLE_SESSIONS, where);

    /* 插入更新后的行 */
    uint32_t state = SYSACCT_SESS_CLOSED;
    void *vals[6];
    uint32_t sizes[6];
    uint32_t types[6];

    vals[0] = &session_id;  sizes[0] = 4; types[0] = FUNDB_TYPE_INT;
    vals[1] = &uid;          sizes[1] = 4; types[1] = FUNDB_TYPE_INT;
    vals[2] = (void *)username; sizes[2] = (uint32_t)strlen(username) + 1; types[2] = FUNDB_TYPE_TEXT;
    vals[3] = &login_tick;   sizes[3] = 4; types[3] = FUNDB_TYPE_INT;
    vals[4] = &logout_tick;  sizes[4] = 4; types[4] = FUNDB_TYPE_INT;
    vals[5] = &state;        sizes[5] = 4; types[5] = FUNDB_TYPE_INT;

    fundb_row_t row;
    row.values = vals; row.sizes = sizes; row.types = types;
    fundb_insert(g_acct.db, SYSACCT_TABLE_SESSIONS, &row);
}

/* ---- 内部审计写入（统一入口：写 user_ext + evlog + sysacct_audit 表） ---- */

static void sysacct_emit_audit(uint32_t uid, const char *username,
                                uint32_t event_type, int success,
                                const char *ip, const char *detail) {
    char ip_buf[48];
    sysacct_ip_or_local(ip, ip_buf, sizeof(ip_buf));

    /* 1) 写入 user_ext 的内存审计日志（如果可用） */
    user_ext_audit_log(uid, username, event_type, ip_buf, success, detail);

    /* 2) 写入 evlog（持久化） */
    uint8_t sev;
    switch (event_type) {
        case SYSACCT_EV_LOGIN_SUCCESS:
        case SYSACCT_EV_LOGOUT:
        case SYSACCT_EV_PASS_CHANGE:
        case SYSACCT_EV_USER_CREATE:
        case SYSACCT_EV_USER_DELETE:
        case SYSACCT_EV_ACCOUNT_UNLOCK:
            sev = success ? EVLOG_SEV_INFO : EVLOG_SEV_WARNING;
            break;
        case SYSACCT_EV_LOGIN_FAILURE:
        case SYSACCT_EV_SUDO:
            sev = success ? EVLOG_SEV_INFO : EVLOG_SEV_WARNING;
            if (!success) sev = EVLOG_SEV_WARNING;
            break;
        case SYSACCT_EV_ACCOUNT_LOCK:
            sev = EVLOG_SEV_CRITICAL;
            break;
        default:
            sev = EVLOG_SEV_INFO;
            break;
    }
    evlog_write(SYSACCT_EV_SOURCE, sev, EVLOG_CAT_SECURITY, event_type,
                "user='%s' uid=%u event=%s ip=%s success=%d detail=%s",
                username, uid, sysacct_event_name(event_type),
                ip_buf, success, detail);

    /* 3) 写入 sysacct_audit 表 */
    sysacct_db_insert_audit(uid, username, event_type, detail);
}

/* ---- 公共 API ---- */

void sysacct_init(void) {
    memset(&g_acct, 0, sizeof(g_acct));
    spinlock_init(&g_acct.lock);
    g_acct.next_session_id = 1;
    g_acct.next_audit_id = 1;

    g_acct.db = fundb_open(SYSACCT_DB_PATH);
    if (!g_acct.db) {
        klog_warn("sysacct: failed to open %s (audit/table features disabled)",
                  SYSACCT_DB_PATH);
    }
    sysacct_db_init_tables();

    /* 注册 evlog 源 */
    evlog_register_source(SYSACCT_EV_SOURCE);

    g_acct.initialized = 1;
    klog_info("sysacct: security audit bridge initialized (db=%s)",
              g_acct.db ? SYSACCT_DB_PATH : "<none>");
}

void sysacct_shutdown(void) {
    if (!g_acct.initialized) return;
    if (g_acct.db) {
        fundb_close(g_acct.db);
        g_acct.db = NULL;
    }
    g_acct.initialized = 0;
}

/* ---- 审计桥接 ---- */

void sysacct_audit_login_success(const char *username, const char *ip) {
    if (!username) return;
    spinlock_lock(&g_acct.lock);
    user_ext_t *u = user_ext_find_by_name(username);
    uint32_t uid = u ? u->uid : 0;
    g_acct.stats.total_logins++;
    spinlock_unlock(&g_acct.lock);

    char detail[SYSACCT_MAX_DETAIL];
    snprintf(detail, sizeof(detail), "login success");
    sysacct_emit_audit(uid, username, SYSACCT_EV_LOGIN_SUCCESS, 1, ip, detail);
}

void sysacct_audit_login_failure(const char *username, const char *ip) {
    if (!username) return;
    spinlock_lock(&g_acct.lock);
    user_ext_t *u = user_ext_find_by_name(username);
    uint32_t uid = u ? u->uid : 0;
    g_acct.stats.failed_logins++;
    spinlock_unlock(&g_acct.lock);

    char detail[SYSACCT_MAX_DETAIL];
    snprintf(detail, sizeof(detail), "login failed (bad credentials)");
    sysacct_emit_audit(uid, username, SYSACCT_EV_LOGIN_FAILURE, 0, ip, detail);
}

void sysacct_audit_logout(const char *username) {
    if (!username) return;
    spinlock_lock(&g_acct.lock);
    user_ext_t *u = user_ext_find_by_name(username);
    uint32_t uid = u ? u->uid : 0;
    spinlock_unlock(&g_acct.lock);

    /* 关闭该用户的所有打开会话 */
    sysacct_session_close_user(username);

    char detail[SYSACCT_MAX_DETAIL];
    snprintf(detail, sizeof(detail), "logout");
    sysacct_emit_audit(uid, username, SYSACCT_EV_LOGOUT, 1, NULL, detail);
}

void sysacct_audit_pass_change(const char *username, int success) {
    if (!username) return;
    spinlock_lock(&g_acct.lock);
    user_ext_t *u = user_ext_find_by_name(username);
    uint32_t uid = u ? u->uid : 0;
    if (success) g_acct.stats.pass_changes++;
    spinlock_unlock(&g_acct.lock);

    char detail[SYSACCT_MAX_DETAIL];
    snprintf(detail, sizeof(detail), "password change %s",
             success ? "succeeded" : "failed");
    sysacct_emit_audit(uid, username, SYSACCT_EV_PASS_CHANGE, success, NULL, detail);
}

void sysacct_audit_sudo(const char *username, int success) {
    if (!username) return;
    spinlock_lock(&g_acct.lock);
    user_ext_t *u = user_ext_find_by_name(username);
    uint32_t uid = u ? u->uid : 0;
    if (success) g_acct.stats.sudo_uses++;
    spinlock_unlock(&g_acct.lock);

    char detail[SYSACCT_MAX_DETAIL];
    snprintf(detail, sizeof(detail), "sudo elevation %s",
             success ? "granted" : "denied");
    sysacct_emit_audit(uid, username, SYSACCT_EV_SUDO, success, NULL, detail);
}

void sysacct_audit_user_create(const char *username, uint32_t uid) {
    if (!username) return;
    char detail[SYSACCT_MAX_DETAIL];
    snprintf(detail, sizeof(detail), "user created uid=%u", uid);
    sysacct_emit_audit(uid, username, SYSACCT_EV_USER_CREATE, 1, NULL, detail);
}

void sysacct_audit_user_delete(const char *username, uint32_t uid) {
    if (!username) return;
    char detail[SYSACCT_MAX_DETAIL];
    snprintf(detail, sizeof(detail), "user deleted uid=%u", uid);
    sysacct_emit_audit(uid, username, SYSACCT_EV_USER_DELETE, 1, NULL, detail);
}

void sysacct_audit_account_lock(const char *username) {
    if (!username) return;
    spinlock_lock(&g_acct.lock);
    user_ext_t *u = user_ext_find_by_name(username);
    uint32_t uid = u ? u->uid : 0;
    g_acct.stats.locked_accounts++;
    spinlock_unlock(&g_acct.lock);

    char detail[SYSACCT_MAX_DETAIL];
    snprintf(detail, sizeof(detail), "account locked due to failures");
    sysacct_emit_audit(uid, username, SYSACCT_EV_ACCOUNT_LOCK, 1, NULL, detail);
}

void sysacct_audit_account_unlock(const char *username) {
    if (!username) return;
    spinlock_lock(&g_acct.lock);
    user_ext_t *u = user_ext_find_by_name(username);
    uint32_t uid = u ? u->uid : 0;
    if (g_acct.stats.locked_accounts > 0) g_acct.stats.locked_accounts--;
    spinlock_unlock(&g_acct.lock);

    char detail[SYSACCT_MAX_DETAIL];
    snprintf(detail, sizeof(detail), "account unlocked by admin");
    sysacct_emit_audit(uid, username, SYSACCT_EV_ACCOUNT_UNLOCK, 1, NULL, detail);
}

/* ---- 会话管理 ---- */

uint32_t sysacct_session_open(uint32_t uid, const char *username, const char *ip) {
    if (!username) return 0;
    spinlock_lock(&g_acct.lock);
    uint32_t sid = g_acct.next_session_id++;
    if (sid == 0) sid = g_acct.next_session_id++;
    g_acct.stats.total_sessions++;
    g_acct.stats.active_sessions++;
    spinlock_unlock(&g_acct.lock);

    /* 同时调用 user_ext 创建内存会话 */
    int ext_sid = user_ext_session_create(uid);
    (void)ext_sid; /* user_ext 的 session_id 与 sysacct 的不同 */

    uint32_t login_tick = (uint32_t)timer_get_ticks();
    char ip_buf[48];
    sysacct_ip_or_local(ip, ip_buf, sizeof(ip_buf));

    /* 持久化到 FunDB */
    if (g_acct.db) {
        uint32_t state = SYSACCT_SESS_OPEN;
        void *vals[6];
        uint32_t sizes[6];
        uint32_t types[6];

        vals[0] = &sid;        sizes[0] = 4; types[0] = FUNDB_TYPE_INT;
        vals[1] = &uid;         sizes[1] = 4; types[1] = FUNDB_TYPE_INT;
        vals[2] = (void *)username; sizes[2] = (uint32_t)strlen(username) + 1; types[2] = FUNDB_TYPE_TEXT;
        vals[3] = &login_tick;  sizes[3] = 4; types[3] = FUNDB_TYPE_INT;
        vals[4] = &login_tick;  sizes[4] = 4; types[4] = FUNDB_TYPE_INT; /* logout=login initially */
        vals[5] = &state;       sizes[5] = 4; types[5] = FUNDB_TYPE_INT;

        fundb_row_t row;
        row.values = vals; row.sizes = sizes; row.types = types;
        fundb_insert(g_acct.db, SYSACCT_TABLE_SESSIONS, &row);
    }

    evlog_info(SYSACCT_EV_SOURCE, SYSACCT_EV_LOGIN_SUCCESS,
               "session opened sid=%u user='%s' uid=%u ip=%s",
               sid, username, uid, ip_buf);

    return sid;
}

int sysacct_session_close(uint32_t session_id) {
    if (session_id == 0) return -1;
    uint32_t logout_tick = (uint32_t)timer_get_ticks();

    spinlock_lock(&g_acct.lock);
    if (g_acct.stats.active_sessions > 0) g_acct.stats.active_sessions--;
    spinlock_unlock(&g_acct.lock);

    if (g_acct.db) {
        sysacct_db_close_session(session_id, logout_tick);
    }

    evlog_info(SYSACCT_EV_SOURCE, SYSACCT_EV_LOGOUT,
               "session closed sid=%u logout_tick=%u", session_id, logout_tick);
    return 0;
}

int sysacct_session_close_user(const char *username) {
    if (!username || !g_acct.db) return -1;

    /* 查询该用户的所有打开会话 */
    fundb_result_t *r = fundb_select(g_acct.db, SYSACCT_TABLE_SESSIONS, "*", NULL, NULL, 0);
    if (!r) return -1;

    int closed = 0;
    for (uint32_t i = 0; i < r->row_count; i++) {
        if (!r->rows[i].values[2] || !r->rows[i].values[5]) continue;
        const char *uname = (const char *)r->rows[i].values[2];
        uint32_t state = *(uint32_t *)r->rows[i].values[5];
        if (strcmp(uname, username) == 0 && state == SYSACCT_SESS_OPEN) {
            uint32_t sid = r->rows[i].values[0] ? *(uint32_t *)r->rows[i].values[0] : 0;
            if (sid) {
                sysacct_session_close(sid);
                closed++;
            }
        }
    }
    fundb_free_result(r);
    return closed;
}

uint32_t sysacct_session_list(sysacct_session_t *out, uint32_t max_count,
                                uint8_t state_filter) {
    if (!out || !g_acct.db) return 0;
    fundb_result_t *r = fundb_select(g_acct.db, SYSACCT_TABLE_SESSIONS, "*", NULL, NULL, 0);
    if (!r) return 0;

    uint32_t count = 0;
    for (uint32_t i = 0; i < r->row_count && count < max_count; i++) {
        if (!r->rows[i].values[0]) continue;
        uint32_t state = r->rows[i].values[5] ? *(uint32_t *)r->rows[i].values[5] : 0;
        if (state_filter != 0 && state != state_filter) continue;

        sysacct_session_t *s = &out[count];
        memset(s, 0, sizeof(*s));
        s->session_id  = *(uint32_t *)r->rows[i].values[0];
        s->uid         = r->rows[i].values[1] ? *(uint32_t *)r->rows[i].values[1] : 0;
        if (r->rows[i].values[2]) {
            strncpy(s->username, (const char *)r->rows[i].values[2], SYSACCT_MAX_USERNAME - 1);
        }
        s->login_tick  = r->rows[i].values[3] ? *(uint32_t *)r->rows[i].values[3] : 0;
        s->logout_tick = r->rows[i].values[4] ? *(uint32_t *)r->rows[i].values[4] : 0;
        s->state       = (uint8_t)state;
        count++;
    }
    fundb_free_result(r);
    return count;
}

uint32_t sysacct_active_session_count(void) {
    if (!g_acct.db) return g_acct.stats.active_sessions;
    fundb_result_t *r = fundb_select(g_acct.db, SYSACCT_TABLE_SESSIONS, "*", NULL, NULL, 0);
    if (!r) return g_acct.stats.active_sessions;
    uint32_t count = 0;
    for (uint32_t i = 0; i < r->row_count; i++) {
        if (r->rows[i].values[5] &&
            *(uint32_t *)r->rows[i].values[5] == SYSACCT_SESS_OPEN) {
            count++;
        }
    }
    fundb_free_result(r);
    return count;
}

/* ---- 锁定管理（包装 user_ext_lockout，加 evlog） ---- */

int sysacct_lockout_record_failure(const char *username) {
    if (!username) return -1;
    int was_locked = user_ext_lockout_is_locked(username);
    int now_locked = user_ext_lockout_record_failure(username);
    if (!was_locked && now_locked) {
        /* 刚刚被锁定，发出审计事件 */
        sysacct_audit_account_lock(username);
    }
    return now_locked;
}

int sysacct_lockout_record_success(const char *username) {
    return user_ext_lockout_record_success(username);
}

int sysacct_lockout_unlock(const char *username) {
    if (!username) return -1;
    int rc = user_ext_lockout_unlock(username);
    if (rc == 0) {
        sysacct_audit_account_unlock(username);
    }
    return rc;
}

int sysacct_lockout_unlock_all(void) {
    int rc = user_ext_lockout_unlock_all();
    if (rc == 0) {
        spinlock_lock(&g_acct.lock);
        g_acct.stats.locked_accounts = 0;
        spinlock_unlock(&g_acct.lock);
        evlog_info(SYSACCT_EV_SOURCE, SYSACCT_EV_ACCOUNT_UNLOCK,
                   "all locked accounts unlocked by admin");
    }
    return rc;
}

/* ---- 统计 ---- */

void sysacct_get_stats(sysacct_stats_t *stats) {
    if (!stats) return;
    spinlock_lock(&g_acct.lock);
    *stats = g_acct.stats;
    spinlock_unlock(&g_acct.lock);
}

void sysacct_reset_stats(void) {
    spinlock_lock(&g_acct.lock);
    memset(&g_acct.stats, 0, sizeof(g_acct.stats));
    spinlock_unlock(&g_acct.lock);
}

/* ---- 审计查询 ---- */

uint32_t sysacct_audit_query(sysacct_audit_t *out, uint32_t max_count,
                              uint32_t event_type_filter) {
    if (!out || !g_acct.db) return 0;
    fundb_result_t *r = fundb_select(g_acct.db, SYSACCT_TABLE_AUDIT, "*", NULL, NULL, 0);
    if (!r) return 0;

    uint32_t count = 0;
    for (uint32_t i = 0; i < r->row_count && count < max_count; i++) {
        if (!r->rows[i].values[0]) continue;
        uint32_t et = r->rows[i].values[3] ? *(uint32_t *)r->rows[i].values[3] : 0;
        if (event_type_filter != 0 && et != event_type_filter) continue;

        sysacct_audit_t *a = &out[count];
        memset(a, 0, sizeof(*a));
        a->id         = *(uint32_t *)r->rows[i].values[0];
        a->uid        = r->rows[i].values[1] ? *(uint32_t *)r->rows[i].values[1] : 0;
        if (r->rows[i].values[2]) {
            strncpy(a->username, (const char *)r->rows[i].values[2], SYSACCT_MAX_USERNAME - 1);
        }
        a->event_type = et;
        a->timestamp  = r->rows[i].values[4] ? *(uint32_t *)r->rows[i].values[4] : 0;
        a->success    = 1; /* sysacct_audit 表只存成功事件，失败由 evlog 保留 */
        if (r->rows[i].values[5]) {
            strncpy(a->detail, (const char *)r->rows[i].values[5], SYSACCT_MAX_DETAIL - 1);
        }
        count++;
    }
    fundb_free_result(r);
    return count;
}

/* ---- 名称辅助 ---- */

const char *sysacct_event_name(uint32_t event_type) {
    switch (event_type) {
        case SYSACCT_EV_LOGIN_SUCCESS:  return "LOGIN_SUCCESS";
        case SYSACCT_EV_LOGIN_FAILURE:  return "LOGIN_FAILURE";
        case SYSACCT_EV_LOGOUT:         return "LOGOUT";
        case SYSACCT_EV_SUDO:           return "SUDO";
        case SYSACCT_EV_PASS_CHANGE:    return "PASS_CHANGE";
        case SYSACCT_EV_ACCOUNT_LOCK:   return "ACCOUNT_LOCK";
        case SYSACCT_EV_ACCOUNT_UNLOCK: return "ACCOUNT_UNLOCK";
        case SYSACCT_EV_USER_CREATE:    return "USER_CREATE";
        case SYSACCT_EV_USER_DELETE:    return "USER_DELETE";
        default: return "UNKNOWN";
    }
}

const char *sysacct_session_state_name(uint8_t state) {
    switch (state) {
        case SYSACCT_SESS_OPEN:    return "OPEN";
        case SYSACCT_SESS_CLOSED:  return "CLOSED";
        case SYSACCT_SESS_EXPIRED: return "EXPIRED";
        default: return "UNKNOWN";
    }
}
