/* svcmgr.c - 服务管理器实现
 *
 * 实现：
 *   - 服务表存储在内存数组中（最多 SVCMGR_MAX_SERVICES 项）
 *   - 服务元数据（除运行时状态外）持久化到 FunDB svcmgr_services 表
 *   - 启动/停止历史记录持久化到 FunDB svcmgr_history 表
 *   - 通过 shell_execute() 启动服务的 command
 *   - 所有状态变化写入 evlog (source="Service")
 *
 * 注意：本系统为单核 hobbyist OS，服务"启动"实为执行 shell 命令；
 *       由于没有真正的进程隔离，运行时状态依赖显式 mark_started/mark_stopped 调用。
 */
#include "svcmgr.h"
#include "evlog.h"
#include "fundb.h"
#include "klog.h"
#include "spinlock.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "timer.h"

#define SVCMGR_EV_SOURCE "Service"

/* shell_execute 在 shell.c 中实现 */
extern void shell_execute(const char *cmd);

/* 全局状态 */
static struct {
    spinlock_t         lock;
    fundb_handle_t     db;
    int                initialized;
    svcmgr_service_t   services[SVCMGR_MAX_SERVICES];
    uint32_t           service_count;
    svcmgr_history_t   history[SVCMGR_HISTORY_SIZE];
    uint32_t           history_head;     /* 环形缓冲写入位置 */
    svcmgr_stats_t     stats;
} g_svc;

/* ---- 内部辅助 ---- */

static svcmgr_service_t *svcmgr_find_internal(const char *name) {
    for (uint32_t i = 0; i < g_svc.service_count; i++) {
        if (strcmp(g_svc.services[i].name, name) == 0) {
            return &g_svc.services[i];
        }
    }
    return NULL;
}

static void svcmgr_history_add(const char *name, uint32_t start_tick,
                                uint32_t stop_tick, int32_t exit_code, uint8_t state) {
    svcmgr_history_t *h = &g_svc.history[g_svc.history_head];
    memset(h, 0, sizeof(*h));
    strncpy(h->service_name, name, SVCMGR_MAX_NAME - 1);
    h->start_tick = start_tick;
    h->stop_tick  = stop_tick;
    h->exit_code  = exit_code;
    h->state      = state;
    g_svc.history_head = (g_svc.history_head + 1) % SVCMGR_HISTORY_SIZE;

    /* 持久化到 FunDB */
    if (g_svc.db) {
        void *vals[4];
        uint32_t sizes[4];
        uint32_t types[4];

        vals[0] = (void *)name; sizes[0] = (uint32_t)strlen(name) + 1; types[0] = FUNDB_TYPE_TEXT;
        vals[1] = &start_tick;  sizes[1] = 4; types[1] = FUNDB_TYPE_INT;
        vals[2] = &stop_tick;   sizes[2] = 4; types[2] = FUNDB_TYPE_INT;
        vals[3] = &exit_code;   sizes[3] = 4; types[3] = FUNDB_TYPE_INT;

        fundb_row_t row;
        row.values = vals; row.sizes = sizes; row.types = types;
        fundb_insert(g_svc.db, SVCMGR_TABLE_HISTORY, &row);
    }
}

/* ---- FunDB 表初始化与持久化 ---- */

