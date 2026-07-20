/* quota_db.c - 配额子系统 FunDB 持久化实现 */
#include "quota_db.h"
#include "evlog.h"
#include "fundb.h"
#include "klog.h"
#include "spinlock.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "timer.h"

#define QUOTA_EV_SOURCE "Quota"

static struct {
    spinlock_t      lock;
    fundb_handle_t  db;
    int             initialized;
} g_qdb;

static void quota_db_create_table(const char *table_name) {
    if (!g_qdb.db) return;
    if (fundb_table_exists(g_qdb.db, table_name)) return;

    fundb_column_t cols[10];
    memset(cols, 0, sizeof(cols));

    strcpy(cols[0].name, "id");
    cols[0].type = FUNDB_TYPE_INT; cols[0].size = 4;
    cols[0].not_null = 1; cols[0].primary_key = 1;

    strcpy(cols[1].name, "flags");
    cols[1].type = FUNDB_TYPE_INT; cols[1].size = 4;

    strcpy(cols[2].name, "blocks_used");
    cols[2].type = FUNDB_TYPE_INT; cols[2].size = 4;

    strcpy(cols[3].name, "blocks_soft");
    cols[3].type = FUNDB_TYPE_INT; cols[3].size = 4;

    strcpy(cols[4].name, "blocks_hard");
    cols[4].type = FUNDB_TYPE_INT; cols[4].size = 4;

    strcpy(cols[5].name, "inodes_used");
    cols[5].type = FUNDB_TYPE_INT; cols[5].size = 4;

    strcpy(cols[6].name, "inodes_soft");
    cols[6].type = FUNDB_TYPE_INT; cols[6].size = 4;

    strcpy(cols[7].name, "inodes_hard");
    cols[7].type = FUNDB_TYPE_INT; cols[7].size = 4;

    strcpy(cols[8].name, "btime");
    cols[8].type = FUNDB_TYPE_INT; cols[8].size = 4;

    strcpy(cols[9].name, "itime");
    cols[9].type = FUNDB_TYPE_INT; cols[9].size = 4;

    int rc = fundb_create_table(g_qdb.db, table_name, cols, 10);
    if (rc != FUNDB_OK) {
        klog_warn("quota_db: failed to create %s (%s)",
                  table_name, fundb_error_string(rc));
    }
}

/* 写入条目（先删后插） */
static int quota_db_write(const char *table, const quota_entry_t *e) {
    if (!g_qdb.db || !e) return -1;

    char where[64];
    snprintf(where, sizeof(where), "id = %u", e->id);
    fundb_delete(g_qdb.db, table, where);

    uint32_t flags = e->flags;
    uint32_t bused = (uint32_t)e->blocks_used;
    uint32_t bsoft = (uint32_t)e->blocks_soft;
    uint32_t bhard = (uint32_t)e->blocks_hard;
    uint32_t iused = (uint32_t)e->inodes_used;
    uint32_t isoft = (uint32_t)e->inodes_soft;
    uint32_t ihard = (uint32_t)e->inodes_hard;
    uint32_t btime = e->btime;
    uint32_t itime = e->itime;

    void *vals[10];
    uint32_t sizes[10];
    uint32_t types[10];

    vals[0] = (void *)&e->id; sizes[0] = 4; types[0] = FUNDB_TYPE_INT;
    vals[1] = &flags;          sizes[1] = 4; types[1] = FUNDB_TYPE_INT;
    vals[2] = &bused;          sizes[2] = 4; types[2] = FUNDB_TYPE_INT;
    vals[3] = &bsoft;          sizes[3] = 4; types[3] = FUNDB_TYPE_INT;
    vals[4] = &bhard;          sizes[4] = 4; types[4] = FUNDB_TYPE_INT;
    vals[5] = &iused;          sizes[5] = 4; types[5] = FUNDB_TYPE_INT;
    vals[6] = &isoft;          sizes[6] = 4; types[6] = FUNDB_TYPE_INT;
    vals[7] = &ihard;          sizes[7] = 4; types[7] = FUNDB_TYPE_INT;
    vals[8] = &btime;          sizes[8] = 4; types[8] = FUNDB_TYPE_INT;
    vals[9] = &itime;          sizes[9] = 4; types[9] = FUNDB_TYPE_INT;

    fundb_row_t row;
    row.values = vals; row.sizes = sizes; row.types = types;
    return fundb_insert(g_qdb.db, table, &row);
}

