/* signal_diag.c - 信号诊断子系统实现 */
#include "signal_diag.h"
#include "evlog.h"
#include "fundb.h"
#include "klog.h"
#include "spinlock.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "timer.h"

#define SIGDIAG_EV_SOURCE "Signal"

static struct {
    spinlock_t        lock;
    fundb_handle_t    db;
    int               initialized;
    uint32_t          next_id;
    sigdiag_stat_t    stats;
    sigdiag_record_t  history[SIGDIAG_HISTORY_SIZE];
    uint32_t          history_head;
} g_sig;

static const char *signal_names[NSIG] = {
    "SIGNAL_0", "SIGHUP", "SIGINT", "SIGQUIT", "SIGILL", "SIGTRAP",
    "SIGABRT", "SIGBUS", "SIGFPE", "SIGKILL", "SIGUSR1", "SIGSEGV",
    "SIGUSR2", "SIGPIPE", "SIGALRM", "SIGTERM", "SIGSTKFLT", "SIGCHLD",
    "SIGCONT", "SIGSTOP", "SIGTSTP", "SIGTTIN", "SIGTTOU", "SIGURG",
    "SIGXCPU", "SIGXFSZ", "SIGVTALRM", "SIGPROF", "SIGWINCH", "SIGIO",
    "SIGPWR", "SIGSYS"
};

static void sigdiag_db_init_table(void) {
    if (!g_sig.db) return;
    if (!fundb_table_exists(g_sig.db, SIGDIAG_TABLE_LOG)) {
        fundb_column_t cols[6];
        memset(cols, 0, sizeof(cols));

        strcpy(cols[0].name, "id");
        cols[0].type = FUNDB_TYPE_INT; cols[0].size = 4;
        cols[0].not_null = 1; cols[0].primary_key = 1;

        strcpy(cols[1].name, "sender");
        cols[1].type = FUNDB_TYPE_INT; cols[1].size = 4;

        strcpy(cols[2].name, "target");
        cols[2].type = FUNDB_TYPE_INT; cols[2].size = 4;

        strcpy(cols[3].name, "signo");
        cols[3].type = FUNDB_TYPE_INT; cols[3].size = 4;

        strcpy(cols[4].name, "timestamp");
        cols[4].type = FUNDB_TYPE_INT; cols[4].size = 4;

        strcpy(cols[5].name, "reason");
        cols[5].type = FUNDB_TYPE_TEXT; cols[5].size = 32;

        int rc = fundb_create_table(g_sig.db, SIGDIAG_TABLE_LOG, cols, 6);
        if (rc != FUNDB_OK) {
            klog_warn("sigdiag: failed to create %s (%s)",
                      SIGDIAG_TABLE_LOG, fundb_error_string(rc));
        }
    }
}

static void sigdiag_db_insert_record(const sigdiag_record_t *r) {
    if (!g_sig.db || !r) return;
    void *vals[6];
    uint32_t sizes[6];
    uint32_t types[6];

    uint32_t sender = (uint32_t)r->sender;
    uint32_t target = (uint32_t)r->target;
    uint32_t signo = (uint32_t)r->signo;
    uint32_t ts = r->timestamp;

    vals[0] = (void *)&r->id;       sizes[0] = 4; types[0] = FUNDB_TYPE_INT;
    vals[1] = &sender;              sizes[1] = 4; types[1] = FUNDB_TYPE_INT;
    vals[2] = &target;              sizes[2] = 4; types[2] = FUNDB_TYPE_INT;
    vals[3] = &signo;               sizes[3] = 4; types[3] = FUNDB_TYPE_INT;
    vals[4] = &ts;                   sizes[4] = 4; types[4] = FUNDB_TYPE_INT;
    vals[5] = (void *)r->reason;    sizes[5] = (uint32_t)strlen(r->reason) + 1; types[5] = FUNDB_TYPE_TEXT;

    fundb_row_t row;
    row.values = vals; row.sizes = sizes; row.types = types;
    fundb_insert(g_sig.db, SIGDIAG_TABLE_LOG, &row);
}

