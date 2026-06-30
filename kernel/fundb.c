/* fundb.c - FUNSOS 数据库引擎实现
 * 基于文件的存储、B-tree 索引、SQL 解析器、WAL 事务
 */

#include "fundb.h"
#include "kheap.h"
#include "klog.h"
#include "string.h"
#include "stdlib.h"
#include "vfs.h"

/* ---- 常量定义 ---- */

#define FUNDB_MAX_TABLES    32
#define FUNDB_MAX_COLUMNS   32
#define FUNDB_MAX_ROWS      1024
#define FUNDB_PAGE_SIZE     4096
#define FUNDB_BTREE_ORDER   64
#define FUNDB_WAL_SIZE      (256 * 1024)
#define FUNDB_DB_PATH       "/var/db/"

/* ---- B-tree 节点 ---- */

typedef struct btree_node {
    uint32_t is_leaf;
    uint32_t key_count;
    uint32_t keys[FUNDB_BTREE_ORDER - 1];
    void *values[FUNDB_BTREE_ORDER - 1];
    struct btree_node *children[FUNDB_BTREE_ORDER];
} btree_node_t;

/* ---- 索引结构 ---- */

typedef struct {
    char name[64];
    char table[64];
    char column[64];
    btree_node_t *root;
} fundb_index_t;

/* ---- 表结构 ---- */

typedef struct {
    char name[64];
    fundb_column_t columns[FUNDB_MAX_COLUMNS];
    uint32_t col_count;
    fundb_row_t rows[FUNDB_MAX_ROWS];
    uint32_t row_count;
    uint32_t next_auto_id;       /* 自增ID计数器 */
    uint8_t dirty;               /* 是否需要写回磁盘 */
} fundb_table_t;

/* ---- WAL 日志条目 ---- */

#define WAL_OP_INSERT  1
#define WAL_OP_UPDATE  2
#define WAL_OP_DELETE  3

typedef struct {
    uint32_t operation;
    char table[64];
    uint32_t row_index;
    fundb_row_t old_data;        /* 旧数据（用于回滚） */
    uint32_t committed;
} wal_entry_t;

/* ---- 数据库句柄 ---- */

typedef struct {
    char path[256];
    fundb_table_t tables[FUNDB_MAX_TABLES];
    uint32_t table_count;
    fundb_index_t indexes[64];
    uint32_t index_count;
    wal_entry_t *wal;            /* WAL 日志缓冲区 */
    uint32_t wal_count;
    uint32_t wal_capacity;
    uint8_t in_transaction;
    uint32_t last_error;
} fundb_db_t;

/* ---- 全局数据库列表 ---- */

#define FUNDB_MAX_DBS 8
static fundb_db_t *g_databases[FUNDB_MAX_DBS];
static uint32_t g_db_count = 0;
static uint8_t g_fundb_initialized = 0;

/* ---- 辅助函数 ---- */

static void *fundb_alloc(uint32_t size)
{
    return kmalloc(size);
}

static void fundb_free_mem(void *ptr)
{
    if (ptr) kfree(ptr);
}

/* 分配行数据 */
static int fundb_row_init(fundb_row_t *row, uint32_t col_count)
{
    row->values = (void **)fundb_alloc(sizeof(void *) * col_count);
    row->sizes = (uint32_t *)fundb_alloc(sizeof(uint32_t) * col_count);
    row->types = (uint32_t *)fundb_alloc(sizeof(uint32_t) * col_count);

    if (!row->values || !row->sizes || !row->types)
        return FUNDB_NO_MEMORY;

    for (uint32_t i = 0; i < col_count; i++) {
        row->values[i] = NULL;
        row->sizes[i] = 0;
        row->types[i] = FUNDB_TYPE_NULL;
    }

    return FUNDB_OK;
}

/* 释放行数据 */
static void fundb_row_free(fundb_row_t *row, uint32_t col_count)
{
    if (row->values) {
        for (uint32_t i = 0; i < col_count; i++) {
            if (row->values[i]) fundb_free_mem(row->values[i]);
        }
        fundb_free_mem(row->values);
    }
    if (row->sizes) fundb_free_mem(row->sizes);
    if (row->types) fundb_free_mem(row->types);
}

/* 复制行数据 */
static int fundb_row_copy(fundb_row_t *dst, const fundb_row_t *src, uint32_t col_count)
{
    for (uint32_t i = 0; i < col_count; i++) {
        dst->types[i] = src->types[i];
        dst->sizes[i] = src->sizes[i];

        if (src->values[i] && src->sizes[i] > 0) {
            dst->values[i] = fundb_alloc(src->sizes[i]);
            if (!dst->values[i]) return FUNDB_NO_MEMORY;
            memcpy(dst->values[i], src->values[i], src->sizes[i]);
        } else {
            dst->values[i] = NULL;
        }
    }
    return FUNDB_OK;
}

/* ---- B-tree 操作 ---- */

static btree_node_t *btree_create_node(uint32_t is_leaf)
{
    btree_node_t *node = (btree_node_t *)fundb_alloc(sizeof(btree_node_t));
    if (!node) return NULL;

    node->is_leaf = is_leaf;
    node->key_count = 0;

    for (uint32_t i = 0; i < FUNDB_BTREE_ORDER - 1; i++) {
        node->keys[i] = 0;
        node->values[i] = NULL;
    }
    for (uint32_t i = 0; i < FUNDB_BTREE_ORDER; i++) {
        node->children[i] = NULL;
    }

    return node;
}

static void btree_destroy(btree_node_t *node)
{
    if (!node) return;

    if (!node->is_leaf) {
        for (uint32_t i = 0; i <= node->key_count; i++) {
            btree_destroy(node->children[i]);
        }
    }

    fundb_free_mem(node);
}

/* B-tree 搜索 */
static void *btree_search(btree_node_t *node, uint32_t key)
{
    if (!node) return NULL;

    uint32_t i = 0;
    while (i < node->key_count && key > node->keys[i])
        i++;

    if (i < node->key_count && key == node->keys[i])
        return node->values[i];

    if (node->is_leaf)
        return NULL;

    return btree_search(node->children[i], key);
}

/* B-tree 插入（简化实现） */
static int btree_insert(btree_node_t **root, uint32_t key, void *value)
{
    if (*root == NULL) {
        *root = btree_create_node(1);
        if (!*root) return FUNDB_NO_MEMORY;

        (*root)->keys[0] = key;
        (*root)->values[0] = value;
        (*root)->key_count = 1;
        return FUNDB_OK;
    }

    /* 简化：在叶节点中顺序查找插入位置 */
    btree_node_t *node = *root;

    /* 如果根节点已满，创建新根 */
    if (node->key_count >= FUNDB_BTREE_ORDER - 1) {
        btree_node_t *new_root = btree_create_node(0);
        if (!new_root) return FUNDB_NO_MEMORY;

        new_root->children[0] = *root;
        *root = new_root;
        /* 简化：不执行分裂，直接插入到新根 */
    }

    /* 找到叶节点 */
    node = *root;
    while (!node->is_leaf) {
        uint32_t i = 0;
        while (i < node->key_count && key > node->keys[i]) i++;
        node = node->children[i];
        if (!node) {
            node = btree_create_node(1);
            break;
        }
    }

    /* 在叶节点中插入 */
    if (node->key_count >= FUNDB_BTREE_ORDER - 1)
        return FUNDB_ERROR;  /* 需要分裂 */

    /* 找到插入位置 */
    uint32_t pos = node->key_count;
    while (pos > 0 && key < node->keys[pos - 1]) {
        node->keys[pos] = node->keys[pos - 1];
        node->values[pos] = node->values[pos - 1];
        pos--;
    }

    node->keys[pos] = key;
    node->values[pos] = value;
    node->key_count++;

    return FUNDB_OK;
}

