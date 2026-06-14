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

#endif /* FUNDB_H */
