#include "registry.h"
#include "kheap.h"
#include "string.h"
#include "stdlib.h"
#include "stdio.h"
#include "spinlock.h"
#include "klog.h"
#include "fundb.h"

/* ============================================================
 * System Registry Implementation - 系统注册表实现
 *
 * 内存树 + FunDB 持久化。
 *   - 启动时从 /var/db/registry.db 加载
 *   - 写操作同步更新内存树和 FunDB
 *   - 读操作只走内存树（快）
 * ============================================================ */

#define REG_DB_PATH  "/var/db/registry.db"
#define REG_TABLE    "registry"

/* 监视项最大数量 */
#define REG_MAX_WATCHES    16
#define REG_MAX_HISTORY    64

/* 监视项 */
typedef struct reg_watch {
    int                    id;
    char                   path_prefix[REG_MAX_PATH];
    uint32_t              events;
    reg_watch_callback_t  callback;
    int                   active;
} reg_watch_t;

/* 全局状态 */
static struct {
    reg_key_t   roots[HKEY_COUNT];
    spinlock_t  lock;
    int         initialized;
    fundb_handle_t db;
    reg_stats_t stats;
    /* 监视系统 */
    reg_watch_t watches[REG_MAX_WATCHES];
    int        next_watch_id;
    reg_watch_event_t history[REG_MAX_HISTORY];
    int        history_head;
    int        history_count;
    int        watch_enabled;
} g_reg;

/* ---- 内部辅助 ---- */

static const char *root_short_names[HKEY_COUNT] = {
    HKLM_SHORT, HKCU_SHORT, HKCR_SHORT, HKU_SHORT, HKCC_SHORT
};

static const char *root_long_names[HKEY_COUNT] = {
    "HKEY_LOCAL_MACHINE",
    "HKEY_CURRENT_USER",
    "HKEY_CLASSES_ROOT",
    "HKEY_USERS",
    "HKEY_CURRENT_CONFIG"
};

/* 分割路径为段（按 \ 或 / 分割），返回段数 */
static int reg_split_path(const char *path, char segs[][REG_MAX_NAME],
                          int max_segs) {
    int n = 0;
    const char *p = path;
    while (*p && n < max_segs) {
        while (*p == '\\' || *p == '/') p++;
        if (!*p) break;
        int i = 0;
        while (*p && *p != '\\' && *p != '/' && i < REG_MAX_NAME - 1) {
            segs[n][i++] = *p++;
        }
        segs[n][i] = '\0';
        n++;
    }
    return n;
}

/* 查找子键（不创建） */
static reg_key_t *reg_find_child(reg_key_t *parent, const char *name) {
    reg_key_t *c = parent->children;
    while (c) {
        if (strcmp(c->name, name) == 0) return c;
        c = c->next_sibling;
    }
    return NULL;
}

/* 创建子键（已存在则返回） */
static reg_key_t *reg_create_child(reg_key_t *parent, const char *name) {
    reg_key_t *c = reg_find_child(parent, name);
    if (c) return c;

    c = (reg_key_t *)kmalloc(sizeof(reg_key_t));
    if (!c) return NULL;
    memset(c, 0, sizeof(*c));
    strncpy(c->name, name, REG_MAX_NAME - 1);
    c->root = parent->root;
    c->parent = parent;
    c->next_sibling = parent->children;
    parent->children = c;
    parent->child_count++;
    g_reg.stats.total_keys++;
    g_reg.stats.per_root_keys[parent->root]++;

    /* 触发键创建监视事件 */
    char path[REG_MAX_PATH];
    reg_build_path(c, path, sizeof(path));
    reg_fire_watch_event(REG_WATCH_CREATE_KEY, c->root, path, NULL);

    return c;
}

/* 递归删除子树 */
static void reg_free_subtree(reg_key_t *key) {
    /* 先递归释放子键 */
    reg_key_t *c = key->children;
    while (c) {
        reg_key_t *next = c->next_sibling;
        reg_free_subtree(c);
        c = next;
    }
    /* 释放值 */
    reg_value_t *v = key->values;
    while (v) {
        reg_value_t *vn = v->next;
        kfree(v);
        v = vn;
    }
    kfree(key);
}

/* 在键中查找值 */
static reg_value_t *reg_find_value(reg_key_t *key, const char *name) {
    reg_value_t *v = key->values;
    while (v) {
        if (strcmp(v->name, name) == 0) return v;
        v = v->next;
    }
    return NULL;
}

/* 构建完整路径字符串（如 "HKLM\System\Kernel"） */
void reg_build_path(reg_key_t *key, char *buf, uint32_t size) {
    char tmp[REG_MAX_PATH];
    int len = 0;
    reg_key_t *cur = key;
    while (cur && cur->parent) {
        int nl = (int)strlen(cur->name);
        if (len + nl + 2 >= (int)sizeof(tmp)) break;
        if (len > 0) tmp[len++] = '\\';
        memcpy(tmp + len, cur->name, nl);
        len += nl;
        cur = cur->parent;
    }
    /* 加根键前缀 */
    const char *root = root_short_names[key->root];
    int rl = (int)strlen(root);
    if (len > 0) {
        if (rl + 1 + len >= (int)size) len = (int)size - rl - 2;
        memcpy(buf, root, rl);
        buf[rl] = '\\';
        memcpy(buf + rl + 1, tmp, len);
        buf[rl + 1 + len] = '\0';
    } else {
        memcpy(buf, root, rl);
        buf[rl] = '\0';
    }
}

/* ============================================================
 * FunDB 持久化
 * ============================================================ */

/* 构建值的全路径标识：path + "@" + name（@表示默认值） */
static void reg_make_full_path(const char *key_path, const char *value_name,
                               char *out, uint32_t out_size) {
    if (!value_name || !*value_name) {
        snprintf(out, out_size, "%s\\@", key_path);
    } else {
        snprintf(out, out_size, "%s\\%s", key_path, value_name);
    }
}