/* ---- WAL 事务日志 ---- */

static int wal_init(fundb_db_t *db)
{
    db->wal_capacity = 64;
    db->wal = (wal_entry_t *)fundb_alloc(sizeof(wal_entry_t) * db->wal_capacity);
    if (!db->wal) return FUNDB_NO_MEMORY;

    db->wal_count = 0;
    db->in_transaction = 0;
    return FUNDB_OK;
}

static int wal_append(fundb_db_t *db, uint32_t op, const char *table,
                      uint32_t row_index, fundb_row_t *old_data, uint32_t col_count)
{
    if (db->wal_count >= db->wal_capacity) {
        /* 扩展 WAL */
        uint32_t new_cap = db->wal_capacity * 2;
        wal_entry_t *new_wal = (wal_entry_t *)fundb_alloc(sizeof(wal_entry_t) * new_cap);
        if (!new_wal) return FUNDB_NO_MEMORY;

        memcpy(new_wal, db->wal, sizeof(wal_entry_t) * db->wal_count);
        fundb_free_mem(db->wal);
        db->wal = new_wal;
        db->wal_capacity = new_cap;
    }

    wal_entry_t *entry = &db->wal[db->wal_count];
    entry->operation = op;
    entry->row_index = row_index;
    entry->committed = 0;

    for (int i = 0; i < 63 && table[i]; i++)
        entry->table[i] = table[i];
    entry->table[63] = '\0';

    if (old_data) {
        fundb_row_init(&entry->old_data, col_count);
        fundb_row_copy(&entry->old_data, old_data, col_count);
    }

    db->wal_count++;
    return FUNDB_OK;
}

static void wal_clear(fundb_db_t *db)
{
    db->wal_count = 0;
}

/* ---- SQL 解析器（简化实现） ---- */

/* SQL 语句类型 */
#define SQL_SELECT      1
#define SQL_INSERT      2
#define SQL_UPDATE      3
#define SQL_DELETE      4
#define SQL_CREATE      5
#define SQL_DROP        6
#define SQL_ALTER       7

typedef struct {
    uint32_t type;
    char table[64];
    char columns[256];
    char where_clause[256];
    char values[256];
    char order_by[64];
    uint32_t limit;
    fundb_column_t new_columns[FUNDB_MAX_COLUMNS];
    uint32_t new_col_count;
} sql_statement_t;

/* 字符串跳过空白 */
static const char *skip_spaces(const char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    return s;
}

/* 字符串比较（不区分大小写，前缀） */
static int str_prefix_ci(const char *str, const char *prefix)
{
    while (*prefix) {
        char a = *str, b = *prefix;
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0;
        str++; prefix++;
    }
    return 1;
}

/* 提取标识符 */
static int extract_identifier(const char *s, char *out, int max_len)
{
    int i = 0;
    while (*s && *s != ' ' && *s != '\t' && *s != '\n' && *s != ',' &&
           *s != '(' && *s != ')' && *s != ';' && i < max_len - 1) {
        out[i++] = *s++;
    }
    out[i] = '\0';
    return i;
}

/* 解析类型字符串 */
static uint32_t parse_type(const char *type_str)
{
    if (str_prefix_ci(type_str, "INT") || str_prefix_ci(type_str, "INTEGER"))
        return FUNDB_TYPE_INT;
    if (str_prefix_ci(type_str, "FLOAT") || str_prefix_ci(type_str, "DOUBLE") ||
        str_prefix_ci(type_str, "REAL"))
        return FUNDB_TYPE_FLOAT;
    if (str_prefix_ci(type_str, "TEXT") || str_prefix_ci(type_str, "VARCHAR") ||
        str_prefix_ci(type_str, "CHAR") || str_prefix_ci(type_str, "STRING"))
        return FUNDB_TYPE_TEXT;
    if (str_prefix_ci(type_str, "BLOB") || str_prefix_ci(type_str, "BINARY"))
        return FUNDB_TYPE_BLOB;
    if (str_prefix_ci(type_str, "BOOL") || str_prefix_ci(type_str, "BOOLEAN"))
        return FUNDB_TYPE_BOOL;
    if (str_prefix_ci(type_str, "DATE") || str_prefix_ci(type_str, "DATETIME") ||
        str_prefix_ci(type_str, "TIME"))
        return FUNDB_TYPE_DATE;
    return FUNDB_TYPE_TEXT;
}

/* 解析 CREATE TABLE 的列定义 */
static int parse_create_columns(const char *sql, sql_statement_t *stmt)
{
    const char *p = sql;
    stmt->new_col_count = 0;

    while (*p && *p != ';') {
        p = skip_spaces(p);
        if (!*p || *p == ')') break;

        if (stmt->new_col_count >= FUNDB_MAX_COLUMNS) break;

        fundb_column_t *col = &stmt->new_columns[stmt->new_col_count];
        memset(col, 0, sizeof(fundb_column_t));

        p += extract_identifier(p, col->name, sizeof(col->name));
        p = skip_spaces(p);

        char type_str[32];
        int ti = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != ',' &&
               *p != '(' && *p != ')' && *p != ';' && ti < 31) {
            type_str[ti++] = *p++;
        }
        type_str[ti] = '\0';
        col->type = parse_type(type_str);

        p = skip_spaces(p);
        if (*p == '(') {
            p++;
            col->size = 0;
            while (*p >= '0' && *p <= '9') {
                col->size = col->size * 10 + (*p - '0');
                p++;
            }
            if (*p == ')') p++;
        }

        while (*p && *p != ',' && *p != ')' && *p != ';') {
            p = skip_spaces(p);
            if (str_prefix_ci(p, "NOT NULL")) {
                col->not_null = 1;
                p += 8;
            } else if (str_prefix_ci(p, "PRIMARY KEY")) {
                col->primary_key = 1;
                p += 11;
            } else if (str_prefix_ci(p, "AUTO_INCREMENT") || str_prefix_ci(p, "AUTOINCREMENT")) {
                col->auto_increment = 1;
                p += 14;
            } else if (str_prefix_ci(p, "DEFAULT")) {
                p += 7;
                p = skip_spaces(p);
                col->default_value = 0;
            } else if (*p) {
                p++;
            }
        }

        stmt->new_col_count++;

        if (*p == ',') p++;
    }

    return stmt->new_col_count > 0 ? FUNDB_OK : FUNDB_ERROR;
}