static void svcmgr_db_init_tables(void) {
    if (!g_svc.db) return;

    /* 服务表 */
    if (!fundb_table_exists(g_svc.db, SVCMGR_TABLE_SERVICES)) {
        fundb_column_t cols[5];
        memset(cols, 0, sizeof(cols));

        strcpy(cols[0].name, "name");
        cols[0].type = FUNDB_TYPE_TEXT; cols[0].size = SVCMGR_MAX_NAME;
        cols[0].not_null = 1; cols[0].primary_key = 1;

        strcpy(cols[1].name, "description");
        cols[1].type = FUNDB_TYPE_TEXT; cols[1].size = SVCMGR_MAX_DESC;

        strcpy(cols[2].name, "command");
        cols[2].type = FUNDB_TYPE_TEXT; cols[2].size = SVCMGR_MAX_CMD;

        strcpy(cols[3].name, "autostart");
        cols[3].type = FUNDB_TYPE_INT; cols[3].size = 4;

        strcpy(cols[4].name, "restart_policy");
        cols[4].type = FUNDB_TYPE_INT; cols[4].size = 4;

        int rc = fundb_create_table(g_svc.db, SVCMGR_TABLE_SERVICES, cols, 5);
        if (rc != FUNDB_OK) {
            klog_warn("svcmgr: failed to create %s (%s)",
                      SVCMGR_TABLE_SERVICES, fundb_error_string(rc));
        }
    }

    /* 历史表 */
    if (!fundb_table_exists(g_svc.db, SVCMGR_TABLE_HISTORY)) {
        fundb_column_t cols[4];
        memset(cols, 0, sizeof(cols));

        strcpy(cols[0].name, "service_name");
        cols[0].type = FUNDB_TYPE_TEXT; cols[0].size = SVCMGR_MAX_NAME;

        strcpy(cols[1].name, "start_tick");
        cols[1].type = FUNDB_TYPE_INT; cols[1].size = 4;

        strcpy(cols[2].name, "stop_tick");
        cols[2].type = FUNDB_TYPE_INT; cols[2].size = 4;

        strcpy(cols[3].name, "exit_code");
        cols[3].type = FUNDB_TYPE_INT; cols[3].size = 4;

        int rc = fundb_create_table(g_svc.db, SVCMGR_TABLE_HISTORY, cols, 4);
        if (rc != FUNDB_OK) {
            klog_warn("svcmgr: failed to create %s (%s)",
                      SVCMGR_TABLE_HISTORY, fundb_error_string(rc));
        }
    }
}

/* 持久化服务定义（先删除再插入） */
static void svcmgr_db_persist_service(const svcmgr_service_t *s) {
    if (!g_svc.db) return;

    char where[64];
    snprintf(where, sizeof(where), "name = '%s'", s->name);
    fundb_delete(g_svc.db, SVCMGR_TABLE_SERVICES, where);

    uint32_t autostart = s->autostart;
    uint32_t policy = s->restart_policy;

    void *vals[5];
    uint32_t sizes[5];
    uint32_t types[5];

    vals[0] = (void *)s->name;        sizes[0] = (uint32_t)strlen(s->name) + 1;        types[0] = FUNDB_TYPE_TEXT;
    vals[1] = (void *)s->description; sizes[1] = (uint32_t)strlen(s->description) + 1; types[1] = FUNDB_TYPE_TEXT;
    vals[2] = (void *)s->command;     sizes[2] = (uint32_t)strlen(s->command) + 1;    types[2] = FUNDB_TYPE_TEXT;
    vals[3] = &autostart;             sizes[3] = 4; types[3] = FUNDB_TYPE_INT;
    vals[4] = &policy;                sizes[4] = 4; types[4] = FUNDB_TYPE_INT;

    fundb_row_t row;
    row.values = vals; row.sizes = sizes; row.types = types;
    fundb_insert(g_svc.db, SVCMGR_TABLE_SERVICES, &row);
}

/* 从 FunDB 删除服务定义 */
static void svcmgr_db_delete_service(const char *name) {
    if (!g_svc.db) return;
    char where[64];
    snprintf(where, sizeof(where), "name = '%s'", name);
    fundb_delete(g_svc.db, SVCMGR_TABLE_SERVICES, where);
}

/* 启动时从 FunDB 加载服务定义 */
static void svcmgr_db_load_services(void) {
    if (!g_svc.db) return;
    fundb_result_t *r = fundb_select(g_svc.db, SVCMGR_TABLE_SERVICES, "*", NULL, NULL, 0);
    if (!r) return;

    for (uint32_t i = 0; i < r->row_count && g_svc.service_count < SVCMGR_MAX_SERVICES; i++) {
        if (!r->rows[i].values[0]) continue;
        svcmgr_service_t *s = &g_svc.services[g_svc.service_count];
        memset(s, 0, sizeof(*s));
        strncpy(s->name, (const char *)r->rows[i].values[0], SVCMGR_MAX_NAME - 1);
        if (r->rows[i].values[1]) {
            strncpy(s->description, (const char *)r->rows[i].values[1], SVCMGR_MAX_DESC - 1);
        }
        if (r->rows[i].values[2]) {
            strncpy(s->command, (const char *)r->rows[i].values[2], SVCMGR_MAX_CMD - 1);
        }
        s->autostart = r->rows[i].values[3] ? *(uint32_t *)r->rows[i].values[3] : 0;
        s->restart_policy = r->rows[i].values[4] ? *(uint32_t *)r->rows[i].values[4] : SVCMGR_RESTART_NEVER;
        s->state = SVCMGR_STATE_STOPPED;
        s->enabled = 1;
        g_svc.service_count++;
    }
    fundb_free_result(r);
}