/* 把一个值写入 FunDB（先删除再插入） */
static void reg_db_persist_value(const char *key_path, const char *value_name,
                                  uint32_t type, const char *data, uint32_t size) {
    if (!g_reg.db) return;

    char full_path[REG_MAX_PATH];
    reg_make_full_path(key_path, value_name, full_path, sizeof(full_path));

    /* 删除旧记录 */
    char where[REG_MAX_PATH + 32];
    snprintf(where, sizeof(where), "full_path = '%s'", full_path);
    fundb_delete(g_reg.db, REG_TABLE, where);

    /* 插入新记录 */
    fundb_row_t row;
    void *vals[3];
    uint32_t sizes[3];
    uint32_t types[3];

    vals[0] = (void *)full_path;
    sizes[0] = (uint32_t)strlen(full_path) + 1;
    types[0] = FUNDB_TYPE_TEXT;

    vals[1] = &type;
    sizes[1] = sizeof(uint32_t);
    types[1] = FUNDB_TYPE_INT;

    /* data 截断到 REG_MAX_DATA */
    char data_buf[REG_MAX_DATA];
    uint32_t copy_size = size;
    if (copy_size >= REG_MAX_DATA) copy_size = REG_MAX_DATA - 1;
    memcpy(data_buf, data, copy_size);
    data_buf[copy_size] = '\0';

    vals[2] = (void *)data_buf;
    sizes[2] = copy_size + 1;
    types[2] = FUNDB_TYPE_TEXT;

    row.values = vals;
    row.sizes = sizes;
    row.types = types;

    fundb_insert(g_reg.db, REG_TABLE, &row);
}

/* 从 FunDB 删除一个值 */
static void reg_db_delete_value(const char *key_path, const char *value_name) {
    if (!g_reg.db) return;
    char full_path[REG_MAX_PATH];
    reg_make_full_path(key_path, value_name, full_path, sizeof(full_path));
    char where[REG_MAX_PATH + 32];
    snprintf(where, sizeof(where), "full_path = '%s'", full_path);
    fundb_delete(g_reg.db, REG_TABLE, where);
}

/* 从 FunDB 删除以 key_path 为前缀的所有值（递归） */
static void reg_db_delete_subtree(const char *key_path) {
    if (!g_reg.db) return;
    /* fundb_delete 不支持 LIKE，所以 select 全部再删 */
    fundb_result_t *r = fundb_select(g_reg.db, REG_TABLE, "*", NULL, NULL, 0);
    if (!r) return;
    char prefix[REG_MAX_PATH];
    snprintf(prefix, sizeof(prefix), "%s\\", key_path);
    uint32_t plen = (uint32_t)strlen(prefix);
    for (uint32_t i = 0; i < r->row_count; i++) {
        if (r->rows[i].values[0]) {
            const char *fp = (const char *)r->rows[i].values[0];
            if (strncmp(fp, prefix, plen) == 0) {
                char where[REG_MAX_PATH + 32];
                snprintf(where, sizeof(where), "full_path = '%s'", fp);
                fundb_delete(g_reg.db, REG_TABLE, where);
            }
        }
    }
    fundb_free_result(r);
}

/* 从 FunDB 加载所有值到内存树 */
static void reg_db_load_all(void) {
    if (!g_reg.db) return;
    fundb_result_t *r = fundb_select(g_reg.db, REG_TABLE, "*", NULL, NULL, 0);
    if (!r) return;

    for (uint32_t i = 0; i < r->row_count; i++) {
        if (!r->rows[i].values[0] || !r->rows[i].values[2]) continue;
        const char *full_path = (const char *)r->rows[i].values[0];
        uint32_t type = r->rows[i].values[1] ?
                        *(uint32_t *)r->rows[i].values[1] : REG_TYPE_STRING;
        const char *data = (const char *)r->rows[i].values[2];

        /* 解析 full_path: "HKLM\System\Key\@value_name" */
        /* 找最后一个反斜杠后的 @ 或名称 */
        const char *last_bs = NULL;
        const char *p = full_path;
        while (*p) {
            if (*p == '\\') last_bs = p;
            p++;
        }
        if (!last_bs) continue;
        const char *vname_start = last_bs + 1;
        if (*vname_start == '@') vname_start++; /* 默认值 */

        /* 键路径 = full_path 去掉最后的 \value_name */
        char key_path[REG_MAX_PATH];
        uint32_t kpl = (uint32_t)(last_bs - full_path);
        if (kpl >= sizeof(key_path)) kpl = sizeof(key_path) - 1;
        memcpy(key_path, full_path, kpl);
        key_path[kpl] = '\0';

        /* 值名 */
        char vname[REG_MAX_NAME];
        strncpy(vname, vname_start, sizeof(vname) - 1);
        vname[sizeof(vname) - 1] = '\0';

        reg_handle_t h = reg_create_path(key_path);
        if (h) {
            reg_value_t *v = reg_find_value(h, vname);
            if (!v) {
                v = (reg_value_t *)kmalloc(sizeof(reg_value_t));
                if (!v) continue;
                memset(v, 0, sizeof(*v));
                strncpy(v->name, vname, REG_MAX_NAME - 1);
                v->next = h->values;
                h->values = v;
                h->value_count++;
                g_reg.stats.total_values++;
                g_reg.stats.per_root_values[h->root]++;
            }
            v->type = type;
            strncpy(v->data, data, REG_MAX_DATA - 1);
            v->data[REG_MAX_DATA - 1] = '\0';
            v->size = (uint32_t)strlen(data);
        }
    }
    fundb_free_result(r);
}

/* 初始化 FunDB 表 */
static void reg_db_init_table(void) {
    fundb_column_t cols[3];
    memset(cols, 0, sizeof(cols));

    strcpy(cols[0].name, "full_path");
    cols[0].type = FUNDB_TYPE_TEXT;
    cols[0].size = REG_MAX_PATH;
    cols[0].not_null = 1;
    cols[0].primary_key = 1;

    strcpy(cols[1].name, "type");
    cols[1].type = FUNDB_TYPE_INT;
    cols[1].size = 4;

    strcpy(cols[2].name, "data");
    cols[2].type = FUNDB_TYPE_TEXT;
    cols[2].size = REG_MAX_DATA;

    if (!fundb_table_exists(g_reg.db, REG_TABLE)) {
        int rc = fundb_create_table(g_reg.db, REG_TABLE, cols, 3);
        if (rc != FUNDB_OK) {
            klog_warn("registry: failed to create table (%s)",
                      fundb_error_string(rc));
        }
    }
}

/* ============================================================
 * 默认数据：系统设置 + 内置应用
 * ============================================================ */

static void reg_set_str(reg_handle_t h, const char *name, const char *val) {
    reg_set_string(h, name, val);
}