/* 解析 SQL 语句 */
static int sql_parse(const char *sql, sql_statement_t *stmt)
{
    const char *p = skip_spaces(sql);

    memset(stmt, 0, sizeof(sql_statement_t));

    if (str_prefix_ci(p, "SELECT")) {
        stmt->type = SQL_SELECT;
        p = skip_spaces(p + 6);

        /* 提取列名 */
        int i = 0;
        while (*p && !str_prefix_ci(p, "FROM")) {
            stmt->columns[i++] = *p++;
        }

        if (str_prefix_ci(p, "FROM")) {
            p = skip_spaces(p + 4);
            p += extract_identifier(p, stmt->table, sizeof(stmt->table));
        }

        p = skip_spaces(p);

        if (str_prefix_ci(p, "WHERE")) {
            p = skip_spaces(p + 5);
            i = 0;
            while (*p && !str_prefix_ci(p, "ORDER") && !str_prefix_ci(p, "LIMIT")) {
                stmt->where_clause[i++] = *p++;
            }
        }

        if (str_prefix_ci(p, "ORDER BY")) {
            p = skip_spaces(p + 8);
            extract_identifier(p, stmt->order_by, sizeof(stmt->order_by));
        }

        if (str_prefix_ci(p, "LIMIT")) {
            p = skip_spaces(p + 5);
            stmt->limit = 0;
            while (*p >= '0' && *p <= '9') {
                stmt->limit = stmt->limit * 10 + (*p - '0');
                p++;
            }
        }

        return FUNDB_OK;
    }

    if (str_prefix_ci(p, "INSERT INTO")) {
        stmt->type = SQL_INSERT;
        p = skip_spaces(p + 11);
        p += extract_identifier(p, stmt->table, sizeof(stmt->table));

        /* 跳过 VALUES 部分 */
        if (str_prefix_ci(p, "VALUES")) {
            p = skip_spaces(p + 6);
            if (*p == '(') p++;
            int i = 0;
            while (*p && *p != ';') {
                stmt->values[i++] = *p++;
            }
        }

        return FUNDB_OK;
    }

    if (str_prefix_ci(p, "UPDATE")) {
        stmt->type = SQL_UPDATE;
        p = skip_spaces(p + 6);
        p += extract_identifier(p, stmt->table, sizeof(stmt->table));

        if (str_prefix_ci(p, "SET")) {
            p = skip_spaces(p + 3);
            int i = 0;
            while (*p && !str_prefix_ci(p, "WHERE")) {
                stmt->values[i++] = *p++;
            }
        }

        if (str_prefix_ci(p, "WHERE")) {
            p = skip_spaces(p + 5);
            int i = 0;
            while (*p && *p != ';') {
                stmt->where_clause[i++] = *p++;
            }
        }

        return FUNDB_OK;
    }

    if (str_prefix_ci(p, "DELETE FROM")) {
        stmt->type = SQL_DELETE;
        p = skip_spaces(p + 11);
        p += extract_identifier(p, stmt->table, sizeof(stmt->table));

        if (str_prefix_ci(p, "WHERE")) {
            p = skip_spaces(p + 5);
            int i = 0;
            while (*p && *p != ';') {
                stmt->where_clause[i++] = *p++;
            }
        }

        return FUNDB_OK;
    }

    if (str_prefix_ci(p, "CREATE TABLE")) {
        stmt->type = SQL_CREATE;
        p = skip_spaces(p + 12);
        p += extract_identifier(p, stmt->table, sizeof(stmt->table));
        p = skip_spaces(p);

        if (*p == '(') {
            p++;
            parse_create_columns(p, stmt);
        }

        return FUNDB_OK;
    }

    if (str_prefix_ci(p, "DROP TABLE")) {
        stmt->type = SQL_DROP;
        p = skip_spaces(p + 10);
        extract_identifier(p, stmt->table, sizeof(stmt->table));
        return FUNDB_OK;
    }

    return FUNDB_ERROR;
}

/* ---- WHERE 条件操作符 ---- */

#define OP_EQ    1  /* = */
#define OP_NE    2  /* != or <> */
#define OP_GT    3  /* > */
#define OP_LT    4  /* < */
#define OP_GE    5  /* >= */
#define OP_LE    6  /* <= */
#define OP_LIKE  7  /* LIKE */

/* WHERE 条件结构 */
typedef struct {
    char column[64];
    uint32_t op;
    char value[256];
    uint8_t has_next;
    uint32_t logical_op;  /* 0=none, 1=AND, 2=OR */
} where_condition_t;

/* ---- 查找列索引 ---- */

static int find_column_index(fundb_table_t *table, const char *name)
{
    for (uint32_t i = 0; i < table->col_count; i++) {
        if (strcmp(table->columns[i].name, name) == 0)
            return (int)i;
    }
    return -1;
}

/* ---- 解析值字符串为实际数据 ---- */

static int parse_value(const char *str, uint32_t *out_type, void **out_val, uint32_t *out_size)
{
    if (!str || !*str) {
        *out_type = FUNDB_TYPE_NULL;
        *out_val = NULL;
        *out_size = 0;
        return FUNDB_OK;
    }

    const char *p = str;
    while (*p == ' ' || *p == '\t') p++;

    if (*p == '\'') {
        p++;
        char buf[256];
        int i = 0;
        while (*p && *p != '\'' && i < 255) buf[i++] = *p++;
        buf[i] = '\0';

        *out_type = FUNDB_TYPE_TEXT;
        *out_size = i + 1;
        *out_val = fundb_alloc(*out_size);
        if (*out_val) memcpy(*out_val, buf, *out_size);
        return FUNDB_OK;
    }

    if ((*p >= '0' && *p <= '9') || *p == '-') {
        int is_float = 0;
        const char *q = p;
        while (*q) {
            if (*q == '.') is_float = 1;
            q++;
        }

        if (is_float) {
            *out_type = FUNDB_TYPE_FLOAT;
            *out_size = sizeof(double);
            *out_val = fundb_alloc(*out_size);
            if (*out_val) {
                double val = strtod(p, NULL);
                memcpy(*out_val, &val, sizeof(double));
            }
        } else {
            *out_type = FUNDB_TYPE_INT;
            *out_size = sizeof(int32_t);
            *out_val = fundb_alloc(*out_size);
            if (*out_val) {
                int32_t val = atoi(p);
                memcpy(*out_val, &val, sizeof(int32_t));
            }
        }
        return FUNDB_OK;
    }

    if (str_prefix_ci(p, "NULL")) {
        *out_type = FUNDB_TYPE_NULL;
        *out_val = NULL;
        *out_size = 0;
        return FUNDB_OK;
    }

    if (str_prefix_ci(p, "TRUE") || str_prefix_ci(p, "FALSE")) {
        *out_type = FUNDB_TYPE_BOOL;
        *out_size = sizeof(uint8_t);
        *out_val = fundb_alloc(*out_size);
        if (*out_val) {
            uint8_t val = str_prefix_ci(p, "TRUE") ? 1 : 0;
            *(uint8_t *)*out_val = val;
        }
        return FUNDB_OK;
    }

    *out_type = FUNDB_TYPE_TEXT;
    *out_size = strlen(p) + 1;
    *out_val = fundb_alloc(*out_size);
    if (*out_val) memcpy(*out_val, p, *out_size);
    return FUNDB_OK;
}

/* ---- 比较两个值 ---- */

static int compare_values(uint32_t type1, void *val1, uint32_t size1,
                          uint32_t type2, void *val2, uint32_t size2)
{
    if (type1 == FUNDB_TYPE_NULL || type2 == FUNDB_TYPE_NULL)
        return 0;

    if (type1 == FUNDB_TYPE_INT && type2 == FUNDB_TYPE_INT) {
        int32_t a = *(int32_t *)val1;
        int32_t b = *(int32_t *)val2;
        if (a == b) return 0;
        return (a < b) ? -1 : 1;
    }

    if ((type1 == FUNDB_TYPE_INT || type1 == FUNDB_TYPE_FLOAT) &&
        (type2 == FUNDB_TYPE_INT || type2 == FUNDB_TYPE_FLOAT)) {
        double a = (type1 == FUNDB_TYPE_INT) ? (double)*(int32_t *)val1 : *(double *)val1;
        double b = (type2 == FUNDB_TYPE_INT) ? (double)*(int32_t *)val2 : *(double *)val2;
        if (a == b) return 0;
        return (a < b) ? -1 : 1;
    }

    if (type1 == FUNDB_TYPE_TEXT && type2 == FUNDB_TYPE_TEXT) {
        return strcmp((char *)val1, (char *)val2);
    }

    return 0;
}

