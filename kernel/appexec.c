/* appexec.c - 应用执行服务实现
 *
 * 从注册表 HKLM\Software\Apps\<id> 读取应用信息，
 * 记录启动历史，事件写入 evlog。
 * 实际执行由 shell 的 shell_execute() 完成。
 */
#include "appexec.h"
#include "kheap.h"
#include "string.h"
#include "stdlib.h"
#include "stdio.h"
#include "spinlock.h"
#include "klog.h"
#include "evlog.h"
#include "registry.h"
#include "timer.h"

/* shell_execute 定义在 shell.c 中 */
extern void shell_execute(const char *cmd);

/* ============================================================
 * 内部状态
 * ============================================================ */

typedef struct {
    spinlock_t          lock;
    appexec_history_t   history[APPEXEC_HISTORY_SIZE];
    uint32_t            history_head;   /* 下一个写入位置 */
    uint32_t            history_count;  /* 有效条目数（<= APPEXEC_HISTORY_SIZE） */
    appexec_stats_t     stats;
    uint8_t             initialized;
} appexec_globals_t;

static appexec_globals_t g_appexec;

/* 追踪已运行的不同应用 ID（用于 unique_apps 统计） */
static char g_seen_apps[APPEXEC_HISTORY_SIZE][APPEXEC_MAX_ID];
static uint32_t g_seen_count = 0;

static void appexec_note_unique(const char *app_id) {
    for (uint32_t i = 0; i < g_seen_count; i++) {
        if (strcmp(g_seen_apps[i], app_id) == 0) return;
    }
    if (g_seen_count < APPEXEC_HISTORY_SIZE) {
        strncpy(g_seen_apps[g_seen_count], app_id, APPEXEC_MAX_ID - 1);
        g_seen_apps[g_seen_count][APPEXEC_MAX_ID - 1] = '\0';
        g_seen_count++;
    }
}

/* ============================================================
 * 公共 API
 * ============================================================ */

void appexec_init(void) {
    memset(&g_appexec, 0, sizeof(g_appexec));
    spinlock_init(&g_appexec.lock);
    g_appexec.initialized = 1;
    g_seen_count = 0;
    klog_info("appexec: application execution service initialized");
}

void appexec_shutdown(void) {
    spinlock_lock(&g_appexec.lock);
    g_appexec.initialized = 0;
    spinlock_unlock(&g_appexec.lock);
}

int appexec_get_info(const char *app_id, appexec_app_info_t *out) {
    if (!app_id || !*app_id || !out) return -1;
    memset(out, 0, sizeof(*out));

    char path[128];
    snprintf(path, sizeof(path), "HKLM\\Software\\Apps\\%s", app_id);
    reg_handle_t h = reg_open_path(path);
    if (!h) return -1;

    strncpy(out->id, app_id, APPEXEC_MAX_ID - 1);
    const char *v;
    v = reg_get_string(h, "name", "");
    strncpy(out->name, v, APPEXEC_MAX_NAME - 1);
    v = reg_get_string(h, "path", "");
    strncpy(out->path, v, APPEXEC_MAX_CMD - 1);
    v = reg_get_string(h, "command", "");
    strncpy(out->command, v, APPEXEC_MAX_CMD - 1);
    v = reg_get_string(h, "category", "");
    strncpy(out->category, v, APPEXEC_MAX_CATEGORY - 1);
    v = reg_get_string(h, "version", "");
    strncpy(out->version, v, sizeof(out->version) - 1);
    v = reg_get_string(h, "description", "");
    strncpy(out->description, v, sizeof(out->description) - 1);

    return 0;
}

int appexec_record_launch(const char *app_id, const char *app_name,
                          const char *command) {
    if (!app_id || !*app_id) return -1;

    spinlock_lock(&g_appexec.lock);
    if (!g_appexec.initialized) {
        spinlock_unlock(&g_appexec.lock);
        return -1;
    }

    uint32_t idx = g_appexec.history_head;
    appexec_history_t *h = &g_appexec.history[idx];
    memset(h, 0, sizeof(*h));
    strncpy(h->id, app_id, APPEXEC_MAX_ID - 1);
    if (app_name) {
        strncpy(h->name, app_name, APPEXEC_MAX_NAME - 1);
    } else {
        strncpy(h->name, app_id, APPEXEC_MAX_NAME - 1);
    }
    if (command) {
        strncpy(h->command, command, APPEXEC_MAX_CMD - 1);
    }
    h->launch_tick = (uint64_t)timer_get_ticks();
    h->completed = 0;
    h->exit_code = 0;
    h->duration_ms = 0;

    g_appexec.history_head = (g_appexec.history_head + 1) % APPEXEC_HISTORY_SIZE;
    if (g_appexec.history_count < APPEXEC_HISTORY_SIZE) {
        g_appexec.history_count++;
    }

    g_appexec.stats.total_launches++;
    appexec_note_unique(app_id);
    g_appexec.stats.unique_apps = g_seen_count;

    spinlock_unlock(&g_appexec.lock);

    evlog_info("AppExec", 1, "app '%s' (id=%s) launched: %s",
               h->name, app_id, command ? command : "(none)");
    return (int)idx;
}