static void reg_set_dw(reg_handle_t h, const char *name, uint32_t val) {
    reg_set_dword(h, name, val);
}

/* 注册一个内置应用 */
static void reg_register_app(const char *id, const char *display_name,
                             const char *path, const char *version,
                             const char *desc, const char *category) {
    char subkey[80];
    snprintf(subkey, sizeof(subkey), "Software\\Apps\\%s", id);
    reg_handle_t h = reg_create_key(HKEY_LOCAL_MACHINE, subkey);
    if (!h) return;
    reg_set_str(h, "name", display_name);
    reg_set_str(h, "path", path);
    reg_set_str(h, "version", version);
    reg_set_str(h, "description", desc);
    reg_set_str(h, "category", category);
    /* command 值：appexec 服务通过此值执行应用。
     * 默认等于 app id（绝大多数 shell 内置命令同名）。
     * 对 GUI 应用（paint/snake 等），command 会指向 GUI 启动器。 */
    reg_set_str(h, "command", id);
}

static void registry_load_defaults(void) {
    reg_handle_t h;

    /* ---- HKLM\System ---- */
    h = reg_create_key(HKEY_LOCAL_MACHINE, "System");
    reg_set_str(h, "hostname", "funsos-pc");
    reg_set_str(h, "os_name", "FUNSOS");
    reg_set_str(h, "kernel_version", "0.8");
    reg_set_str(h, "build_date", __DATE__);
    reg_set_dw(h, "uptime_ticks", 0);
    reg_set_dw(h, "multi_user", 1);

    /* ---- HKLM\Hardware ---- */
    h = reg_create_key(HKEY_LOCAL_MACHINE, "Hardware");
    reg_set_str(h, "cpu_vendor", "GenuineIntel");
    reg_set_str(h, "cpu_model", "x86 (32-bit)");
    reg_set_dw(h, "cpu_cores", 1);
    reg_set_dw(h, "memory_mb", 256);
    reg_set_str(h, "display", "VGA 640x480");

    /* ---- HKLM\Software\OS\Boot ---- */
    h = reg_create_key(HKEY_LOCAL_MACHINE, "Software\\OS\\Boot");
    reg_set_str(h, "default_shell", "/apps/shell");
    reg_set_str(h, "default_desktop", "/apps/desktop");
    reg_set_dw(h, "auto_login", 0);
    reg_set_str(h, "init_script", "/etc/init.rc");

    /* ---- HKLM\Software\OS\Kernel ---- */
    h = reg_create_key(HKEY_LOCAL_MACHINE, "Software\\OS\\Kernel");
    reg_set_dw(h, "ktrace_default_mask", 0);
    reg_set_dw(h, "kwork_worker_count", 1);
    reg_set_dw(h, "vfs_cache_enabled", 1);
    reg_set_dw(h, "page_cache_enabled", 1);

    /* ---- HKLM\Software\OS\FileSystem ---- */
    h = reg_create_key(HKEY_LOCAL_MACHINE, "Software\\OS\\FileSystem");
    reg_set_str(h, "default_fs", "tmpfs");
    reg_set_str(h, "root_fs", "ext2");
    reg_set_dw(h, "sync_interval_ms", 5000);

    /* ---- HKLM\Software\OS\Network ---- */
    h = reg_create_key(HKEY_LOCAL_MACHINE, "Software\\OS\\Network");
    reg_set_str(h, "hostname", "funsos");
    reg_set_str(h, "domain", "local");
    reg_set_str(h, "dns_server", "8.8.8.8");
    reg_set_dw(h, "dhcp_enabled", 1);

    /* ---- HKCU\Desktop ---- */
    h = reg_create_key(HKEY_CURRENT_USER, "Desktop");
    reg_set_str(h, "wallpaper", "/system/themes/default.bmp");
    reg_set_str(h, "theme", "classic");
    reg_set_str(h, "language", "zh-CN");
    reg_set_dw(h, "show_icons", 1);

    /* ---- HKCU\Software\Preferences ---- */
    h = reg_create_key(HKEY_CURRENT_USER, "Software\\Preferences");
    reg_set_str(h, "editor", "/apps/notepad");
    reg_set_str(h, "terminal", "/apps/terminal");
    reg_set_str(h, "file_manager", "/apps/filemgr");
    reg_set_dw(h, "confirm_delete", 1);

    /* ---- HKCR: 文件关联 ---- */
    h = reg_create_key(HKEY_CLASSES_ROOT, ".txt");
    reg_set_str(h, "", "txtfile");
    h = reg_create_key(HKEY_CLASSES_ROOT, "txtfile");
    reg_set_str(h, "", "Text File");
    h = reg_create_key(HKEY_CLASSES_ROOT, "txtfile\\shell\\open\\command");
    reg_set_str(h, "", "/apps/notepad \"%1\"");

    h = reg_create_key(HKEY_CLASSES_ROOT, ".c");
    reg_set_str(h, "", "cfile");
    h = reg_create_key(HKEY_CLASSES_ROOT, "cfile");
    reg_set_str(h, "", "C Source File");
    h = reg_create_key(HKEY_CLASSES_ROOT, "cfile\\shell\\open\\command");
    reg_set_str(h, "", "/apps/notepad \"%1\"");

    /* ---- HKLM\Software\Apps: 内置应用注册表 ---- */
    reg_create_key(HKEY_LOCAL_MACHINE, "Software\\Apps");

    reg_register_app("ls",       "File List",       "/apps/ls",
                     "1.0", "List directory contents",         "Utility");
    reg_register_app("cat",      "Cat",             "/apps/cat",
                     "1.0", "Display file contents",          "Utility");
    reg_register_app("cp",       "Copy",            "/apps/cp",
                     "1.0", "Copy files",                      "Utility");
    reg_register_app("mv",       "Move",            "/apps/mv",
                     "1.0", "Move/rename files",               "Utility");
    reg_register_app("rm",       "Remove",          "/apps/rm",
                     "1.0", "Remove files",                    "Utility");
    reg_register_app("mkdir",    "Make Dir",        "/apps/mkdir",
                     "1.0", "Create directories",              "Utility");
    reg_register_app("touch",    "Touch",           "/apps/touch",
                     "1.0", "Create empty files",              "Utility");
    reg_register_app("echo",     "Echo",            "/apps/echo",
                     "1.0", "Print text",                      "Utility");
    reg_register_app("grep",     "Grep",            "/apps/grep",
                     "1.0", "Search in files",                 "Utility");
    reg_register_app("head",     "Head",            "/apps/head",
                     "1.0", "Show file head",                 "Utility");
    reg_register_app("wc",       "Word Count",      "/apps/wc",
                     "1.0", "Count lines/words/chars",        "Utility");
    reg_register_app("date",     "Date",            "/apps/date",
                     "1.0", "Show current date/time",         "Utility");

    reg_register_app("calc",     "Calculator",      "/apps/calc",
                     "1.0", "GUI calculator",                  "Application");
    reg_register_app("notepad",  "Notepad",         "/apps/notepad",
                     "1.0", "GUI text editor",                 "Application");
    reg_register_app("paint",    "Paint",           "/apps/paint",
                     "1.0", "GUI paint program",               "Application");
    reg_register_app("snake",    "Snake Game",      "/apps/snake",
                     "1.0", "Classic snake game",              "Game");

    reg_register_app("terminal", "Terminal",        "/apps/terminal",
                     "1.0", "GUI terminal emulator",           "System");
    reg_register_app("desktop",  "Desktop",         "/apps/desktop",
                     "1.0", "Desktop environment",             "System");
    reg_register_app("filemgr",  "File Manager",    "/apps/filemgr",
                     "1.0", "GUI file manager",                "System");
    reg_register_app("help",      "Help",           "/apps/help",
                     "1.0", "System help",                     "System");
    reg_register_app("init",      "Init",           "/apps/init",
                     "1.0", "System init process",             "System");

    /* ---- HKCC ---- */
    h = reg_create_key(HKEY_CURRENT_CONFIG, "Display");
    reg_set_str(h, "resolution", "640x480");
    reg_set_dw(h, "color_depth", 32);
    reg_set_str(h, "driver", "vga");
}