/* ---- 公共 API ---- */

void svcmgr_init(void) {
    memset(&g_svc, 0, sizeof(g_svc));
    spinlock_init(&g_svc.lock);

    g_svc.db = fundb_open(SVCMGR_DB_PATH);
    if (!g_svc.db) {
        klog_warn("svcmgr: failed to open %s (persistence disabled)", SVCMGR_DB_PATH);
    }
    svcmgr_db_init_tables();
    svcmgr_db_load_services();

    evlog_register_source(SVCMGR_EV_SOURCE);

    g_svc.initialized = 1;
    klog_info("svcmgr: service manager initialized (%u services registered)",
              g_svc.service_count);
}

void svcmgr_shutdown(void) {
    if (!g_svc.initialized) return;
    /* 停止所有运行中的服务 */
    spinlock_lock(&g_svc.lock);
    for (uint32_t i = 0; i < g_svc.service_count; i++) {
        if (g_svc.services[i].state == SVCMGR_STATE_RUNNING) {
            g_svc.services[i].state = SVCMGR_STATE_STOPPED;
            g_svc.services[i].stop_tick = (uint32_t)timer_get_ticks();
        }
    }
    spinlock_unlock(&g_svc.lock);

    if (g_svc.db) {
        fundb_close(g_svc.db);
        g_svc.db = NULL;
    }
    g_svc.initialized = 0;
}

/* ---- 服务注册 ---- */

int svcmgr_register(const char *name, const char *description, const char *command,
                    int autostart, svcmgr_restart_policy_t restart_policy) {
    if (!name || !*name || !command) return -1;

    spinlock_lock(&g_svc.lock);

    /* 已存在则更新 */
    svcmgr_service_t *s = svcmgr_find_internal(name);
    if (s) {
        if (description) {
            strncpy(s->description, description, SVCMGR_MAX_DESC - 1);
            s->description[SVCMGR_MAX_DESC - 1] = '\0';
        }
        strncpy(s->command, command, SVCMGR_MAX_CMD - 1);
        s->command[SVCMGR_MAX_CMD - 1] = '\0';
        s->autostart = (uint8_t)autostart;
        s->restart_policy = (uint8_t)restart_policy;
        s->enabled = 1;
        svcmgr_db_persist_service(s);
        spinlock_unlock(&g_svc.lock);
        evlog_info(SVCMGR_EV_SOURCE, 1, "service '%s' re-registered (cmd='%s')",
                   name, command);
        return 0;
    }

    /* 新增 */
    if (g_svc.service_count >= SVCMGR_MAX_SERVICES) {
        spinlock_unlock(&g_svc.lock);
        return -2;
    }

    s = &g_svc.services[g_svc.service_count];
    memset(s, 0, sizeof(*s));
    strncpy(s->name, name, SVCMGR_MAX_NAME - 1);
    if (description) {
        strncpy(s->description, description, SVCMGR_MAX_DESC - 1);
    }
    strncpy(s->command, command, SVCMGR_MAX_CMD - 1);
    s->autostart = (uint8_t)autostart;
    s->restart_policy = (uint8_t)restart_policy;
    s->state = SVCMGR_STATE_STOPPED;
    s->enabled = 1;
    g_svc.service_count++;
    g_svc.stats.total_services++;

    svcmgr_db_persist_service(s);
    spinlock_unlock(&g_svc.lock);

    evlog_info(SVCMGR_EV_SOURCE, 1, "service '%s' registered (cmd='%s' autostart=%d policy=%s)",
               name, command, autostart, svcmgr_restart_policy_name(restart_policy));
    return 0;
}