/* 从 FunDB 行读取 */
static void quota_db_row_to_entry(const fundb_row_t *row, quota_entry_t *e) {
    if (!row || !e) return;
    memset(e, 0, sizeof(*e));
    if (row->values[0]) e->id = *(uint32_t *)row->values[0];
    if (row->values[1]) e->flags = *(uint8_t *)row->values[1];
    if (row->values[2]) e->blocks_used = *(uint32_t *)row->values[2];
    if (row->values[3]) e->blocks_soft = *(uint32_t *)row->values[3];
    if (row->values[4]) e->blocks_hard = *(uint32_t *)row->values[4];
    if (row->values[5]) e->inodes_used = *(uint32_t *)row->values[5];
    if (row->values[6]) e->inodes_soft = *(uint32_t *)row->values[6];
    if (row->values[7]) e->inodes_hard = *(uint32_t *)row->values[7];
    if (row->values[8]) e->btime = *(uint32_t *)row->values[8];
    if (row->values[9]) e->itime = *(uint32_t *)row->values[9];
}

void quota_db_init(void) {
    memset(&g_qdb, 0, sizeof(g_qdb));
    spinlock_init(&g_qdb.lock);

    g_qdb.db = fundb_open(QUOTA_DB_PATH);
    if (!g_qdb.db) {
        klog_warn("quota_db: failed to open %s (persistence disabled)",
                  QUOTA_DB_PATH);
    }
    quota_db_create_table(QUOTA_TABLE_USER);
    quota_db_create_table(QUOTA_TABLE_GROUP);

    evlog_register_source(QUOTA_EV_SOURCE);
    g_qdb.initialized = 1;
    klog_info("quota_db: quota persistence initialized (db=%s)",
              g_qdb.db ? QUOTA_DB_PATH : "<none>");
}

void quota_db_shutdown(void) {
    if (!g_qdb.initialized) return;
    if (g_qdb.db) {
        fundb_close(g_qdb.db);
        g_qdb.db = NULL;
    }
    g_qdb.initialized = 0;
}

int quota_db_save_user(const quota_entry_t *e) {
    if (!e) return -1;
    spinlock_lock(&g_qdb.lock);
    int rc = quota_db_write(QUOTA_TABLE_USER, e);
    spinlock_unlock(&g_qdb.lock);
    if (rc == FUNDB_OK) {
        evlog_info(QUOTA_EV_SOURCE, 1, "user quota saved uid=%u", e->id);
    }
    return rc == FUNDB_OK ? 0 : -2;
}

int quota_db_save_group(const quota_entry_t *e) {
    if (!e) return -1;
    spinlock_lock(&g_qdb.lock);
    int rc = quota_db_write(QUOTA_TABLE_GROUP, e);
    spinlock_unlock(&g_qdb.lock);
    if (rc == FUNDB_OK) {
        evlog_info(QUOTA_EV_SOURCE, 2, "group quota saved gid=%u", e->id);
    }
    return rc == FUNDB_OK ? 0 : -2;
}

int quota_db_delete_user(uint32_t uid) {
    if (!g_qdb.db) return -1;
    spinlock_lock(&g_qdb.lock);
    char where[64];
    snprintf(where, sizeof(where), "id = %u", uid);
    int rc = fundb_delete(g_qdb.db, QUOTA_TABLE_USER, where);
    spinlock_unlock(&g_qdb.lock);
    if (rc == FUNDB_OK) {
        evlog_info(QUOTA_EV_SOURCE, 3, "user quota deleted uid=%u", uid);
    }
    return rc == FUNDB_OK ? 0 : -2;
}

int quota_db_delete_group(uint32_t gid) {
    if (!g_qdb.db) return -1;
    spinlock_lock(&g_qdb.lock);
    char where[64];
    snprintf(where, sizeof(where), "id = %u", gid);
    int rc = fundb_delete(g_qdb.db, QUOTA_TABLE_GROUP, where);
    spinlock_unlock(&g_qdb.lock);
    if (rc == FUNDB_OK) {
        evlog_info(QUOTA_EV_SOURCE, 4, "group quota deleted gid=%u", gid);
    }
    return rc == FUNDB_OK ? 0 : -2;
}