void sigdiag_init(void) {
    memset(&g_sig, 0, sizeof(g_sig));
    spinlock_init(&g_sig.lock);
    g_sig.next_id = 1;

    g_sig.db = fundb_open(SIGDIAG_DB_PATH);
    if (!g_sig.db) {
        klog_warn("sigdiag: failed to open %s (persistence disabled)",
                  SIGDIAG_DB_PATH);
    }
    sigdiag_db_init_table();

    evlog_register_source(SIGDIAG_EV_SOURCE);
    g_sig.initialized = 1;
    klog_info("sigdiag: signal diagnostics initialized");
}

void sigdiag_shutdown(void) {
    if (!g_sig.initialized) return;
    if (g_sig.db) {
        fundb_close(g_sig.db);
        g_sig.db = NULL;
    }
    g_sig.initialized = 0;
}

void sigdiag_record_send(pid_t sender, pid_t target, int signo,
                          int result, const char *reason) {
    if (signo < 0 || signo >= NSIG) return;

    spinlock_lock(&g_sig.lock);
    uint32_t id = g_sig.next_id++;
    g_sig.stats.sends[signo]++;
    g_sig.stats.total_sends++;
    if (result != 0) g_sig.stats.drops[signo]++, g_sig.stats.total_drops++;

    /* 写入历史 */
    sigdiag_record_t *r = &g_sig.history[g_sig.history_head];
    memset(r, 0, sizeof(*r));
    r->id = id;
    r->sender = sender;
    r->target = target;
    r->signo = signo;
    r->result = result;
    r->timestamp = (uint32_t)timer_get_ticks();
    if (reason) {
        strncpy(r->reason, reason, sizeof(r->reason) - 1);
    } else {
        strncpy(r->reason, "unknown", sizeof(r->reason) - 1);
    }
    g_sig.history_head = (g_sig.history_head + 1) % SIGDIAG_HISTORY_SIZE;

    /* 持久化 */
    sigdiag_db_insert_record(r);
    spinlock_unlock(&g_sig.lock);

    /* evlog */
    if (result == 0) {
        evlog_info(SIGDIAG_EV_SOURCE, 1,
                   "signal sent: %s sender=%d target=%d reason=%s",
                   sigdiag_signal_name(signo), sender, target,
                   reason ? reason : "unknown");
    } else {
        evlog_warn(SIGDIAG_EV_SOURCE, 2,
                   "signal send FAILED: %s sender=%d target=%d reason=%s",
                   sigdiag_signal_name(signo), sender, target,
                   reason ? reason : "unknown");
    }
}

void sigdiag_record_deliver(pid_t target, int signo, int handler_kind) {
    if (signo < 0 || signo >= NSIG) return;
    spinlock_lock(&g_sig.lock);
    g_sig.stats.delivers[signo]++;
    g_sig.stats.total_delivers++;
    switch (handler_kind) {
        case 0: g_sig.stats.default_actions[signo]++; break;
        case 1: g_sig.stats.ignored[signo]++; break;
        case 2: g_sig.stats.user_handlers[signo]++; break;
    }
    spinlock_unlock(&g_sig.lock);
}

void sigdiag_get_stats(sigdiag_stat_t *stats) {
    if (!stats) return;
    spinlock_lock(&g_sig.lock);
    *stats = g_sig.stats;
    spinlock_unlock(&g_sig.lock);
}

void sigdiag_reset_stats(void) {
    spinlock_lock(&g_sig.lock);
    memset(&g_sig.stats, 0, sizeof(g_sig.stats));
    spinlock_unlock(&g_sig.lock);
}

uint32_t sigdiag_history(sigdiag_record_t *out, uint32_t max_count) {
    if (!out) return 0;
    spinlock_lock(&g_sig.lock);
    uint32_t n = 0;
    for (uint32_t i = 0; i < SIGDIAG_HISTORY_SIZE && n < max_count; i++) {
        uint32_t idx = (g_sig.history_head + SIGDIAG_HISTORY_SIZE - 1 - i)
                       % SIGDIAG_HISTORY_SIZE;
        sigdiag_record_t *r = &g_sig.history[idx];
        if (!r->reason[0] && r->id == 0) continue;
        out[n++] = *r;
    }
    spinlock_unlock(&g_sig.lock);
    return n;
}

const char *sigdiag_signal_name(int signo) {
    if (signo < 0 || signo >= NSIG) return "UNKNOWN";
    return signal_names[signo];
}