int svcmgr_unregister(const char *name) {
    if (!name) return -1;
    spinlock_lock(&g_svc.lock);

    svcmgr_service_t *s = svcmgr_find_internal(name);
    if (!s) {
        spinlock_unlock(&g_svc.lock);
        return -2;
    }
    if (s->state == SVCMGR_STATE_RUNNING) {
        spinlock_unlock(&g_svc.lock);
        return -3;  /* 必须先停止 */
    }

    /* 移除：用最后一项覆盖 */
    uint32_t idx = (uint32_t)(s - g_svc.services);
    if (idx < g_svc.service_count - 1) {
        g_svc.services[idx] = g_svc.services[g_svc.service_count - 1];
    }
    g_svc.service_count--;
    if (g_svc.stats.total_services > 0) g_svc.stats.total_services--;

    svcmgr_db_delete_service(name);
    spinlock_unlock(&g_svc.lock);

    evlog_info(SVCMGR_EV_SOURCE, 2, "service '%s' unregistered", name);
    return 0;
}

int svcmgr_set_command(const char *name, const char *command) {
    if (!name || !command) return -1;
    spinlock_lock(&g_svc.lock);
    svcmgr_service_t *s = svcmgr_find_internal(name);
    if (!s) { spinlock_unlock(&g_svc.lock); return -2; }
    strncpy(s->command, command, SVCMGR_MAX_CMD - 1);
    s->command[SVCMGR_MAX_CMD - 1] = '\0';
    svcmgr_db_persist_service(s);
    spinlock_unlock(&g_svc.lock);
    return 0;
}

int svcmgr_set_description(const char *name, const char *desc) {
    if (!name) return -1;
    spinlock_lock(&g_svc.lock);
    svcmgr_service_t *s = svcmgr_find_internal(name);
    if (!s) { spinlock_unlock(&g_svc.lock); return -2; }
    if (desc) {
        strncpy(s->description, desc, SVCMGR_MAX_DESC - 1);
        s->description[SVCMGR_MAX_DESC - 1] = '\0';
    }
    svcmgr_db_persist_service(s);
    spinlock_unlock(&g_svc.lock);
    return 0;
}

int svcmgr_set_autostart(const char *name, int autostart) {
    if (!name) return -1;
    spinlock_lock(&g_svc.lock);
    svcmgr_service_t *s = svcmgr_find_internal(name);
    if (!s) { spinlock_unlock(&g_svc.lock); return -2; }
    s->autostart = (uint8_t)autostart;
    svcmgr_db_persist_service(s);
    spinlock_unlock(&g_svc.lock);
    return 0;
}

int svcmgr_set_restart_policy(const char *name, svcmgr_restart_policy_t policy) {
    if (!name) return -1;
    spinlock_lock(&g_svc.lock);
    svcmgr_service_t *s = svcmgr_find_internal(name);
    if (!s) { spinlock_unlock(&g_svc.lock); return -2; }
    s->restart_policy = (uint8_t)policy;
    svcmgr_db_persist_service(s);
    spinlock_unlock(&g_svc.lock);
    return 0;
}

int svcmgr_set_enabled(const char *name, int enabled) {
    if (!name) return -1;
    spinlock_lock(&g_svc.lock);
    svcmgr_service_t *s = svcmgr_find_internal(name);
    if (!s) { spinlock_unlock(&g_svc.lock); return -2; }
    s->enabled = (uint8_t)enabled;
    spinlock_unlock(&g_svc.lock);
    return 0;
}

int svcmgr_add_dependency(const char *name, const char *depends_on) {
    if (!name || !depends_on) return -1;
    spinlock_lock(&g_svc.lock);
    svcmgr_service_t *s = svcmgr_find_internal(name);
    if (!s) { spinlock_unlock(&g_svc.lock); return -2; }
    if (s->dep_count >= SVCMGR_MAX_DEPS) { spinlock_unlock(&g_svc.lock); return -3; }
    /* 避免重复 */
    for (uint8_t i = 0; i < s->dep_count; i++) {
        if (strcmp(s->dependencies[i], depends_on) == 0) {
            spinlock_unlock(&g_svc.lock);
            return 0;
        }
    }
    strncpy(s->dependencies[s->dep_count], depends_on, SVCMGR_MAX_DEP_NAME - 1);
    s->dependencies[s->dep_count][SVCMGR_MAX_DEP_NAME - 1] = '\0';
    s->dep_count++;
    spinlock_unlock(&g_svc.lock);
    return 0;
}