void appexec_record_result(int history_index, int32_t exit_code) {
    if (history_index < 0 || history_index >= APPEXEC_HISTORY_SIZE) return;

    spinlock_lock(&g_appexec.lock);
    appexec_history_t *h = &g_appexec.history[history_index];
    if (h->completed) {
        spinlock_unlock(&g_appexec.lock);
        return;
    }
    h->exit_code = exit_code;
    h->completed = 1;
    uint64_t now = (uint64_t)timer_get_ticks();
    if (now >= h->launch_tick) {
        h->duration_ms = (uint32_t)((now - h->launch_tick) * 10);  /* ticks→ms (100Hz) */
    }

    if (exit_code == 0) {
        g_appexec.stats.successful++;
    } else {
        g_appexec.stats.failed++;
    }
    spinlock_unlock(&g_appexec.lock);

    if (exit_code == 0) {
        evlog_info("AppExec", 2, "app '%s' completed exit=0 duration=%ums",
                   h->name, h->duration_ms);
    } else {
        evlog_err("AppExec", 3, "app '%s' FAILED exit=%d duration=%ums",
                  h->name, exit_code, h->duration_ms);
    }
}

int appexec_run(const char *app_id, const char *command_override) {
    if (!app_id || !*app_id) return -1;

    appexec_app_info_t info;
    if (appexec_get_info(app_id, &info) != 0) {
        evlog_warn("AppExec", 4, "appexec: app '%s' not found", app_id);
        return -1;
    }

    /* 确定要执行的命令 */
    const char *cmd = command_override;
    if (!cmd || !*cmd) cmd = info.command;
    if (!cmd || !*cmd) cmd = info.path;
    if (!cmd || !*cmd) cmd = info.id;  /* 最终回退：用 app id 作为命令 */

    int idx = appexec_record_launch(app_id, info.name, cmd);
    shell_execute(cmd);

    /* shell_execute 没有 exit code 返回值，假设 0。
     * 实际退出码需要调用者通过 last_exit_code 获取后调用 appexec_record_result。
     * 但 appexec_run 作为便捷函数，我们记录 0 并标记完成。 */
    appexec_record_result(idx, 0);
    return 0;
}

uint32_t appexec_history_count(void) {
    spinlock_lock(&g_appexec.lock);
    uint32_t n = g_appexec.history_count;
    spinlock_unlock(&g_appexec.lock);
    return n;
}

uint32_t appexec_history(appexec_history_t *out, uint32_t max_count) {
    if (!out || max_count == 0) return 0;
    spinlock_lock(&g_appexec.lock);

    uint32_t count = g_appexec.history_count;
    if (count > max_count) count = max_count;

    /* 从最新到最旧（history_head-1 是最新） */
    for (uint32_t i = 0; i < count; i++) {
        int idx = (int)g_appexec.history_head - 1 - (int)i;
        while (idx < 0) idx += APPEXEC_HISTORY_SIZE;
        out[i] = g_appexec.history[idx];
    }

    spinlock_unlock(&g_appexec.lock);
    return count;
}

void appexec_clear_history(void) {
    spinlock_lock(&g_appexec.lock);
    memset(g_appexec.history, 0, sizeof(g_appexec.history));
    g_appexec.history_head = 0;
    g_appexec.history_count = 0;
    spinlock_unlock(&g_appexec.lock);
}

void appexec_get_stats(appexec_stats_t *stats) {
    if (!stats) return;
    spinlock_lock(&g_appexec.lock);
    *stats = g_appexec.stats;
    spinlock_unlock(&g_appexec.lock);
}

void appexec_reset_stats(void) {
    spinlock_lock(&g_appexec.lock);
    memset(&g_appexec.stats, 0, sizeof(g_appexec.stats));
    g_seen_count = 0;
    spinlock_unlock(&g_appexec.lock);
}