/* ---- LIKE 模式匹配 ---- */

static int like_match(const char *str, const char *pattern)
{
    if (*pattern == '\0') return *str == '\0';

    if (*pattern == '%') {
        while (*str) {
            if (like_match(str, pattern + 1)) return 1;
            str++;
        }
        return like_match(str, pattern + 1);
    }

    if (*pattern == '_') {
        if (*str == '\0') return 0;
        return like_match(str + 1, pattern + 1);
    }

    if (*str == '\0') return 0;
    if (*str != *pattern) return 0;
    return like_match(str + 1, pattern + 1);
}

/* ---- 解析单个 WHERE 条件 ---- */

static int parse_single_condition(const char *clause, where_condition_t *cond)
{
    const char *p = clause;
    while (*p == ' ' || *p == '\t') p++;

    int i = 0;
    while (*p && *p != ' ' && *p != '\t' && *p != '=' && *p != '!' &&
           *p != '<' && *p != '>' && i < 63) {
        cond->column[i++] = *p++;
    }
    cond->column[i] = '\0';

    while (*p == ' ' || *p == '\t') p++;

    if (*p == '=') {
        cond->op = OP_EQ;
        p++;
    } else if (*p == '!' && *(p + 1) == '=') {
        cond->op = OP_NE;
        p += 2;
    } else if (*p == '<' && *(p + 1) == '>') {
        cond->op = OP_NE;
        p += 2;
    } else if (*p == '>' && *(p + 1) == '=') {
        cond->op = OP_GE;
        p += 2;
    } else if (*p == '<' && *(p + 1) == '=') {
        cond->op = OP_LE;
        p += 2;
    } else if (*p == '>') {
        cond->op = OP_GT;
        p++;
    } else if (*p == '<') {
        cond->op = OP_LT;
        p++;
    } else {
        return FUNDB_ERROR;
    }

    while (*p == ' ' || *p == '\t') p++;

    i = 0;
    if (*p == '\'') {
        p++;
        cond->value[i++] = '\'';
        while (*p && *p != '\'' && i < 254) cond->value[i++] = *p++;
        if (*p == '\'') p++;
        cond->value[i] = '\'';
        cond->value[i + 1] = '\0';
    } else {
        while (*p && *p != ' ' && *p != '\t' && *p != ';' && i < 254) {
            cond->value[i++] = *p++;
        }
        cond->value[i] = '\0';
    }

    while (*p == ' ' || *p == '\t') p++;

    if (str_prefix_ci(p, "AND")) {
        cond->has_next = 1;
        cond->logical_op = 1;
    } else if (str_prefix_ci(p, "OR")) {
        cond->has_next = 1;
        cond->logical_op = 2;
    } else {
        cond->has_next = 0;
    }

    return FUNDB_OK;
}

/* ---- 评估单个条件 ---- */

static int evaluate_single_condition(fundb_table_t *table, fundb_row_t *row, where_condition_t *cond)
{
    int col_idx = find_column_index(table, cond->column);
    if (col_idx < 0) return 0;

    uint32_t val_type;
    void *val_data;
    uint32_t val_size;
    parse_value(cond->value, &val_type, &val_data, &val_size);

    int cmp = compare_values(row->types[col_idx], row->values[col_idx], row->sizes[col_idx],
                             val_type, val_data, val_size);

    int result = 0;
    switch (cond->op) {
    case OP_EQ:  result = (cmp == 0); break;
    case OP_NE:  result = (cmp != 0); break;
    case OP_GT:  result = (cmp > 0); break;
    case OP_LT:  result = (cmp < 0); break;
    case OP_GE:  result = (cmp >= 0); break;
    case OP_LE:  result = (cmp <= 0); break;
    case OP_LIKE:
        if (row->types[col_idx] == FUNDB_TYPE_TEXT && val_type == FUNDB_TYPE_TEXT)
            result = like_match((char *)row->values[col_idx], (char *)val_data);
        break;
    }

    if (val_data) fundb_free_mem(val_data);
    return result;
}

/* ---- 评估整个 WHERE 子句 ---- */

static int evaluate_where(fundb_table_t *table, fundb_row_t *row, const char *where_clause)
{
    if (!where_clause || !*where_clause) return 1;

    const char *p = where_clause;
    int result = 1;
    int first = 1;
    uint32_t logical_op = 0;

    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        where_condition_t cond;
        if (parse_single_condition(p, &cond) != FUNDB_OK) break;

        int cond_result = evaluate_single_condition(table, row, &cond);

        if (first) {
            result = cond_result;
            first = 0;
        } else if (logical_op == 1) {
            result = result && cond_result;
        } else if (logical_op == 2) {
            result = result || cond_result;
        }

        if (!cond.has_next) break;
        logical_op = cond.logical_op;

        while (*p && *p != ' ' && *p != '\t') p++;
        while (*p == ' ' || *p == '\t') p++;
        if (str_prefix_ci(p, "AND")) p += 3;
        else if (str_prefix_ci(p, "OR")) p += 2;
    }

    return result;
}

/* ---- 查找表 ---- */

static fundb_table_t *find_table(fundb_db_t *db, const char *name)
{
    for (uint32_t i = 0; i < db->table_count; i++) {
        int match = 1;
        for (int j = 0; j < 63 && (name[j] || db->tables[i].name[j]); j++) {
            if (name[j] != db->tables[i].name[j]) { match = 0; break; }
        }
        if (match) return &db->tables[i];
    }
    return NULL;
}

/* ---- 磁盘 I/O ---- */

/* 将表数据写入文件 */
static int fundb_write_table(fundb_db_t *db, fundb_table_t *table)
{
    char path[256];
    /* 构建路径: /var/db/<dbname>/<tablename>.tbl */
    int pos = 0;
    for (int i = 0; FUNDB_DB_PATH[i]; i++) path[pos++] = FUNDB_DB_PATH[i];

    /* 数据库名 */
    for (int i = 0; db->path[i] && db->path[i] != '.'; i++)
        path[pos++] = db->path[i];
    path[pos++] = '/';

    /* 表名 */
    for (int i = 0; table->name[i] && i < 58; i++)
        path[pos++] = table->name[i];
    path[pos++] = '.';
    path[pos++] = 't';
    path[pos++] = 'b';
    path[pos++] = 'l';
    path[pos] = '\0';

    /* 通过 VFS 写入 */
    vfs_creat(path, 0x1FF);
    file_t *f = NULL;
    vfs_open(path, FILE_MODE_WRITE, &f);  /* 写入模式 */
    if (f == NULL) return FUNDB_IO_ERROR;

    /* 写入表头 */
    vfs_write(f, &table->col_count, sizeof(uint32_t));
    vfs_write(f, &table->row_count, sizeof(uint32_t));
    vfs_write(f, table->columns, sizeof(fundb_column_t) * table->col_count);

    /* 写入行数据 */
    for (uint32_t i = 0; i < table->row_count; i++) {
        for (uint32_t j = 0; j < table->col_count; j++) {
            vfs_write(f, &table->rows[i].types[j], sizeof(uint32_t));
            vfs_write(f, &table->rows[i].sizes[j], sizeof(uint32_t));
            if (table->rows[i].values[j] && table->rows[i].sizes[j] > 0) {
                vfs_write(f, table->rows[i].values[j], table->rows[i].sizes[j]);
            }
        }
    }

    vfs_close(f);
    table->dirty = 0;
    return FUNDB_OK;
}