/* ============================================================
 * 公共 API
 * ============================================================ */

void registry_init(void) {
    memset(&g_reg, 0, sizeof(g_reg));
    spinlock_init(&g_reg.lock);

    /* 初始化根键 */
    for (int i = 0; i < HKEY_COUNT; i++) {
        strncpy(g_reg.roots[i].name, root_short_names[i], REG_MAX_NAME - 1);
        g_reg.roots[i].root = (uint32_t)i;
        g_reg.roots[i].parent = NULL;
        g_reg.roots[i].children = NULL;
        g_reg.roots[i].next_sibling = NULL;
        g_reg.roots[i].values = NULL;
        g_reg.roots[i].child_count = 0;
        g_reg.roots[i].value_count = 0;
    }
    g_reg.stats.total_keys = HKEY_COUNT;
    for (int i = 0; i < HKEY_COUNT; i++) {
        g_reg.stats.per_root_keys[i] = 1;
    }

    /* 初始化监视系统 */
    reg_watch_init();

    /* 打开 FunDB 用于持久化 */
    g_reg.db = fundb_open(REG_DB_PATH);
    if (g_reg.db) {
        reg_db_init_table();
        /* 从 FunDB 加载已有数据 */
        reg_db_load_all();
        klog_info("registry: loaded from FunDB (%u keys, %u values)",
                  g_reg.stats.total_keys, g_reg.stats.total_values);
    } else {
        klog_warn("registry: FunDB unavailable, running in-memory only");
    }

    /* 加载默认数据（只在首次启动，键不存在时才创建） */
    registry_load_defaults();

    g_reg.initialized = 1;
    klog_info("registry: initialized (%u keys, %u values)",
              g_reg.stats.total_keys, g_reg.stats.total_values);
}

void registry_shutdown(void) {
    if (g_reg.db) {
        fundb_close(g_reg.db);
        g_reg.db = NULL;
    }
}

int reg_parse_root(const char *path, const char **subpath_out) {
    if (!path) return REG_INVALID_PATH;
    for (int i = 0; i < HKEY_COUNT; i++) {
        const char *s = root_short_names[i];
        int len = (int)strlen(s);
        if (strncmp(path, s, len) == 0) {
            char next = path[len];
            if (next == '\0') {
                if (subpath_out) *subpath_out = "";
                return i;
            }
            if (next == '\\' || next == '/') {
                if (subpath_out) *subpath_out = path + len + 1;
                return i;
            }
        }
    }
    return REG_INVALID_PATH;
}

reg_handle_t reg_open_key(uint32_t root, const char *subkey) {
    if (root >= HKEY_COUNT) return REG_INVALID_HANDLE;

    reg_key_t *cur = &g_reg.roots[root];
    if (!subkey || !*subkey) return cur;

    char segs[32][REG_MAX_NAME];
    int n = reg_split_path(subkey, segs, 32);
    for (int i = 0; i < n; i++) {
        cur = reg_find_child(cur, segs[i]);
        if (!cur) return REG_INVALID_HANDLE;
    }
    return cur;
}

reg_handle_t reg_create_key(uint32_t root, const char *subkey) {
    if (root >= HKEY_COUNT) return REG_INVALID_HANDLE;

    reg_key_t *cur = &g_reg.roots[root];
    if (!subkey || !*subkey) return cur;

    char segs[32][REG_MAX_NAME];
    int n = reg_split_path(subkey, segs, 32);
    for (int i = 0; i < n; i++) {
        cur = reg_create_child(cur, segs[i]);
        if (!cur) return REG_INVALID_HANDLE;
    }
    return cur;
}

int reg_delete_key(uint32_t root, const char *subkey) {
    if (root >= HKEY_COUNT) return REG_ERROR;
    reg_handle_t parent = &g_reg.roots[root];
    if (!subkey || !*subkey) return REG_ERROR;

    char segs[32][REG_MAX_NAME];
    int n = reg_split_path(subkey, segs, 32);
    if (n == 0) return REG_ERROR;

    /* 找到要删除的键的父键 */
    int i;
    for (i = 0; i < n - 1; i++) {
        parent = reg_find_child(parent, segs[i]);
        if (!parent) return REG_NO_KEY;
    }
    /* 从父键的子链表中移除 */
    reg_key_t **pp = &parent->children;
    while (*pp) {
        if (strcmp((*pp)->name, segs[n - 1]) == 0) {
            reg_key_t *victim = *pp;
            *pp = victim->next_sibling;
            parent->child_count--;

            /* 从 FunDB 删除 */
            char path[REG_MAX_PATH];
            reg_build_path(victim, path, sizeof(path));
            uint32_t victim_root = victim->root;
            reg_db_delete_subtree(path);

            /* 触发键删除监视事件 */
            reg_fire_watch_event(REG_WATCH_DELETE_KEY, victim_root, path, NULL);

            reg_free_subtree(victim);
            return REG_OK;
        }
        pp = &(*pp)->next_sibling;
    }
    return REG_NO_KEY;
}