int svcmgr_remove_dependency(const char *name, const char *depends_on) {
    if (!name || !depends_on) return -1;
    spinlock_lock(&g_svc.lock);
    svcmgr_service_t *s = svcmgr_find_internal(name);
    if (!s) { spinlock_unlock(&g_svc.lock); return -2; }
    for (uint8_t i = 0; i < s->dep_count; i++) {
        if (strcmp(s->dependencies[i], depends_on) == 0) {
            /* 用最后一项覆盖 */
            if (i < s->dep_count - 1) {
                strncpy(s->dependencies[i], s->dependencies[s->dep_count - 1],
                        SVCMGR_MAX_DEP_NAME - 1);
                s->dependencies[i][SVCMGR_MAX_DEP_NAME - 1] = '\0';
            }
            s->dep_count--;
            break;
        }
    }
    spinlock_unlock(&g_svc.lock);
    return 0;
}

/* ---- 生命周期 ---- */

/* 内部启动：递归启动依赖 */
static int svcmgr_start_internal(svcmgr_service_t *s) {
    if (!s || !s->enabled) return -1;
    if (s->state == SVCMGR_STATE_RUNNING) return 0;

    /* 先启动依赖 */
    for (uint8_t i = 0; i < s->dep_count; i++) {
        svcmgr_service_t *dep = svcmgr_find_internal(s->dependencies[i]);
        if (dep && dep->state != SVCMGR_STATE_RUNNING) {
            int rc = svcmgr_start_internal(dep);
            if (rc != 0) {
                evlog_err(SVCMGR_EV_SOURCE, 3,
                          "service '%s' start failed: dependency '%s' failed",
                          s->name, s->dependencies[i]);
                return -3;
            }
        }
    }

    s->state = SVCMGR_STATE_STARTING;
    s->start_tick = (uint32_t)timer_get_ticks();
    g_svc.stats.total_starts++;

    evlog_info(SVCMGR_EV_SOURCE, 4, "service '%s' starting (cmd='%s')",
               s->name, s->command);

    /* 执行命令 */
    if (s->command[0]) {
        shell_execute(s->command);
    }

    s->state = SVCMGR_STATE_RUNNING;
    s->run_count++;
    g_svc.stats.running++;

    evlog_info(SVCMGR_EV_SOURCE, 5, "service '%s' started", s->name);
    return 0;
}

int svcmgr_start(const char *name) {
    if (!name) return -1;
    spinlock_lock(&g_svc.lock);
    svcmgr_service_t *s = svcmgr_find_internal(name);
    if (!s) { spinlock_unlock(&g_svc.lock); return -2; }
    int rc = svcmgr_start_internal(s);
    spinlock_unlock(&g_svc.lock);
    return rc;
}

int svcmgr_stop(const char *name) {
    if (!name) return -1;
    spinlock_lock(&g_svc.lock);
    svcmgr_service_t *s = svcmgr_find_internal(name);
    if (!s) { spinlock_unlock(&g_svc.lock); return -2; }
    if (s->state != SVCMGR_STATE_RUNNING) {
        spinlock_unlock(&g_svc.lock);
        return -3;
    }

    s->state = SVCMGR_STATE_STOPPING;
    s->stop_tick = (uint32_t)timer_get_ticks();
    g_svc.stats.total_stops++;
    if (g_svc.stats.running > 0) g_svc.stats.running--;
    g_svc.stats.stopped++;

    /* 记录历史 */
    svcmgr_history_add(s->name, s->start_tick, s->stop_tick, s->last_exit_code,
                       SVCMGR_STATE_STOPPED);

    s->state = SVCMGR_STATE_STOPPED;
    spinlock_unlock(&g_svc.lock);

    evlog_info(SVCMGR_EV_SOURCE, 6, "service '%s' stopped", name);
    return 0;
}