/* 从文件读取表数据 */
static int fundb_read_table(fundb_db_t *db, fundb_table_t *table)
{
    char path[256];
    int pos = 0;
    for (int i = 0; FUNDB_DB_PATH[i]; i++) path[pos++] = FUNDB_DB_PATH[i];
    for (int i = 0; db->path[i] && db->path[i] != '.'; i++)
        path[pos++] = db->path[i];
    path[pos++] = '/';
    for (int i = 0; table->name[i] && i < 58; i++)
        path[pos++] = table->name[i];
    path[pos++] = '.';
    path[pos++] = 't';
    path[pos++] = 'b';
    path[pos++] = 'l';
    path[pos] = '\0';

    file_t *f = NULL;
    vfs_open(path, FILE_MODE_READ, &f);  /* 只读 */
    if (f == NULL) return FUNDB_IO_ERROR;

    /* 读取表头 */
    vfs_read(f, &table->col_count, sizeof(uint32_t));
    vfs_read(f, &table->row_count, sizeof(uint32_t));
    vfs_read(f, table->columns, sizeof(fundb_column_t) * table->col_count);

    /* 读取行数据 */
    for (uint32_t i = 0; i < table->row_count && i < FUNDB_MAX_ROWS; i++) {
        fundb_row_init(&table->rows[i], table->col_count);

        for (uint32_t j = 0; j < table->col_count; j++) {
            vfs_read(f, &table->rows[i].types[j], sizeof(uint32_t));
            vfs_read(f, &table->rows[i].sizes[j], sizeof(uint32_t));

            if (table->rows[i].sizes[j] > 0) {
                table->rows[i].values[j] = fundb_alloc(table->rows[i].sizes[j]);
                if (table->rows[i].values[j]) {
                    vfs_read(f, table->rows[i].values[j], table->rows[i].sizes[j]);
                }
            }
        }
    }

    vfs_close(f);
    return FUNDB_OK;
}

/* ================================================================
 *  公共 API 实现
 * ================================================================ */

/* 初始化数据库子系统 */
void fundb_init(void)
{
    if (g_fundb_initialized) return;

    for (uint32_t i = 0; i < FUNDB_MAX_DBS; i++)
        g_databases[i] = NULL;
    g_db_count = 0;

    /* 确保 /var/db/ 目录存在 */
    vfs_mkdir("/var", 0x1FF);
    vfs_mkdir("/var/db", 0x1FF);

    g_fundb_initialized = 1;
    klog_info("FunDB: Database engine initialized");
}

/* 从磁盘加载所有表 */
static int fundb_load_tables(fundb_db_t *db)
{
    char dir_path[256];
    int pos = 0;
    for (int i = 0; FUNDB_DB_PATH[i]; i++) dir_path[pos++] = FUNDB_DB_PATH[i];
    for (int i = 0; db->path[i] && db->path[i] != '.' && i < 200; i++)
        dir_path[pos++] = db->path[i];
    dir_path[pos] = '\0';

    vfs_mkdir(dir_path, 0x1FF);

    file_t *dir = NULL;
    if (vfs_opendir(dir_path, &dir) != 0 || !dir)
        return FUNDB_OK;

    vfs_dirent_t entry;
    while (vfs_readdir(dir, &entry) == 0) {
        if (entry.name[0] == '.') continue;

        int len = 0;
        while (entry.name[len]) len++;
        if (len < 5) continue;
        if (entry.name[len - 4] != '.' || entry.name[len - 3] != 't' ||
            entry.name[len - 2] != 'b' || entry.name[len - 1] != 'l')
            continue;

        if (db->table_count >= FUNDB_MAX_TABLES) break;

        fundb_table_t *table = &db->tables[db->table_count];
        memset(table, 0, sizeof(fundb_table_t));

        int name_len = len - 4;
        if (name_len > 63) name_len = 63;
        for (int i = 0; i < name_len; i++)
            table->name[i] = entry.name[i];
        table->name[name_len] = '\0';

        if (fundb_read_table(db, table) == FUNDB_OK) {
            table->dirty = 0;

            table->next_auto_id = 1;
            for (uint32_t i = 0; i < table->col_count; i++) {
                if (table->columns[i].auto_increment || table->columns[i].primary_key) {
                    uint32_t max_id = 0;
                    for (uint32_t r = 0; r < table->row_count; r++) {
                        if (table->rows[r].types[i] == FUNDB_TYPE_INT && table->rows[r].values[i]) {
                            uint32_t val = *(uint32_t *)table->rows[r].values[i];
                            if (val > max_id) max_id = val;
                        }
                    }
                    table->next_auto_id = max_id + 1;
                    break;
                }
            }

            for (uint32_t i = 0; i < table->col_count; i++) {
                if (table->columns[i].primary_key) {
                    char idx_name[128];
                    idx_name[0] = 'p'; idx_name[1] = 'k'; idx_name[2] = '_';
                    int p = 3;
                    for (int j = 0; table->name[j] && p < 120; j++)
                        idx_name[p++] = table->name[j];
                    idx_name[p] = '\0';
                    fundb_create_index((fundb_handle_t)db, table->name,
                                       table->columns[i].name, idx_name);
                    break;
                }
            }

            db->table_count++;
        }
    }

    vfs_closedir(dir);
    return FUNDB_OK;
}

/* 打开数据库 */
fundb_handle_t fundb_open(const char *path)
{
    if (!g_fundb_initialized) fundb_init();

    if (g_db_count >= FUNDB_MAX_DBS)
        return NULL;

    fundb_db_t *db = (fundb_db_t *)fundb_alloc(sizeof(fundb_db_t));
    if (!db) return NULL;

    memset(db, 0, sizeof(fundb_db_t));

    for (int i = 0; i < 255 && path[i]; i++)
        db->path[i] = path[i];

    db->table_count = 0;
    db->index_count = 0;
    db->last_error = FUNDB_OK;

    wal_init(db);

    fundb_load_tables(db);

    g_databases[g_db_count] = db;
    g_db_count++;

    klog_info("FunDB: Opened database: %s (%u tables)", path, db->table_count);
    return (fundb_handle_t)db;
}

/* 关闭数据库 */
int fundb_close(fundb_handle_t db)
{
    fundb_db_t *d = (fundb_db_t *)db;
    if (!d) return FUNDB_ERROR;

    /* 写回所有脏表 */
    for (uint32_t i = 0; i < d->table_count; i++) {
        if (d->tables[i].dirty) {
            fundb_write_table(d, &d->tables[i]);
        }
    }

    /* 释放 WAL */
    if (d->wal) {
        fundb_free_mem(d->wal);
    }

    /* 释放索引 */
    for (uint32_t i = 0; i < d->index_count; i++) {
        btree_destroy(d->indexes[i].root);
    }

    /* 从全局列表中移除 */
    for (uint32_t i = 0; i < g_db_count; i++) {
        if (g_databases[i] == d) {
            g_databases[i] = g_databases[g_db_count - 1];
            g_databases[g_db_count - 1] = NULL;
            g_db_count--;
            break;
        }
    }

    fundb_free_mem(d);
    return FUNDB_OK;
}

/* 获取数据库信息 */
int fundb_get_info(fundb_handle_t db, fundb_info_t *info)
{
    fundb_db_t *d = (fundb_db_t *)db;
    if (!d || !info) return FUNDB_ERROR;
    memset(info, 0, sizeof(fundb_info_t));
    strncpy(info->path, d->path, 255);
    info->table_count = d->table_count;
    for (uint32_t i = 0; i < d->table_count && i < 32; i++) {
        strncpy(info->table_names[i], d->tables[i].name, 63);
        info->table_row_counts[i] = d->tables[i].row_count;
    }
    return FUNDB_OK;
}

