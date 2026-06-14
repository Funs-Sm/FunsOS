/* funsos_database.h - 内嵌数据库 API
 * 基于 fundb 的轻量级数据库操作接口
 * 支持 SQL 查询、表管理、事务等
 */

#ifndef FUNSOS_DATABASE_H
#define FUNSOS_DATABASE_H

#include "stdint.h"

/* ---- 数据库句柄 ---- */
typedef void *funsos_db_t;

/* ---- 查询结果集 ---- */
typedef void *funsos_db_result_t;

/* ---- 数据库行数据 ---- */
typedef struct {
    uint32_t column_count;      /* 列数 */
    char   **column_names;      /* 列名数组 */
    char   **values;            /* 值数组（均为字符串形式） */
} funsos_db_row_t;

/* ---- 数据库配置 ---- */
typedef struct {
    const char *db_path;        /* 数据库文件路径 */
    uint32_t    page_size;      /* 页大小 (默认 4096) */
    uint32_t    cache_size_kb;  /* 缓存大小 (KB, 默认 256) */
    uint8_t     read_only;      /* 只读模式 */
    uint8_t     create_if_missing; /* 不存在时创建 */
} funsos_db_config_t;

/* ---- 数据库状态 ---- */
typedef struct {
    char        db_path[256];   /* 数据库路径 */
    uint32_t    table_count;    /* 表数量 */
    uint32_t    total_size_kb;  /* 数据库总大小 (KB) */
    uint32_t    free_size_kb;   /* 空闲空间 (KB) */
    uint32_t    page_count;     /* 页总数 */
    uint32_t    cache_hits;     /* 缓存命中次数 */
    uint32_t    cache_misses;   /* 缓存未命中次数 */
} funsos_db_stats_t;

/* ---- 数据库 API ---- */

/* 打开/创建数据库 */
funsos_db_t funsos_db_open(const char *path);
funsos_db_t funsos_db_open_ex(const funsos_db_config_t *config);

/* 关闭数据库 */
int funsos_db_close(funsos_db_t db);

/* 执行 SQL 语句（INSERT/UPDATE/DELETE/CREATE 等） */
int funsos_db_execute(funsos_db_t db, const char *sql);

/* 执行 SQL 查询（SELECT） */
funsos_db_result_t funsos_db_query(funsos_db_t db, const char *sql);

/* ---- 结果集操作 ---- */

/* 获取结果集中的行数 */
int funsos_db_result_row_count(funsos_db_result_t result);

/* 获取结果集中的列数 */
int funsos_db_result_column_count(funsos_db_result_t result);

/* 获取列名 */
const char *funsos_db_result_column_name(funsos_db_result_t result, int column);

/* 移动到下一行（首次调用移到第一行），返回 1=有数据, 0=无更多数据 */
int funsos_db_result_next(funsos_db_result_t result);

/* 获取当前行指定列的值 */
const char *funsos_db_result_get(funsos_db_result_t result, int column);

/* 获取当前行指定列名的值 */
const char *funsos_db_result_get_by_name(funsos_db_result_t result, const char *column_name);

/* 获取当前行整行数据 */
int funsos_db_result_get_row(funsos_db_result_t result, funsos_db_row_t *row);

/* 释放结果集 */
void funsos_db_result_free(funsos_db_result_t result);

/* ---- 事务管理 ---- */

/* 开始事务 */
int funsos_db_begin_transaction(funsos_db_t db);

/* 提交事务 */
int funsos_db_commit(funsos_db_t db);

/* 回滚事务 */
int funsos_db_rollback(funsos_db_t db);

/* ---- 数据库管理 ---- */

/* 获取数据库状态 */
int funsos_db_get_stats(funsos_db_t db, funsos_db_stats_t *stats);

/* 压缩数据库（回收空闲空间） */
int funsos_db_vacuum(funsos_db_t db);

/* 备份数据库到指定路径 */
int funsos_db_backup(funsos_db_t db, const char *backup_path);

/* 列出所有表 */
int funsos_db_list_tables(funsos_db_t db, char *buf, uint32_t bufsize);

/* 检查表是否存在 */
int funsos_db_table_exists(funsos_db_t db, const char *table_name);

/* ---- 预处理语句（参数化查询） ---- */

typedef void *funsos_db_stmt_t;

/* 准备 SQL 语句 */
funsos_db_stmt_t funsos_db_prepare(funsos_db_t db, const char *sql);

/* 绑定参数（按索引，从 1 开始） */
int funsos_db_bind_int(funsos_db_stmt_t stmt, int index, int value);
int funsos_db_bind_int64(funsos_db_stmt_t stmt, int index, int64_t value);
int funsos_db_bind_double(funsos_db_stmt_t stmt, int index, double value);
int funsos_db_bind_text(funsos_db_stmt_t stmt, int index, const char *text);
int funsos_db_bind_blob(funsos_db_stmt_t stmt, int index, const void *data, uint32_t size);
int funsos_db_bind_null(funsos_db_stmt_t stmt, int index);

/* 执行预处理语句 */
int funsos_db_stmt_execute(funsos_db_stmt_t stmt);

/* 执行预处理查询 */
funsos_db_result_t funsos_db_stmt_query(funsos_db_stmt_t stmt);

/* 重置预处理语句（可重新绑定参数） */
int funsos_db_stmt_reset(funsos_db_stmt_t stmt);

/* 释放预处理语句 */
void funsos_db_stmt_free(funsos_db_stmt_t stmt);

/* ---- 便捷函数 ---- */

/* 获取最后插入的行 ID */
int64_t funsos_db_last_insert_id(funsos_db_t db);

/* 获取受影响的行数 */
int funsos_db_changes(funsos_db_t db);

/* 获取最后一次错误信息 */
const char *funsos_db_error_message(funsos_db_t db);

#endif /* FUNSOS_DATABASE_H */