int svcmgr_restart(const char *name) {
    if (!name) return -1;
    spinlock_lock(&g_svc.lock);
    svcmgr_service_t *s = svcmgr_find_internal(name);
    if (!s) { spinlock_unlock(&g_svc.lock); return -2; }
    spinlock_unlock(&g_svc.lock);

    if (s->state == SVCMGR_STATE_RUNNING) {
        svcmgr_stop(name);
    }

    spinlock_lock(&g_svc.lock);
    s->restart_count++;
    g_svc.stats.total_restarts++;
    spinlock_unlock(&g_svc.lock);

    return svcmgr_start(name);
}

int svcmgr_start_all(void) {
    int started = 0;
    spinlock_lock(&g_svc.lock);
    for (uint32_t i = 0; i < g_svc.service_count; i++) {
        svcmgr_service_t *s = &g_svc.services[i];
        if (s->autostart && s->enabled && s->state == SVCMGR_STATE_STOPPED) {
            /* 释放锁以避免递归死锁 */
            spinlock_unlock(&g_svc.lock);
            if (svcmgr_start(s->name) == 0) started++;
            spinlock_lock(&g_svc.lock);
        }
    }
    spinlock_unlock(&g_svc.lock);

    evlog_info(SVCMGR_EV_SOURCE, 7, "start_all: started %d autostart services", started);
    return started;
}

int svcmgr_stop_all(void) {
    int stopped = 0;
    spinlock_lock(&g_svc.lock);
    for (uint32_t i = 0; i < g_svc.service_count; i++) {
        svcmgr_service_t *s = &g_svc.services[i];
        if (s->state == SVCMGR_STATE_RUNNING) {
            s->state = SVCMGR_STATE_STOPPING;
            s->stop_tick = (uint32_t)timer_get_ticks();
            g_svc.stats.total_stops++;
            if (g_svc.stats.running > 0) g_svc.stats.running--;
            g_svc.stats.stopped++;
            svcmgr_history_add(s->name, s->start_tick, s->stop_tick,
                                s->last_exit_code, SVCMGR_STATE_STOPPED);
            s->state = SVCMGR_STATE_STOPPED;
            stopped++;
        }
    }
    spinlock_unlock(&g_svc.lock);

    evlog_info(SVCMGR_EV_SOURCE, 8, "stop_all: stopped %d services", stopped);
    return stopped;
}

/* ---- 查询 ---- */

svcmgr_service_t *svcmgr_find(const char *name) {
    if (!name) return NULL;
    spinlock_lock(&g_svc.lock);
    svcmgr_service_t *s = svcmgr_find_internal(name);
    spinlock_unlock(&g_svc.lock);
    return s;
}

uint32_t svcmgr_list(svcmgr_service_t *out, uint32_t max_count) {
    if (!out) return 0;
    spinlock_lock(&g_svc.lock);
    uint32_t n = g_svc.service_count;
    if (n > max_count) n = max_count;
    for (uint32_t i = 0; i < n; i++) {
        out[i] = g_svc.services[i];
    }
    spinlock_unlock(&g_svc.lock);
    return n;
}

uint32_t svcmgr_history_list(svcmgr_history_t *out, uint32_t max_count) {
    if (!out) return 0;
    spinlock_lock(&g_svc.lock);
    uint32_t n = 0;
    /* 从最新到最旧读取环形缓冲 */
    for (uint32_t i = 0; i < SVCMGR_HISTORY_SIZE && n < max_count; i++) {
        uint32_t idx = (g_svc.history_head + SVCMGR_HISTORY_SIZE - 1 - i) % SVCMGR_HISTORY_SIZE;
        svcmgr_history_t *h = &g_svc.history[idx];
        if (!h->service_name[0]) continue;
        out[n++] = *h;
    }
    spinlock_unlock(&g_svc.lock);
    return n;
}

/* ---- 统计 ---- */