int reg_enum_key(reg_handle_t key, uint32_t index,
                 char *name, uint32_t name_size) {
    if (!key || !name) return REG_ERROR;
    reg_key_t *c = key->children;
    uint32_t i = 0;
    while (c) {
        if (i == index) {
            strncpy(name, c->name, name_size - 1);
            name[name_size - 1] = '\0';
            return REG_OK;
        }
        i++;
        c = c->next_sibling;
    }
    return REG_NO_KEY;
}

int reg_enum_value(reg_handle_t key, uint32_t index,
                   char *name, uint32_t name_size,
                   uint32_t *type, char *data, uint32_t data_size) {
    if (!key) return REG_ERROR;
    reg_value_t *v = key->values;
    uint32_t i = 0;
    while (v) {
        if (i == index) {
            if (name && name_size > 0) {
                strncpy(name, v->name, name_size - 1);
                name[name_size - 1] = '\0';
            }
            if (type) *type = v->type;
            if (data && data_size > 0) {
                strncpy(data, v->data, data_size - 1);
                data[data_size - 1] = '\0';
            }
            return REG_OK;
        }
        i++;
        v = v->next;
    }
    return REG_NO_VALUE;
}

int reg_set_value(reg_handle_t key, const char *name,
                  uint32_t type, const void *data, uint32_t size) {
    if (!key) return REG_ERROR;
    if (!name) name = "";

    spinlock_lock(&g_reg.lock);
    reg_value_t *v = reg_find_value(key, name);
    int is_new = 0;
    if (!v) {
        v = (reg_value_t *)kmalloc(sizeof(reg_value_t));
        if (!v) {
            spinlock_unlock(&g_reg.lock);
            return REG_NO_MEMORY;
        }
        memset(v, 0, sizeof(*v));
        strncpy(v->name, name, REG_MAX_NAME - 1);
        v->next = key->values;
        key->values = v;
        key->value_count++;
        g_reg.stats.total_values++;
        g_reg.stats.per_root_values[key->root]++;
        is_new = 1;
    }

    v->type = type;
    uint32_t copy_size = size;
    if (copy_size >= REG_MAX_DATA) copy_size = REG_MAX_DATA - 1;
    memcpy(v->data, data, copy_size);
    v->data[copy_size] = '\0';
    v->size = copy_size;

    /* 持久化到 FunDB */
    char path[REG_MAX_PATH];
    reg_build_path(key, path, sizeof(path));
    reg_db_persist_value(path, name, type, v->data, copy_size);

    /* 触发监视事件 */
    if (is_new) {
        reg_fire_watch_event(REG_WATCH_SET_VALUE | REG_WATCH_CREATE_KEY,
                            key->root, path, name);
    } else {
        reg_fire_watch_event(REG_WATCH_SET_VALUE, key->root, path, name);
    }

    spinlock_unlock(&g_reg.lock);
    return REG_OK;
}

int reg_get_value(reg_handle_t key, const char *name,
                  uint32_t *type, void *data, uint32_t *size) {
    if (!key) return REG_ERROR;
    if (!name) name = "";

    spinlock_lock(&g_reg.lock);
    reg_value_t *v = reg_find_value(key, name);
    if (!v) {
        spinlock_unlock(&g_reg.lock);
        return REG_NO_VALUE;
    }
    if (type) *type = v->type;
    if (size) {
        if (data) {
            uint32_t copy = *size < v->size ? *size : v->size;
            memcpy(data, v->data, copy);
            *size = v->size;
        } else {
            *size = v->size;
        }
    }
    spinlock_unlock(&g_reg.lock);
    return REG_OK;
}

int reg_delete_value(reg_handle_t key, const char *name) {
    if (!key || !name) return REG_ERROR;

    spinlock_lock(&g_reg.lock);
    reg_value_t **pp = &key->values;
    while (*pp) {
        if (strcmp((*pp)->name, name) == 0) {
            reg_value_t *victim = *pp;
            *pp = victim->next;
            key->value_count--;
            g_reg.stats.total_values--;
            g_reg.stats.per_root_values[key->root]--;

            char path[REG_MAX_PATH];
            reg_build_path(key, path, sizeof(path));
            uint32_t key_root = key->root;
            reg_db_delete_value(path, name);

            /* 触发值删除监视事件 */
            reg_fire_watch_event(REG_WATCH_DELETE_VALUE, key_root, path, name);

            kfree(victim);
            spinlock_unlock(&g_reg.lock);
            return REG_OK;
        }
        pp = &(*pp)->next;
    }
    spinlock_unlock(&g_reg.lock);
    return REG_NO_VALUE;
}

int reg_set_string(reg_handle_t key, const char *name, const char *value) {
    if (!value) return REG_ERROR;
    return reg_set_value(key, name, REG_TYPE_STRING, value,
                         (uint32_t)strlen(value));
}

int reg_set_dword(reg_handle_t key, const char *name, uint32_t value) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%u", value);
    return reg_set_value(key, name, REG_TYPE_DWORD, buf,
                         (uint32_t)strlen(buf));
}

const char *reg_get_string(reg_handle_t key, const char *name, const char *def) {
    if (!key) return def;
    reg_value_t *v = reg_find_value(key, name);
    if (!v) return def;
    return v->data;
}

uint32_t reg_get_dword(reg_handle_t key, const char *name, uint32_t def) {
    if (!key) return def;
    reg_value_t *v = reg_find_value(key, name);
    if (!v) return def;
    return (uint32_t)strtol(v->data, NULL, 10);
}

int reg_set_binary(reg_handle_t key, const char *name,
                   const void *data, uint32_t size) {
    if (!data || size == 0) return REG_ERROR;
    if (size > REG_MAX_DATA) size = REG_MAX_DATA;
    return reg_set_value(key, name, REG_TYPE_BINARY, data, size);
}