/* 创建表 */
int fundb_create_table(fundb_handle_t db, const char *name,
                       fundb_column_t *columns, uint32_t col_count)
{
    fundb_db_t *d = (fundb_db_t *)db;
    if (!d) return FUNDB_ERROR;

    if (d->table_count >= FUNDB_MAX_TABLES)
        return FUNDB_ERROR;

    /* 检查表是否已存在 */
    if (find_table(d, name))
        return FUNDB_DUP_KEY;

    fundb_table_t *table = &d->tables[d->table_count];

    /* 设置表名 */
    for (int i = 0; i < 63 && name[i]; i++)
        table->name[i] = name[i];
    table->name[63] = '\0';

    /* 复制列定义 */
    if (columns && col_count > 0) {
        for (uint32_t i = 0; i < col_count && i < FUNDB_MAX_COLUMNS; i++) {
            table->columns[i] = columns[i];
        }
        table->col_count = col_count;
    }

    table->row_count = 0;
    table->next_auto_id = 1;
    table->dirty = 1;

    /* 为自增主键创建索引 */
    for (uint32_t i = 0; i < table->col_count; i++) {
        if (table->columns[i].primary_key) {
            char idx_name[128];
            idx_name[0] = 'p'; idx_name[1] = 'k'; idx_name[2] = '_';
            int pos = 3;
            for (int j = 0; name[j] && pos < 120; j++)
                idx_name[pos++] = name[j];
            idx_name[pos] = '\0';

            fundb_create_index(db, name, table->columns[i].name, idx_name);
            break;
        }
    }

    d->table_count++;

    /* 写入磁盘 */
    fundb_write_table(d, table);

    klog_info("FunDB: Created table: %s (%d columns)", name, col_count);
    return FUNDB_OK;
}

/* 删除表 */
int fundb_drop_table(fundb_handle_t db, const char *name)
{
    fundb_db_t *d = (fundb_db_t *)db;
    if (!d) return FUNDB_ERROR;

    for (uint32_t i = 0; i < d->table_count; i++) {
        int match = 1;
        for (int j = 0; j < 63 && (name[j] || d->tables[i].name[j]); j++) {
            if (name[j] != d->tables[i].name[j]) { match = 0; break; }
        }
        if (match) {
            /* 释放行数据 */
            for (uint32_t r = 0; r < d->tables[i].row_count; r++) {
                fundb_row_free(&d->tables[i].rows[r], d->tables[i].col_count);
            }

            /* 移动后续表 */
            for (uint32_t k = i; k < d->table_count - 1; k++)
                d->tables[k] = d->tables[k + 1];

            d->table_count--;
            return FUNDB_OK;
        }
    }

    return FUNDB_NO_TABLE;
}

/* 修改表结构 */
int fundb_alter_table(fundb_handle_t db, const char *name,
                      fundb_column_t *new_columns, uint32_t new_col_count)
{
    fundb_db_t *d = (fundb_db_t *)db;
    fundb_table_t *table = find_table(d, name);
    if (!table) return FUNDB_NO_TABLE;

    /* 更新列定义 */
    for (uint32_t i = 0; i < new_col_count && i < FUNDB_MAX_COLUMNS; i++) {
        table->columns[i] = new_columns[i];
    }
    table->col_count = new_col_count;
    table->dirty = 1;

    return FUNDB_OK;
}

/* 插入行 */
int fundb_insert(fundb_handle_t db, const char *table_name, fundb_row_t *row)
{
    fundb_db_t *d = (fundb_db_t *)db;
    fundb_table_t *table = find_table(d, table_name);
    if (!table) return FUNDB_NO_TABLE;

    if (table->row_count >= FUNDB_MAX_ROWS)
        return FUNDB_ERROR;

    /* WAL 记录 */
    if (d->in_transaction) {
        wal_append(d, WAL_OP_INSERT, table_name, table->row_count, NULL, 0);
    }

    /* 处理自增列 */
    for (uint32_t i = 0; i < table->col_count; i++) {
        if (table->columns[i].auto_increment && row->types[i] == FUNDB_TYPE_NULL) {
            row->values[i] = fundb_alloc(sizeof(uint32_t));
            if (row->values[i]) {
                *(uint32_t *)row->values[i] = table->next_auto_id++;
                row->sizes[i] = sizeof(uint32_t);
                row->types[i] = FUNDB_TYPE_INT;
            }
        }
    }

    /* 复制行数据 */
    fundb_row_init(&table->rows[table->row_count], table->col_count);
    fundb_row_copy(&table->rows[table->row_count], row, table->col_count);

    /* 更新索引 */
    for (uint32_t i = 0; i < d->index_count; i++) {
        if (strcmp(d->indexes[i].table, table_name) == 0) {
            /* 找到索引列的值作为键 */
            for (uint32_t j = 0; j < table->col_count; j++) {
                if (strcmp(d->indexes[i].column, table->columns[j].name) == 0) {
                    uint32_t key = 0;
                    if (row->types[j] == FUNDB_TYPE_INT && row->values[j]) {
                        key = *(uint32_t *)row->values[j];
                    } else {
                        key = table->row_count;  /* 用行号作为键 */
                    }
                    btree_insert(&d->indexes[i].root, key,
                                 &table->rows[table->row_count]);
                    break;
                }
            }
        }
    }

    table->row_count++;
    table->dirty = 1;

    return FUNDB_OK;
}

/* 更新行 */
int fundb_update(fundb_handle_t db, const char *table_name,
                 const char *where_clause, fundb_row_t *row)
{
    fundb_db_t *d = (fundb_db_t *)db;
    fundb_table_t *table = find_table(d, table_name);
    if (!table) return FUNDB_NO_TABLE;

    if (!row) return FUNDB_ERROR;

    int updated = 0;
    for (uint32_t i = 0; i < table->row_count; i++) {
        if (!evaluate_where(table, &table->rows[i], where_clause))
            continue;

        if (d->in_transaction) {
            wal_append(d, WAL_OP_UPDATE, table_name, i,
                       &table->rows[i], table->col_count);
        }

        for (uint32_t j = 0; j < table->col_count; j++) {
            if (row->types[j] != FUNDB_TYPE_NULL) {
                if (table->rows[i].values[j]) {
                    fundb_free_mem(table->rows[i].values[j]);
                    table->rows[i].values[j] = NULL;
                }
                table->rows[i].types[j] = row->types[j];
                table->rows[i].sizes[j] = row->sizes[j];
                if (row->values[j] && row->sizes[j] > 0) {
                    table->rows[i].values[j] = fundb_alloc(row->sizes[j]);
                    if (table->rows[i].values[j]) {
                        memcpy(table->rows[i].values[j], row->values[j], row->sizes[j]);
                    }
                }
            }
        }
        updated++;
    }

    table->dirty = 1;
    return updated > 0 ? FUNDB_OK : FUNDB_NO_ROW;
}