int quota_db_load_all(void) {
    if (!g_qdb.db) return -1;
    int loaded = 0;

    /* 加载用户配额 */
    fundb_result_t *r = fundb_select(g_qdb.db, QUOTA_TABLE_USER, "*", NULL, NULL, 0);
    if (r) {
        for (uint32_t i = 0; i < r->row_count; i++) {
            quota_entry_t e;
            quota_db_row_to_entry(&r->rows[i], &e);
            e.type = QUOTA_TYPE_USER;
            /* 调用 quota.c 的 set_user 加载到内存 */
            if (quota_set_user(e.id, e.blocks_soft, e.blocks_hard,
                               e.inodes_soft, e.inodes_hard) == 0) {
                /* 重新读取以更新使用量 */
                quota_entry_t *mem = quota_get_user(e.id);
                if (mem) {
                    mem->blocks_used = e.blocks_used;
                    mem->inodes_used = e.inodes_used;
                    mem->flags = e.flags;
                    mem->btime = e.btime;
                    mem->itime = e.itime;
                }
                loaded++;
            }
        }
        fundb_free_result(r);
    }

    /* 加载组配额 */
    r = fundb_select(g_qdb.db, QUOTA_TABLE_GROUP, "*", NULL, NULL, 0);
    if (r) {
        for (uint32_t i = 0; i < r->row_count; i++) {
            quota_entry_t e;
            quota_db_row_to_entry(&r->rows[i], &e);
            e.type = QUOTA_TYPE_GROUP;
            if (quota_set_group(e.id, e.blocks_soft, e.blocks_hard,
                                e.inodes_soft, e.inodes_hard) == 0) {
                quota_entry_t *mem = quota_get_group(e.id);
                if (mem) {
                    mem->blocks_used = e.blocks_used;
                    mem->inodes_used = e.inodes_used;
                    mem->flags = e.flags;
                    mem->btime = e.btime;
                    mem->itime = e.itime;
                }
                loaded++;
            }
        }
        fundb_free_result(r);
    }

    evlog_info(QUOTA_EV_SOURCE, 5, "loaded %d quota entries from FunDB", loaded);
    klog_info("quota_db: loaded %d entries from FunDB", loaded);
    return loaded;
}

int quota_db_sync(void) {
    if (!g_qdb.db) return -1;
    int synced = 0;

    /* 同步用户配额 */
    quota_entry_t entries[64];
    int n = quota_list(entries, 64, QUOTA_TYPE_USER);
    for (int i = 0; i < n; i++) {
        if (quota_db_write(QUOTA_TABLE_USER, &entries[i]) == FUNDB_OK) {
            synced++;
        }
    }

    /* 同步组配额 */
    n = quota_list(entries, 64, QUOTA_TYPE_GROUP);
    for (int i = 0; i < n; i++) {
        if (quota_db_write(QUOTA_TABLE_GROUP, &entries[i]) == FUNDB_OK) {
            synced++;
        }
    }

    evlog_info(QUOTA_EV_SOURCE, 6, "synced %d quota entries to FunDB", synced);
    return synced;
}

int quota_db_query_user(uint32_t uid, quota_entry_t *out) {
    if (!g_qdb.db || !out) return -1;
    char where[64];
    snprintf(where, sizeof(where), "id = %u", uid);
    fundb_result_t *r = fundb_select(g_qdb.db, QUOTA_TABLE_USER, "*", where, NULL, 1);
    if (!r || r->row_count == 0) {
        if (r) fundb_free_result(r);
        return -2;
    }
    quota_db_row_to_entry(&r->rows[0], out);
    out->type = QUOTA_TYPE_USER;
    fundb_free_result(r);
    return 0;
}

int quota_db_query_group(uint32_t gid, quota_entry_t *out) {
    if (!g_qdb.db || !out) return -1;
    char where[64];
    snprintf(where, sizeof(where), "id = %u", gid);
    fundb_result_t *r = fundb_select(g_qdb.db, QUOTA_TABLE_GROUP, "*", where, NULL, 1);
    if (!r || r->row_count == 0) {
        if (r) fundb_free_result(r);
        return -2;
    }
    quota_db_row_to_entry(&r->rows[0], out);
    out->type = QUOTA_TYPE_GROUP;
    fundb_free_result(r);
    return 0;
}