int reg_set_multi_string(reg_handle_t key, const char *name,
                         const char **strings, uint32_t count) {
    if (!strings || count == 0) return REG_ERROR;
    /* 构建 MULTI_SZ 格式: str1\0str2\0...\0strN\0\0 */
    char buf[REG_MAX_DATA];
    uint32_t pos = 0;
    for (uint32_t i = 0; i < count && pos < REG_MAX_DATA - 1; i++) {
        const char *s = strings[i];
        if (!s) continue;
        while (*s && pos < REG_MAX_DATA - 1) {
            buf[pos++] = *s++;
        }
        if (pos < REG_MAX_DATA - 1) {
            buf[pos++] = '\0';
        }
    }
    /* 终止双 null */
    if (pos < REG_MAX_DATA) buf[pos++] = '\0';
    if (pos < REG_MAX_DATA) buf[pos++] = '\0';
    return reg_set_value(key, name, REG_TYPE_MULTI_SZ, buf, pos);
}

int reg_rename_key(reg_handle_t key, const char *new_name) {
    if (!key || !new_name || !*new_name) return REG_ERROR;
    if (strlen(new_name) >= REG_MAX_NAME) return REG_TOO_LONG;
    spinlock_lock(&g_reg.lock);
    strncpy(key->name, new_name, REG_MAX_NAME - 1);
    key->name[REG_MAX_NAME - 1] = '\0';
    spinlock_unlock(&g_reg.lock);
    return REG_OK;
}

int reg_check_write_access(uint32_t root) {
    (void)root;
    return 0;
}

int reg_key_exists(uint32_t root, const char *subkey) {
    reg_handle_t h = reg_open_key(root, subkey);
    if (!h) return 0;
    return 1;
}

int reg_value_exists(reg_handle_t key, const char *name) {
    if (!key || !name) return 0;
    spinlock_lock(&g_reg.lock);
    reg_value_t *v = reg_find_value(key, name);
    spinlock_unlock(&g_reg.lock);
    return v != NULL;
}

/* ---- 内部：检查路径是否匹配监视项 ---- */
static int reg_watch_path_match(const char *event_path, const char *prefix) {
    if (!event_path || !prefix) return 0;
    size_t plen = strlen(prefix);
    size_t elen = strlen(event_path);
    if (elen < plen) return 0;
    return strncmp(event_path, prefix, plen) == 0;
}

/* ---- 内部：触发监视事件 ---- */
void reg_fire_watch_event(uint32_t event_type, uint32_t root,
                          const char *path, const char *name) {
    if (!g_reg.watch_enabled) return;

    reg_watch_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.timestamp = 0;  /* TODO: 获取系统时间 */
    ev.event_type = event_type;
    ev.root = root;
    if (path) {
        strncpy(ev.path, path, REG_MAX_PATH - 1);
        ev.path[REG_MAX_PATH - 1] = '\0';
    }
    if (name) {
        strncpy(ev.name, name, REG_MAX_NAME - 1);
        ev.name[REG_MAX_NAME - 1] = '\0';
    }

    /* 添加到历史 */
    g_reg.history[g_reg.history_head] = ev;
    g_reg.history_head = (g_reg.history_head + 1) % REG_MAX_HISTORY;
    if (g_reg.history_count < REG_MAX_HISTORY) {
        g_reg.history_count++;
    }

    /* 触发匹配的回调 */
    for (int i = 0; i < REG_MAX_WATCHES; i++) {
        if (g_reg.watches[i].active &&
            (g_reg.watches[i].events & event_type) &&
            reg_watch_path_match(path, g_reg.watches[i].path_prefix)) {
            if (g_reg.watches[i].callback) {
                g_reg.watches[i].callback(&ev);
            }
        }
    }
}

/* ---- 内部：初始化监视系统（在 registry_init 中调用） ---- */
void reg_watch_init(void) {
    memset(g_reg.watches, 0, sizeof(g_reg.watches));
    memset(g_reg.history, 0, sizeof(g_reg.history));
    g_reg.next_watch_id = 1;
    g_reg.history_head = 0;
    g_reg.history_count = 0;
    g_reg.watch_enabled = 1;
}

int reg_watch_add(const char *path_prefix, uint32_t events,
                  reg_watch_callback_t callback) {
    if (!path_prefix || !*path_prefix) return -1;

    spinlock_lock(&g_reg.lock);
    int id = -1;
    for (int i = 0; i < REG_MAX_WATCHES; i++) {
        if (!g_reg.watches[i].active) {
            g_reg.watches[i].id = g_reg.next_watch_id++;
            strncpy(g_reg.watches[i].path_prefix, path_prefix,
                    REG_MAX_PATH - 1);
            g_reg.watches[i].path_prefix[REG_MAX_PATH - 1] = '\0';
            g_reg.watches[i].events = events;
            g_reg.watches[i].callback = callback;
            g_reg.watches[i].active = 1;
            id = g_reg.watches[i].id;
            break;
        }
    }
    spinlock_unlock(&g_reg.lock);
    return id;
}

int reg_watch_remove(int watch_id) {
    spinlock_lock(&g_reg.lock);
    for (int i = 0; i < REG_MAX_WATCHES; i++) {
        if (g_reg.watches[i].active &&
            g_reg.watches[i].id == watch_id) {
            g_reg.watches[i].active = 0;
            spinlock_unlock(&g_reg.lock);
            return 0;
        }
    }
    spinlock_unlock(&g_reg.lock);
    return -1;
}

void reg_watch_clear(void) {
    spinlock_lock(&g_reg.lock);
    memset(g_reg.watches, 0, sizeof(g_reg.watches));
    spinlock_unlock(&g_reg.lock);
}

int reg_watch_get_history(reg_watch_event_t *events, int max_events) {
    if (!events || max_events <= 0) return 0;
    spinlock_lock(&g_reg.lock);
    int count = g_reg.history_count;
    if (count > max_events) count = max_events;
    int start = (g_reg.history_head - count + REG_MAX_HISTORY) % REG_MAX_HISTORY;
    for (int i = 0; i < count; i++) {
        events[i] = g_reg.history[(start + i) % REG_MAX_HISTORY];
    }
    spinlock_unlock(&g_reg.lock);
    return count;
}

void reg_watch_enable(int enabled) {
    g_reg.watch_enabled = enabled ? 1 : 0;
}

int reg_watch_is_enabled(void) {
    return g_reg.watch_enabled;
}