void svcmgr_get_stats(svcmgr_stats_t *stats) {
    if (!stats) return;
    spinlock_lock(&g_svc.lock);
    /* 重新计算运行/停止/失败计数 */
    g_svc.stats.total_services = g_svc.service_count;
    uint32_t running = 0, stopped = 0, failed = 0;
    for (uint32_t i = 0; i < g_svc.service_count; i++) {
        switch (g_svc.services[i].state) {
            case SVCMGR_STATE_RUNNING:  running++; break;
            case SVCMGR_STATE_STOPPED:  stopped++; break;
            case SVCMGR_STATE_FAILED:   failed++; break;
            default: break;
        }
    }
    g_svc.stats.running = running;
    g_svc.stats.stopped = stopped;
    g_svc.stats.failed = failed;
    *stats = g_svc.stats;
    spinlock_unlock(&g_svc.lock);
}

void svcmgr_reset_stats(void) {
    spinlock_lock(&g_svc.lock);
    /* 保留 total_services / running 等动态计数，重置累计计数 */
    g_svc.stats.total_starts = 0;
    g_svc.stats.total_stops = 0;
    g_svc.stats.total_restarts = 0;
    g_svc.stats.total_failures = 0;
    spinlock_unlock(&g_svc.lock);
}

/* ---- 名称辅助 ---- */

const char *svcmgr_state_name(svcmgr_state_t state) {
    switch (state) {
        case SVCMGR_STATE_STOPPED:  return "STOPPED";
        case SVCMGR_STATE_STARTING: return "STARTING";
        case SVCMGR_STATE_RUNNING:  return "RUNNING";
        case SVCMGR_STATE_STOPPING: return "STOPPING";
        case SVCMGR_STATE_FAILED:   return "FAILED";
        default: return "UNKNOWN";
    }
}

const char *svcmgr_restart_policy_name(svcmgr_restart_policy_t policy) {
    switch (policy) {
        case SVCMGR_RESTART_NEVER:      return "NEVER";
        case SVCMGR_RESTART_ON_FAILURE: return "ON_FAILURE";
        case SVCMGR_RESTART_ALWAYS:     return "ALWAYS";
        default: return "UNKNOWN";
    }
}

/* ---- 状态更新 ---- */

int svcmgr_mark_started(const char *name, int success) {
    if (!name) return -1;
    spinlock_lock(&g_svc.lock);
    svcmgr_service_t *s = svcmgr_find_internal(name);
    if (!s) { spinlock_unlock(&g_svc.lock); return -2; }
    if (success) {
        s->state = SVCMGR_STATE_RUNNING;
        s->run_count++;
        g_svc.stats.running++;
    } else {
        s->state = SVCMGR_STATE_FAILED;
        s->failure_count++;
        g_svc.stats.total_failures++;
        g_svc.stats.failed++;

        /* 应用重启策略 */
        if (s->restart_policy == SVCMGR_RESTART_ON_FAILURE ||
            s->restart_policy == SVCMGR_RESTART_ALWAYS) {
            s->restart_count++;
            g_svc.stats.total_restarts++;
            evlog_warn(SVCMGR_EV_SOURCE, 9,
                       "service '%s' failed, scheduling restart (policy=%s count=%u)",
                       name, svcmgr_restart_policy_name(s->restart_policy), s->restart_count);
            /* 注意：实际重启需要异步执行，这里只标记 */
        }
    }
    spinlock_unlock(&g_svc.lock);
    return 0;
}

int svcmgr_mark_stopped(const char *name, int exit_code) {
    if (!name) return -1;
    spinlock_lock(&g_svc.lock);
    svcmgr_service_t *s = svcmgr_find_internal(name);
    if (!s) { spinlock_unlock(&g_svc.lock); return -2; }
    s->last_exit_code = exit_code;
    s->stop_tick = (uint32_t)timer_get_ticks();
    svcmgr_history_add(s->name, s->start_tick, s->stop_tick, exit_code,
                       exit_code == 0 ? SVCMGR_STATE_STOPPED : SVCMGR_STATE_FAILED);
    if (s->state == SVCMGR_STATE_RUNNING && g_svc.stats.running > 0) {
        g_svc.stats.running--;
    }
    s->state = (exit_code == 0) ? SVCMGR_STATE_STOPPED : SVCMGR_STATE_FAILED;
    if (exit_code != 0) {
        g_svc.stats.total_failures++;
        g_svc.stats.failed++;
    } else {
        g_svc.stats.stopped++;
    }
    spinlock_unlock(&g_svc.lock);
    return 0;
}
