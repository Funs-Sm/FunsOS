/* fundb.h - FUNSOS 数据库引擎
 * 嵌入式 SQL 数据库，支持 B-tree 索引、WAL 事务和 SQL 解析
 */

#ifndef FUNDB_H
#define FUNDB_H

#include "stdint.h"

/* 数据类型 */
#define FUNDB_TYPE_INT     1
#define FUNDB_TYPE_FLOAT   2
#define FUNDB_TYPE_TEXT    3
#define FUNDB_TYPE_BLOB    4
#define FUNDB_TYPE_BOOL    5
#define FUNDB_TYPE_DATE    6
#define FUNDB_TYPE_NULL    7

/* 错误码 */
#define FUNDB_OK           0
#define FUNDB_ERROR       -1
#define FUNDB_NO_TABLE    -2
#define FUNDB_NO_COLUMN   -3
#define FUNDB_DUP_KEY     -4
#define FUNDB_CONSTRAINT  -5
#define FUNDB_NO_MEMORY   -6
#define FUNDB_IO_ERROR    -7
#define FUNDB_CORRUPT     -8
#define FUNDB_BUSY        -9
#define FUNDB_NO_ROW      -10

/* 限制 */
#define FUNDB_MAX_COLUMNS  32
#define FUNDB_MAX_TABLES   32

/* 列定义 */
typedef struct {
    char name[64];
    uint32_t type;
    uint32_t size;           /* 最大长度 */
    uint32_t not_null;
    uint32_t primary_key;
    uint32_t auto_increment;
    uint32_t default_value;
} fundb_column_t;

/* 行数据 */
typedef struct {
    void **values;           /* 每列的值指针 */
    uint32_t *sizes;         /* 每列值的大小 */
    uint32_t *types;         /* 每列的类型 */
} fundb_row_t;

/* 结果集 */
typedef struct {
    fundb_column_t *columns;
    uint32_t col_count;
    fundb_row_t *rows;
    uint32_t row_count;
    uint32_t row_capacity;
} fundb_result_t;

/* 数据库句柄 */
typedef void *fundb_handle_t;

/* 数据库内部结构 (用于 shell 命令) */
typedef struct {
    char path[256];
    uint32_t table_count;
    char table_names[32][64];
    uint32_t table_row_counts[32];
} fundb_info_t;

/* 获取数据库信息 */
int fundb_get_info(fundb_handle_t db, fundb_info_t *info);

/* ---- 数据库操作 ---- */

/* 打开/关闭数据库 */
fundb_handle_t fundb_open(const char *path);
int fundb_close(fundb_handle_t db);

/* ---- 表操作 ---- */

/* 创建表 */
int fundb_create_table(fundb_handle_t db, const char *name,
                       fundb_column_t *columns, uint32_t col_count);

/* 删除表 */
int fundb_drop_table(fundb_handle_t db, const char *name);

/* 修改表结构 */
int fundb_alter_table(fundb_handle_t db, const char *name,
                      fundb_column_t *new_columns, uint32_t new_col_count);

/* ---- 数据操作 ---- */

/* 插入行 */
int fundb_insert(fundb_handle_t db, const char *table, fundb_row_t *row);

/* 更新行 */
int fundb_update(fundb_handle_t db, const char *table,
                 const char *where_clause, fundb_row_t *row);

/* 删除行 */
int fundb_delete(fundb_handle_t db, const char *table, const char *where_clause);

/* 查询行 */
fundb_result_t *fundb_select(fundb_handle_t db, const char *table,
                             const char *columns, const char *where_clause,
                             const char *order_by, uint32_t limit);

/* ---- SQL 查询 ---- */

/* 执行 SQL 语句 */
fundb_result_t *fundb_query(fundb_handle_t db, const char *sql);

/* 释放结果集 */
void fundb_free_result(fundb_result_t *result);

/* ---- 事务 ---- */

/* 开始事务 */
int fundb_begin(fundb_handle_t db);

/* 提交事务 */
int fundb_commit(fundb_handle_t db);

/* 回滚事务 */
int fundb_rollback(fundb_handle_t db);

/* ---- 索引 ---- */

/* 创建索引 */
int fundb_create_index(fundb_handle_t db, const char *table,
                       const char *column, const char *index_name);

/* 删除索引 */
int fundb_drop_index(fundb_handle_t db, const char *index_name);

/* ---- 工具函数 ---- */

/* 检查表是否存在 */
int fundb_table_exists(fundb_handle_t db, const char *name);

/* 获取表的行数 */
uint32_t fundb_row_count(fundb_handle_t db, const char *table);

/* 获取错误描述 */
const char *fundb_error_string(int error);

/* 初始化数据库子系统 */
void fundb_init(void);

/* ================================================================ */
/*  视图 (Views)                                                     */
/* ================================================================ */

#define FUNDB_MAX_VIEWS        16
#define FUNDB_VIEW_NAME_MAX    64
#define FUNDB_VIEW_SQL_MAX     512

typedef struct {
    char     name[FUNDB_VIEW_NAME_MAX];
    char     sql[FUNDB_VIEW_SQL_MAX];
    uint32_t col_count;
    char     column_names[FUNDB_MAX_COLUMNS][64];
    int      active;
} fundb_view_t;

int  fundb_create_view(fundb_handle_t db, const char *name, const char *sql);
int  fundb_drop_view(fundb_handle_t db, const char *name);
int  fundb_query_view(fundb_handle_t db, const char *name, fundb_result_t **result);
int  fundb_view_exists(fundb_handle_t db, const char *name);
int  fundb_list_views(fundb_handle_t db, char *buf, uint32_t bufsize);