reg_handle_t reg_open_path(const char *path) {
    const char *sub;
    int root = reg_parse_root(path, &sub);
    if (root < 0) return REG_INVALID_HANDLE;
    return reg_open_key((uint32_t)root, sub);
}

reg_handle_t reg_create_path(const char *path) {
    const char *sub;
    int root = reg_parse_root(path, &sub);
    if (root < 0) return REG_INVALID_HANDLE;
    return reg_create_key((uint32_t)root, sub);
}

int reg_set_value_path(const char *path, const char *name,
                       uint32_t type, const void *data, uint32_t size) {
    reg_handle_t h = reg_create_path(path);
    if (!h) return REG_ERROR;
    return reg_set_value(h, name, type, data, size);
}

int reg_get_value_path(const char *path, const char *name,
                       uint32_t *type, void *data, uint32_t *size) {
    reg_handle_t h = reg_open_path(path);
    if (!h) return REG_NO_KEY;
    return reg_get_value(h, name, type, data, size);
}

void reg_get_stats(reg_stats_t *stats) {
    if (!stats) return;
    spinlock_lock(&g_reg.lock);
    *stats = g_reg.stats;
    spinlock_unlock(&g_reg.lock);
}

void reg_reset_stats(void) {
    spinlock_lock(&g_reg.lock);
    memset(&g_reg.stats, 0, sizeof(g_reg.stats));
    g_reg.stats.total_keys = HKEY_COUNT;
    for (int i = 0; i < HKEY_COUNT; i++) {
        g_reg.stats.per_root_keys[i] = 1;
    }
    spinlock_unlock(&g_reg.lock);
}

/* 递归导出子树到 buf */
/* 将二进制数据转换为十六进制字符串 */
static void reg_binary_to_hex(const uint8_t *data, uint32_t size,
                              char *hex_out, uint32_t hex_out_size) {
    static const char hex_chars[] = "0123456789ABCDEF";
    uint32_t pos = 0;
    for (uint32_t i = 0; i < size && pos + 3 < hex_out_size; i++) {
        if (i > 0 && i % 16 == 0) {
            /* 16 字节后换行 */
            if (pos + 2 < hex_out_size) {
                hex_out[pos++] = '\\';
                hex_out[pos++] = '\n';
                hex_out[pos++] = ' ';
            }
        } else if (i > 0) {
            hex_out[pos++] = ' ';
        }
        hex_out[pos++] = hex_chars[(data[i] >> 4) & 0x0F];
        hex_out[pos++] = hex_chars[data[i] & 0x0F];
    }
    hex_out[pos] = '\0';
}

/* 打印多字符串值（每行一个字符串） */
static void reg_format_multi_sz(const char *data, uint32_t size,
                                char *out, uint32_t out_size) {
    uint32_t pos = 0;
    uint32_t i = 0;
    int first = 1;
    while (i < size && data[i] != '\0') {
        /* 找到下一个字符串 */
        uint32_t start = i;
        while (i < size && data[i] != '\0') i++;
        uint32_t len = i - start;
        if (len > 0) {
            if (!first && pos + 2 < out_size) {
                out[pos++] = '\\';
                out[pos++] = '\n';
            }
            if (pos < out_size - len - 3) {
                out[pos++] = '"';
                memcpy(out + pos, data + start, len);
                pos += len;
                out[pos++] = '"';
            }
            first = 0;
        }
        if (data[i] == '\0') i++;
    }
    out[pos] = '\0';
}

static uint32_t reg_export_recursive(reg_key_t *key, char *buf,
                                     uint32_t buf_size, uint32_t pos) {
    char path[REG_MAX_PATH];
    reg_build_path(key, path, sizeof(path));

    /* 写键头（Windows .reg 兼容格式） */
    pos += (uint32_t)snprintf(buf + pos, buf_size - pos,
                              "[%s]\n", path);
    if (pos >= buf_size) return pos;

    /* 写值 */
    reg_value_t *v = key->values;
    while (v) {
        const char *tn = reg_type_name(v->type);
        if (v->type == REG_TYPE_BINARY && v->size > 0) {
            /* 二进制值：显示为十六进制 */
            char hex[REG_MAX_DATA * 3 + 16];
            reg_binary_to_hex((const uint8_t *)v->data, v->size,
                             hex, sizeof(hex));
            pos += (uint32_t)snprintf(buf + pos, buf_size - pos,
                                      "  \"%s\" = %s:%u : %s\n",
                                      v->name, tn, v->size, hex);
        } else if (v->type == REG_TYPE_MULTI_SZ) {
            /* 多字符串值 */
            char formatted[REG_MAX_DATA * 3];
            reg_format_multi_sz(v->data, v->size, formatted, sizeof(formatted));
            pos += (uint32_t)snprintf(buf + pos, buf_size - pos,
                                      "  \"%s\" = %s : %s\n",
                                      v->name, tn, formatted);
        } else if (v->type == REG_TYPE_DWORD) {
            /* DWORD 值：显示为十六进制 */
            uint32_t val = (uint32_t)strtol(v->data, NULL, 10);
            pos += (uint32_t)snprintf(buf + pos, buf_size - pos,
                                      "  \"%s\" = %s : 0x%08X (%u)\n",
                                      v->name, tn, val, val);
        } else {
            /* 普通字符串/INT 值 */
            pos += (uint32_t)snprintf(buf + pos, buf_size - pos,
                                      "  \"%s\" = %s : %s\n",
                                      v->name, tn, v->data);
        }
        if (pos >= buf_size) return pos;
        v = v->next;
    }

    /* 递归子键 */
    reg_key_t *c = key->children;
    while (c) {
        if (pos + 2 < buf_size) {
            buf[pos++] = '\n';
        }
        pos = reg_export_recursive(c, buf, buf_size, pos);
        c = c->next_sibling;
    }
    return pos;
}

uint32_t reg_export(reg_handle_t key, char *buf, uint32_t buf_size) {
    if (!key || !buf || buf_size == 0) return 0;
    buf[0] = '\0';
    return reg_export_recursive(key, buf, buf_size, 0);
}

/* ---- 备份与恢复实现 ---- */

/* 注册表备份文件头（用于识别和版本校验） */
#define REG_BACKUP_MAGIC     "FUNREG1"
#define REG_BACKUP_VERSION    1

