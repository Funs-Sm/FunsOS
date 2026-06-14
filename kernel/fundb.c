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

        /* 简化：不解析列定义，使用默认列 */
        stmt->new_col_count = 0;

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

    /* 初始化 WAL */
    wal_init(db);

    /* 注册数据库 */
    g_databases[g_db_count] = db;
    g_db_count++;

    klog_info("FunDB: Opened database: %s", path);
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

    /* 简化实现：更新所有匹配行 */
    int updated = 0;
    for (uint32_t i = 0; i < table->row_count; i++) {
        /* WAL 记录 */
        if (d->in_transaction) {
            wal_append(d, WAL_OP_UPDATE, table_name, i,
                       &table->rows[i], table->col_count);
        }

        /* 更新行数据 */
        fundb_row_copy(&table->rows[i], row, table->col_count);
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

    /* 简化实现：删除所有匹配行 */
    if (table->row_count > 0) {
        /* WAL 记录 */
        if (d->in_transaction) {
            wal_append(d, WAL_OP_DELETE, table_name, 0,
                       &table->rows[0], table->col_count);
        }

        /* 释放最后一行 */
        fundb_row_free(&table->rows[table->row_count - 1], table->col_count);
        table->row_count--;
        table->dirty = 1;
    }

    return FUNDB_OK;
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

    /* 确定返回行数 */
    uint32_t count = table->row_count;
    if (limit > 0 && limit < count) count = limit;

    result->row_count = count;
    result->row_capacity = count;
    result->rows = (fundb_row_t *)fundb_alloc(sizeof(fundb_row_t) * count);

    /* 复制行数据 */
    for (uint32_t i = 0; i < count; i++) {
        fundb_row_init(&result->rows[i], table->col_count);
        fundb_row_copy(&result->rows[i], &table->rows[i], table->col_count);
    }

    return result;
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
        /* 简化：创建一个空行并插入 */
        fundb_table_t *table = find_table((fundb_db_t *)db, stmt.table);
        if (!table) return NULL;

        fundb_row_t row;
        fundb_row_init(&row, table->col_count);
        fundb_insert(db, stmt.table, &row);
        fundb_row_free(&row, table->col_count);
        return NULL;
    }

    case SQL_UPDATE:
        fundb_update(db, stmt.table, stmt.where_clause, NULL);
        return NULL;

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