/* ================================================================ */
/*  触发器 (Triggers)                                                */
/* ================================================================ */

#define FUNDB_MAX_TRIGGERS     32
#define FUNDB_TRIG_NAME_MAX    64
#define FUNDB_TRIG_SQL_MAX     512

#define FUNDB_TRIG_BEFORE      0
#define FUNDB_TRIG_AFTER       1

#define FUNDB_TRIG_INSERT      1
#define FUNDB_TRIG_UPDATE      2
#define FUNDB_TRIG_DELETE      3

typedef void (*fundb_trigger_callback_t)(fundb_handle_t db,
    const char *table, int operation, fundb_row_t *old_row, fundb_row_t *new_row);

typedef struct {
    char     name[FUNDB_TRIG_NAME_MAX];
    char     table_name[64];
    uint32_t timing;       /* BEFORE / AFTER */
    uint32_t event;        /* INSERT / UPDATE / DELETE */
    char     action_sql[FUNDB_TRIG_SQL_MAX];
    fundb_trigger_callback_t callback;
    int      active;
} fundb_trigger_t;

int  fundb_create_trigger(fundb_handle_t db, const char *name,
                          const char *table, uint32_t timing, uint32_t event,
                          const char *action_sql);
int  fundb_drop_trigger(fundb_handle_t db, const char *name);
int  fundb_list_triggers(fundb_handle_t db, const char *table,
                         char *buf, uint32_t bufsize);
int  fundb_trigger_exists(fundb_handle_t db, const char *name);
int  fundb_set_trigger_callback(fundb_handle_t db, const char *name,
                                fundb_trigger_callback_t callback);

/* ================================================================ */
/*  聚合函数 (Aggregate Functions)                                   */
/* ================================================================ */

typedef enum {
    FUNDB_AGG_COUNT = 0,
    FUNDB_AGG_SUM,
    FUNDB_AGG_AVG,
    FUNDB_AGG_MIN,
    FUNDB_AGG_MAX,
    FUNDB_AGG_TOTAL
} fundb_agg_type_t;

typedef struct {
    fundb_agg_type_t type;
    char     column_name[64];
    double   result;
    int64_t  result_int;
    int      has_result;
} fundb_agg_ctx_t;

fundb_result_t *fundb_aggregate(fundb_handle_t db, const char *table,
                                const char *column, fundb_agg_type_t agg_type,
                                const char *where_clause);
int  fundb_count(fundb_handle_t db, const char *table,
                 const char *where_clause, int64_t *result);
int  fundb_sum(fundb_handle_t db, const char *table, const char *column,
               const char *where_clause, double *result);
int  fundb_avg(fundb_handle_t db, const char *table, const char *column,
               const char *where_clause, double *result);
int  fundb_min(fundb_handle_t db, const char *table, const char *column,
               const char *where_clause, double *result);
int  fundb_max(fundb_handle_t db, const char *table, const char *column,
               const char *where_clause, double *result);

/* ================================================================ */
/*  存储过程 (Stored Procedures)                                     */
/* ================================================================ */

#define FUNDB_MAX_PROCS        16
#define FUNDB_PROC_NAME_MAX    64
#define FUNDB_PROC_BODY_MAX    2048
#define FUNDB_PROC_MAX_ARGS    16

typedef struct {
    char     name[FUNDB_PROC_NAME_MAX];
    char     body[FUNDB_PROC_BODY_MAX];
    uint32_t arg_count;
    char     arg_names[FUNDB_PROC_MAX_ARGS][64];
    uint32_t arg_types[FUNDB_PROC_MAX_ARGS];
    int      active;
} fundb_proc_t;

int  fundb_create_proc(fundb_handle_t db, const char *name,
                       const char *body, uint32_t arg_count,
                       const char **arg_names, const uint32_t *arg_types);
int  fundb_drop_proc(fundb_handle_t db, const char *name);
int  fundb_call_proc(fundb_handle_t db, const char *name,
                     fundb_row_t *args, fundb_result_t **result);
int  fundb_list_procs(fundb_handle_t db, char *buf, uint32_t bufsize);
int  fundb_proc_exists(fundb_handle_t db, const char *name);

/* ================================================================ */
/*  数据库备份/导出 (Backup/Export)                                  */
/* ================================================================ */

#define FUNDB_EXPORT_SQL       1
#define FUNDB_EXPORT_CSV       2
#define FUNDB_EXPORT_JSON      3

int  fundb_backup(fundb_handle_t db, const char *backup_path);
int  fundb_restore(fundb_handle_t db, const char *backup_path);
int  fundb_export_table(fundb_handle_t db, const char *table,
                        const char *path, uint32_t format);
int  fundb_import_table(fundb_handle_t db, const char *table,
                        const char *path, uint32_t format);

/* ================================================================ */
/*  数据库统计信息 (Statistics)                                      */
/* ================================================================ */

typedef struct {
    uint32_t total_tables;
    uint32_t total_rows;
    uint32_t total_indexes;
    uint32_t total_views;
    uint32_t total_triggers;
    uint32_t total_procs;
    uint64_t total_size_bytes;
    uint32_t page_size;
    uint32_t total_pages;
    uint32_t free_pages;
    uint32_t cache_hits;
    uint32_t cache_misses;
    uint64_t query_count;
    uint64_t transaction_count;
} fundb_stats_t;

int  fundb_get_stats(fundb_handle_t db, fundb_stats_t *stats);
int  fundb_analyze_table(fundb_handle_t db, const char *table);
int  fundb_vacuum(fundb_handle_t db);
int  fundb_reindex(fundb_handle_t db, const char *table);

#endif /* FUNDB_H */