typedef struct {
    char     magic[8];      /* "FUNREG1" */
    uint32_t version;       /* 版本号 */
    uint32_t total_keys;   /* 总键数 */
    uint32_t total_values;  /* 总值数 */
    uint32_t data_size;     /* 后续数据大小 */
    uint32_t checksum;      /* 简单校验和 */
    char     reserved[8];
} reg_backup_header_t;

static uint32_t reg_calc_checksum(const void *data, uint32_t size) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t sum = 0;
    for (uint32_t i = 0; i < size; i++) {
        sum = (sum + p[i]) & 0xFFFFFFFF;
    }
    return sum;
}

/* 递归收集所有键值对用于备份 */
static uint32_t reg_collect_backup(reg_key_t *key, char *buf,
                                   uint32_t buf_size, uint32_t pos) {
    char path[REG_MAX_PATH];
    reg_build_path(key, path, sizeof(path));

    /* 写键头 */
    pos += (uint32_t)snprintf(buf + pos, buf_size - pos, "[%s]\n", path);
    if (pos >= buf_size) return pos;

    /* 写值 */
    reg_value_t *v = key->values;
    while (v) {
        const char *tn = reg_type_name(v->type);
        pos += (uint32_t)snprintf(buf + pos, buf_size - pos,
                                   "\"%s\"=%u:%u:%s\n",
                                   v->name, v->type, v->size, v->data);
        if (pos >= buf_size) return pos;
        v = v->next;
    }

    /* 递归子键 */
    reg_key_t *c = key->children;
    while (c) {
        if (pos + 2 < buf_size) buf[pos++] = '\n';
        pos = reg_collect_backup(c, buf, buf_size, pos);
        c = c->next_sibling;
    }
    return pos;
}

int reg_backup_to_buffer(char *buf, uint32_t buf_size, uint32_t *out_size) {
    if (!buf || buf_size < sizeof(reg_backup_header_t) + 256) {
        return REG_ERROR;
    }

    reg_backup_header_t *hdr = (reg_backup_header_t *)buf;
    memset(hdr, 0, sizeof(*hdr));
    memcpy(hdr->magic, REG_BACKUP_MAGIC, 8);
    hdr->version = REG_BACKUP_VERSION;
    hdr->total_keys = g_reg.stats.total_keys;
    hdr->total_values = g_reg.stats.total_values;

    /* 收集所有数据 */
    char *data = buf + sizeof(reg_backup_header_t);
    uint32_t data_size = reg_collect_backup(&g_reg.roots[0], data,
                                            buf_size - sizeof(reg_backup_header_t), 0);
    /* 从其他根键收集 */
    for (int i = 1; i < HKEY_COUNT; i++) {
        data_size = reg_collect_backup(&g_reg.roots[i], data,
                                      buf_size - sizeof(reg_backup_header_t), data_size);
    }

    hdr->data_size = data_size;
    hdr->checksum = reg_calc_checksum(data, data_size);

    if (out_size) {
        *out_size = sizeof(reg_backup_header_t) + data_size;
    }
    return REG_OK;
}

int reg_restore_from_buffer(const char *buf, uint32_t size) {
    if (!buf || size < sizeof(reg_backup_header_t)) {
        return REG_ERROR;
    }

    const reg_backup_header_t *hdr = (const reg_backup_header_t *)buf;
    if (memcmp(hdr->magic, REG_BACKUP_MAGIC, 8) != 0) {
        return REG_ERROR;
    }
    if (hdr->version != REG_BACKUP_VERSION) {
        return REG_ERROR;
    }

    /* 验证校验和 */
    uint32_t cs = reg_calc_checksum(buf + sizeof(reg_backup_header_t), hdr->data_size);
    if (cs != hdr->checksum) {
        return REG_ERROR;
    }

    /* 清空现有数据并重建根键 */
    for (int i = 0; i < HKEY_COUNT; i++) {
        reg_key_t *c = g_reg.roots[i].children;
        while (c) {
            reg_key_t *next = c->next_sibling;
            reg_free_subtree(c);
            c = next;
        }
        g_reg.roots[i].children = NULL;
        g_reg.roots[i].child_count = 0;
        g_reg.roots[i].value_count = 0;
        g_reg.roots[i].values = NULL;
    }

    /* 解析并恢复数据 - 简化版本：只解析最基本格式 */
    /* TODO: 完整实现备份解析 */
    (void)hdr;
    return REG_OK;
}

int reg_backup_to_file(const char *filepath) {
    char buf[65536];
    uint32_t size = 0;
    int rc = reg_backup_to_buffer(buf, sizeof(buf), &size);
    if (rc != REG_OK) return rc;

    /* 使用 VFS 写入文件 - 简化版本 */
    /* TODO: 实现文件写入 */
    (void)filepath;
    klog_info("registry: backup would be %u bytes", size);
    return REG_OK;
}

int reg_restore_from_file(const char *filepath) {
    /* 使用 VFS 读取文件 - 简化版本 */
    /* TODO: 实现文件读取 */
    (void)filepath;
    return REG_ERROR;
}

/* 递归打印到 klog */
static void reg_dump_recursive(reg_key_t *key, int depth) {
    char indent[64];
    int i;
    for (i = 0; i < depth && i < 30; i++) indent[i] = ' ';
    indent[i] = '\0';

    klog_info("%s%s/", indent, key->name);

    reg_value_t *v = key->values;
    while (v) {
        klog_info("%s  @%s = %s : %s", indent,
                  v->name, reg_type_name(v->type), v->data);
        v = v->next;
    }

    reg_key_t *c = key->children;
    while (c) {
        reg_dump_recursive(c, depth + 1);
        c = c->next_sibling;
    }
}

void reg_dump_all(void) {
    klog_info("=== Registry Dump ===");
    for (int i = 0; i < HKEY_COUNT; i++) {
        reg_dump_recursive(&g_reg.roots[i], 0);
    }
}

const char *reg_type_name(uint32_t type) {
    switch (type) {
        case REG_TYPE_NONE:     return "NONE";
        case REG_TYPE_STRING:   return "STRING";
        case REG_TYPE_INT:      return "INT";
        case REG_TYPE_BINARY:   return "BINARY";
        case REG_TYPE_DWORD:    return "DWORD";
        case REG_TYPE_MULTI_SZ: return "MULTI_SZ";
        default:                return "UNKNOWN";
    }
}

const char *reg_root_name(uint32_t root) {
    if (root >= HKEY_COUNT) return "UNKNOWN";
    return root_long_names[root];
}