/* 删除行 */
int fundb_delete(fundb_handle_t db, const char *table_name, const char *where_clause)
{
    fundb_db_t *d = (fundb_db_t *)db;
    fundb_table_t *table = find_table(d, table_name);
    if (!table) return FUNDB_NO_TABLE;

    int deleted = 0;
    for (int i = (int)table->row_count - 1; i >= 0; i--) {
        if (!evaluate_where(table, &table->rows[i], where_clause))
            continue;

        if (d->in_transaction) {
            wal_append(d, WAL_OP_DELETE, table_name, i,
                       &table->rows[i], table->col_count);
        }

        fundb_row_free(&table->rows[i], table->col_count);

        for (uint32_t k = i; k < table->row_count - 1; k++) {
            table->rows[k] = table->rows[k + 1];
        }
        table->row_count--;
        deleted++;
    }

    if (deleted > 0)
        table->dirty = 1;

    return FUNDB_OK;
}

/* 排序比较函数的上下文 */
typedef struct {
    fundb_table_t *table;
    int sort_col;
    int ascending;
} sort_ctx_t;

/* 行交换 */
static void swap_rows(fundb_row_t *a, fundb_row_t *b, uint32_t col_count)
{
    fundb_row_t tmp = *a;
    *a = *b;
    *b = tmp;
}

/* 对结果集进行排序 */
static void sort_result(fundb_result_t *result, int sort_col, int ascending)
{
    if (sort_col < 0 || (uint32_t)sort_col >= result->col_count)
        return;

    for (uint32_t i = 0; i < result->row_count; i++) {
        for (uint32_t j = i + 1; j < result->row_count; j++) {
            int cmp = compare_values(
                result->rows[i].types[sort_col],
                result->rows[i].values[sort_col],
                result->rows[i].sizes[sort_col],
                result->rows[j].types[sort_col],
                result->rows[j].values[sort_col],
                result->rows[j].sizes[sort_col]
            );

            int swap = ascending ? (cmp > 0) : (cmp < 0);
            if (swap) {
                swap_rows(&result->rows[i], &result->rows[j], result->col_count);
            }
        }
    }
}

/* 查询行 */
fundb_result_t *fundb_select(fundb_handle_t db, const char *table_name,
                             const char *columns, const char *where_clause,
                             const char *order_by, uint32_t limit)
{
    fundb_db_t *d = (fundb_db_t *)db;
    fundb_table_t *table = find_table(d, table_name);
    if (!table) return NULL;

    /* 分配结果集 */
    fundb_result_t *result = (fundb_result_t *)fundb_alloc(sizeof(fundb_result_t));
    if (!result) return NULL;

    result->col_count = table->col_count;
    result->columns = (fundb_column_t *)fundb_alloc(sizeof(fundb_column_t) * table->col_count);
    memcpy(result->columns, table->columns, sizeof(fundb_column_t) * table->col_count);

    /* 临时数组存储匹配的行索引 */
    uint32_t *matched = (uint32_t *)fundb_alloc(sizeof(uint32_t) * table->row_count);
    uint32_t matched_count = 0;

    if (matched) {
        /* 应用 WHERE 条件过滤 */
        for (uint32_t i = 0; i < table->row_count; i++) {
            if (evaluate_where(table, &table->rows[i], where_clause)) {
                matched[matched_count++] = i;
            }
        }
    } else {
        /* 内存不足时，回退到所有行 */
        matched_count = table->row_count;
    }

    /* 确定返回行数（应用 LIMIT） */
    uint32_t count = matched_count;
    if (limit > 0 && limit < count) count = limit;

    result->row_count = count;
    result->row_capacity = count;
    result->rows = (fundb_row_t *)fundb_alloc(sizeof(fundb_row_t) * count);

    if (!result->rows) {
        if (matched) fundb_free_mem(matched);
        fundb_free_result(result);
        return NULL;
    }

    /* 复制匹配的行数据 */
    for (uint32_t i = 0; i < count; i++) {
        uint32_t src_idx = matched ? matched[i] : i;
        fundb_row_init(&result->rows[i], table->col_count);
        fundb_row_copy(&result->rows[i], &table->rows[src_idx], table->col_count);
    }

    if (matched) fundb_free_mem(matched);

    /* 应用 ORDER BY 排序 */
    if (order_by && *order_by) {
        char col_name[64];
        int ascending = 1;
        const char *p = order_by;

        while (*p == ' ' || *p == '\t') p++;

        int i = 0;
        while (*p && *p != ' ' && *p != '\t' && i < 63) {
            col_name[i++] = *p++;
        }
        col_name[i] = '\0';

        while (*p == ' ' || *p == '\t') p++;

        if (str_prefix_ci(p, "DESC")) {
            ascending = 0;
        }

        int sort_col = find_column_index(table, col_name);
        if (sort_col >= 0) {
            sort_result(result, sort_col, ascending);
        }
    }

    return result;
}

/* 解析逗号分隔的值列表 */
static int parse_values_list(const char *values_str, fundb_row_t *row,
                             fundb_table_t *table)
{
    const char *p = values_str;
    uint32_t col_idx = 0;

    while (*p && col_idx < table->col_count) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p || *p == ';' || *p == ')') break;

        char val_str[256];
        int vi = 0;

        if (*p == '\'') {
            p++;
            val_str[vi++] = '\'';
            while (*p && *p != '\'' && vi < 254) val_str[vi++] = *p++;
            if (*p == '\'') p++;
            val_str[vi] = '\'';
            val_str[vi + 1] = '\0';
        } else {
            while (*p && *p != ',' && *p != ' ' && *p != '\t' &&
                   *p != ';' && *p != ')' && vi < 254) {
                val_str[vi++] = *p++;
            }
            val_str[vi] = '\0';
        }

        uint32_t val_type;
        void *val_data;
        uint32_t val_size;
        parse_value(val_str, &val_type, &val_data, &val_size);

        row->types[col_idx] = val_type;
        row->sizes[col_idx] = val_size;
        row->values[col_idx] = val_data;

        col_idx++;
    }

    return FUNDB_OK;
}

/* 解析 UPDATE SET 子句 */
static int parse_set_clause(const char *set_str, fundb_row_t *row,
                            fundb_table_t *table)
{
    const char *p = set_str;

    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p || *p == ';') break;

        char col_name[64];
        int ci = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != '=' && ci < 63) {
            col_name[ci++] = *p++;
        }
        col_name[ci] = '\0';

        while (*p == ' ' || *p == '\t') p++;
        if (*p == '=') p++;
        while (*p == ' ' || *p == '\t') p++;

        char val_str[256];
        int vi = 0;
        if (*p == '\'') {
            p++;
            val_str[vi++] = '\'';
            while (*p && *p != '\'' && vi < 254) val_str[vi++] = *p++;
            if (*p == '\'') p++;
            val_str[vi] = '\'';
            val_str[vi + 1] = '\0';
        } else {
            while (*p && *p != ',' && *p != ' ' && *p != '\t' &&
                   *p != ';' && vi < 254) {
                val_str[vi++] = *p++;
            }
            val_str[vi] = '\0';
        }

        int col_idx = find_column_index(table, col_name);
        if (col_idx >= 0) {
            uint32_t val_type;
            void *val_data;
            uint32_t val_size;
            parse_value(val_str, &val_type, &val_data, &val_size);

            row->types[col_idx] = val_type;
            row->sizes[col_idx] = val_size;
            row->values[col_idx] = val_data;
        }
    }

    return FUNDB_OK;
}

/* 执行 SQL 查询 */
fundb_result_t *fundb_query(fundb_handle_t db, const char *sql)
{
    sql_statement_t stmt;
    int rc = sql_parse(sql, &stmt);
    if (rc != FUNDB_OK) return NULL;

    switch (stmt.type) {
    case SQL_SELECT:
        return fundb_select(db, stmt.table, stmt.columns,
                           stmt.where_clause, stmt.order_by, stmt.limit);

    case SQL_INSERT: {
        fundb_table_t *table = find_table((fundb_db_t *)db, stmt.table);
        if (!table) return NULL;

        fundb_row_t row;
        fundb_row_init(&row, table->col_count);

        if (stmt.values[0]) {
            parse_values_list(stmt.values, &row, table);
        }

        fundb_insert(db, stmt.table, &row);
        fundb_row_free(&row, table->col_count);
        return NULL;
    }

    case SQL_UPDATE: {
        fundb_table_t *table = find_table((fundb_db_t *)db, stmt.table);
        if (!table) return NULL;

        fundb_row_t row;
        fundb_row_init(&row, table->col_count);

        if (stmt.values[0]) {
            parse_set_clause(stmt.values, &row, table);
        }

        fundb_update(db, stmt.table, stmt.where_clause, &row);
        fundb_row_free(&row, table->col_count);
        return NULL;
    }

    case SQL_DELETE:
        fundb_delete(db, stmt.table, stmt.where_clause);
        return NULL;

    case SQL_CREATE:
        fundb_create_table(db, stmt.table, stmt.new_columns, stmt.new_col_count);
        return NULL;

    case SQL_DROP:
        fundb_drop_table(db, stmt.table);
        return NULL;

    default:
        return NULL;
    }
}

/* 释放结果集 */
void fundb_free_result(fundb_result_t *result)
{
    if (!result) return;

    if (result->rows) {
        for (uint32_t i = 0; i < result->row_count; i++) {
            fundb_row_free(&result->rows[i], result->col_count);
        }
        fundb_free_mem(result->rows);
    }

    if (result->columns)
        fundb_free_mem(result->columns);

    fundb_free_mem(result);
}

/* 开始事务 */
int fundb_begin(fundb_handle_t db)
{
    fundb_db_t *d = (fundb_db_t *)db;
    if (!d) return FUNDB_ERROR;

    d->in_transaction = 1;
    wal_clear(d);
    return FUNDB_OK;
}

/* 提交事务 */
int fundb_commit(fundb_handle_t db)
{
    fundb_db_t *d = (fundb_db_t *)db;
    if (!d) return FUNDB_ERROR;

    /* 标记所有 WAL 条目为已提交 */
    for (uint32_t i = 0; i < d->wal_count; i++) {
        d->wal[i].committed = 1;
    }

    /* 写回所有脏表 */
    for (uint32_t i = 0; i < d->table_count; i++) {
        if (d->tables[i].dirty) {
            fundb_write_table(d, &d->tables[i]);
        }
    }

    d->in_transaction = 0;
    wal_clear(d);

    return FUNDB_OK;
}

/* 回滚事务 */
int fundb_rollback(fundb_handle_t db)
{
    fundb_db_t *d = (fundb_db_t *)db;
    if (!d) return FUNDB_ERROR;

    /* 从后往前回滚 WAL 条目 */
    for (int i = (int)d->wal_count - 1; i >= 0; i--) {
        wal_entry_t *entry = &d->wal[i];
        fundb_table_t *table = find_table(d, entry->table);
        if (!table) continue;

        switch (entry->operation) {
        case WAL_OP_INSERT:
            /* 回滚插入 = 删除该行 */
            if (entry->row_index < table->row_count) {
                fundb_row_free(&table->rows[entry->row_index], table->col_count);
                for (uint32_t k = entry->row_index; k < table->row_count - 1; k++)
                    table->rows[k] = table->rows[k + 1];
                table->row_count--;
            }
            break;

        case WAL_OP_UPDATE:
            /* 回滚更新 = 恢复旧数据 */
            if (entry->row_index < table->row_count) {
                fundb_row_free(&table->rows[entry->row_index], table->col_count);
                fundb_row_copy(&table->rows[entry->row_index], &entry->old_data, table->col_count);
            }
            break;

        case WAL_OP_DELETE:
            /* 回滚删除 = 恢复行 */
            /* 简化：增加行计数 */
            table->row_count++;
            break;
        }
    }

    d->in_transaction = 0;
    wal_clear(d);

    return FUNDB_OK;
}

/* 创建索引 */
int fundb_create_index(fundb_handle_t db, const char *table_name,
                       const char *column, const char *index_name)
{
    fundb_db_t *d = (fundb_db_t *)db;
    if (!d) return FUNDB_ERROR;

    if (d->index_count >= 64)
        return FUNDB_ERROR;

    fundb_index_t *idx = &d->indexes[d->index_count];

    for (int i = 0; i < 63 && index_name[i]; i++)
        idx->name[i] = index_name[i];
    idx->name[63] = '\0';

    for (int i = 0; i < 63 && table_name[i]; i++)
        idx->table[i] = table_name[i];
    idx->table[63] = '\0';

    for (int i = 0; i < 63 && column[i]; i++)
        idx->column[i] = column[i];
    idx->column[63] = '\0';

    /* 创建 B-tree */
    idx->root = btree_create_node(1);

    /* 为已有数据建立索引 */
    fundb_table_t *table = find_table(d, table_name);
    if (table) {
        for (uint32_t i = 0; i < table->row_count; i++) {
            /* 找到索引列 */
            for (uint32_t j = 0; j < table->col_count; j++) {
                if (strcmp(table->columns[j].name, column) == 0) {
                    uint32_t key = 0;
                    if (table->rows[i].types[j] == FUNDB_TYPE_INT && table->rows[i].values[j]) {
                        key = *(uint32_t *)table->rows[i].values[j];
                    } else {
                        key = i;
                    }
                    btree_insert(&idx->root, key, &table->rows[i]);
                    break;
                }
            }
        }
    }

    d->index_count++;
    return FUNDB_OK;
}

/* 删除索引 */
int fundb_drop_index(fundb_handle_t db, const char *index_name)
{
    fundb_db_t *d = (fundb_db_t *)db;
    if (!d) return FUNDB_ERROR;

    for (uint32_t i = 0; i < d->index_count; i++) {
        if (strcmp(d->indexes[i].name, index_name) == 0) {
            btree_destroy(d->indexes[i].root);
            for (uint32_t k = i; k < d->index_count - 1; k++)
                d->indexes[k] = d->indexes[k + 1];
            d->index_count--;
            return FUNDB_OK;
        }
    }

    return FUNDB_ERROR;
}

/* 检查表是否存在 */
int fundb_table_exists(fundb_handle_t db, const char *name)
{
    fundb_db_t *d = (fundb_db_t *)db;
    return find_table(d, name) != NULL;
}

/* 获取表的行数 */
uint32_t fundb_row_count(fundb_handle_t db, const char *table_name)
{
    fundb_db_t *d = (fundb_db_t *)db;
    fundb_table_t *table = find_table(d, table_name);
    return table ? table->row_count : 0;
}

/* 获取错误描述 */
const char *fundb_error_string(int error)
{
    switch (error) {
    case FUNDB_OK:         return "OK";
    case FUNDB_ERROR:      return "General error";
    case FUNDB_NO_TABLE:   return "Table not found";
    case FUNDB_NO_COLUMN:  return "Column not found";
    case FUNDB_DUP_KEY:    return "Duplicate key";
    case FUNDB_CONSTRAINT: return "Constraint violation";
    case FUNDB_NO_MEMORY:  return "Out of memory";
    case FUNDB_IO_ERROR:   return "I/O error";
    case FUNDB_CORRUPT:    return "Database corrupt";
    case FUNDB_BUSY:       return "Database busy";
    case FUNDB_NO_ROW:     return "No row found";
    default:               return "Unknown error";
    }
}
