#ifndef REGISTRY_H
#define REGISTRY_H

#include "stdint.h"
#include "stddef.h"

/* ============================================================
 * System Registry - 系统注册表
 *
 * 类似 Windows 注册表的层次化配置存储，基于 FunDB 持久化。
 *
 * 五个根键（类似 Windows）：
 *   HKLM  HKEY_LOCAL_MACHINE   系统级配置（硬件、内核、软件）
 *   HKCU  HKEY_CURRENT_USER    当前用户配置（桌面、偏好）
 *   HKCR  HKEY_CLASSES_ROOT    文件关联与类型
 *   HKU   HKEY_USERS           所有用户配置
 *   HKCC  HKEY_CURRENT_CONFIG  当前硬件配置
 *
 * 每个键可以有：
 *   - 子键（形成树结构）
 *   - 值（name=type=data 三元组，可有多个）
 *
 * 持久化：所有写入操作同步到 FunDB 数据库 /var/db/registry.db
 * 读取：启动时一次性加载到内存树，后续读操作走内存
 * ============================================================ */

/* 根键枚举 */
#define HKEY_LOCAL_MACHINE   0
#define HKEY_CURRENT_USER    1
#define HKEY_CLASSES_ROOT    2
#define HKEY_USERS           3
#define HKEY_CURRENT_CONFIG  4
#define HKEY_COUNT           5

/* 根键简称（用于路径，如 "HKLM\System\Kernel"） */
#define HKLM_SHORT  "HKLM"
#define HKCU_SHORT  "HKCU"
#define HKCR_SHORT  "HKCR"
#define HKU_SHORT   "HKU"
#define HKCC_SHORT  "HKCC"

/* 值类型 */
#define REG_TYPE_NONE      0
#define REG_TYPE_STRING    1   /* 以 null 结尾的字符串 */
#define REG_TYPE_INT       2   /* 32 位整数 */
#define REG_TYPE_BINARY    3   /* 二进制数据 */
#define REG_TYPE_DWORD     4   /* 同 REG_TYPE_INT（Windows 习惯） */
#define REG_TYPE_MULTI_SZ  5   /* 多字符串（以 \0 分隔，末尾 \0\0） */

/* 限制 */
#define REG_MAX_NAME        64    /* 键/值名称最大长度 */
#define REG_MAX_DATA        256   /* 数据最大长度 */
#define REG_MAX_PATH        320   /* 完整路径最大长度 */
#define REG_MAX_CHILDREN    64    /* 每个键最大子键数（软限制） */
#define REG_MAX_VALUES      32    /* 每个键最大值数（软限制） */

/* 错误码 */
#define REG_OK              0
#define REG_ERROR          -1
#define REG_NO_KEY         -2
#define REG_NO_VALUE       -3
#define REG_EXISTS         -4
#define REG_NO_MEMORY      -5
#define REG_INVALID_PATH   -6
#define REG_TOO_LONG       -7

/* ---- 键句柄 ---- */
typedef struct reg_value {
    char     name[REG_MAX_NAME];
    uint32_t type;
    char     data[REG_MAX_DATA];
    uint32_t size;            /* 数据字节长度 */
    struct reg_value *next;
} reg_value_t;

typedef struct reg_key {
    char     name[REG_MAX_NAME];
    uint32_t root;            /* 所属根键 */
    struct reg_key *parent;
    struct reg_key *children;   /* 子键链表 */
    struct reg_key *next_sibling;
    reg_value_t *values;       /* 值链表 */
    uint32_t child_count;
    uint32_t value_count;
} reg_key_t;

/* 句柄就是键指针 */
typedef reg_key_t *reg_handle_t;
#define REG_INVALID_HANDLE  ((reg_handle_t)0)

/* ---- 统计 ---- */
typedef struct {
    uint32_t total_keys;
    uint32_t total_values;
    uint32_t total_size_bytes;
    uint32_t per_root_keys[HKEY_COUNT];
    uint32_t per_root_values[HKEY_COUNT];
} reg_stats_t;

/* ---- 初始化 ---- */
void registry_init(void);        /* 启动时初始化，加载 FunDB 数据 */
void registry_shutdown(void);    /* 关闭前刷盘 */

/* ---- 路径解析 ---- */
/* 把 "HKLM\System\Kernel" 解析为根键 + 子路径
 * 返回 HKEY_* 或 REG_INVALID_PATH */
int reg_parse_root(const char *path, const char **subpath_out);

/* ---- 键操作 ---- */
/* 打开键（查找） */
reg_handle_t reg_open_key(uint32_t root, const char *subkey);

/* 创建键（如果不存在则创建，否则返回已有键） */
reg_handle_t reg_create_key(uint32_t root, const char *subkey);

/* 删除键（连同所有子键和值） */
int reg_delete_key(uint32_t root, const char *subkey);

/* 枚举子键 */
int reg_enum_key(reg_handle_t key, uint32_t index,
                 char *name, uint32_t name_size);

/* 枚举值 */
int reg_enum_value(reg_handle_t key, uint32_t index,
                   char *name, uint32_t name_size,
                   uint32_t *type, char *data, uint32_t data_size);

/* ---- 值操作 ---- */
/* 设置值（不存在则创建） */
int reg_set_value(reg_handle_t key, const char *name,
                  uint32_t type, const void *data, uint32_t size);

/* 获取值 */
int reg_get_value(reg_handle_t key, const char *name,
                  uint32_t *type, void *data, uint32_t *size);

/* 删除值 */
int reg_delete_value(reg_handle_t key, const char *name);

/* ---- 便捷函数 ---- */
int reg_set_string(reg_handle_t key, const char *name, const char *value);
int reg_set_dword(reg_handle_t key, const char *name, uint32_t value);
int reg_set_binary(reg_handle_t key, const char *name, const void *data, uint32_t size);
int reg_set_multi_string(reg_handle_t key, const char *name, const char **strings, uint32_t count);
const char *reg_get_string(reg_handle_t key, const char *name, const char *def);
uint32_t reg_get_dword(reg_handle_t key, const char *name, uint32_t def);

/* ---- 键路径构建 ---- */
void reg_build_path(reg_key_t *key, char *buf, uint32_t buf_size);

/* ---- 监视初始化 ---- */
void reg_watch_init(void);
void reg_fire_watch_event(uint32_t event_type, uint32_t root,
                          const char *path, const char *name);

/* ---- 键改名 ---- */
int reg_rename_key(reg_handle_t key, const char *new_name);

/* ---- 权限检查（HKLM 写保护） ---- */
/* 检查是否可以写入指定根键的路径，返回 0=可写，负数=只读 */
int reg_check_write_access(uint32_t root);

/* ---- 键存在性检查 ---- */
/* 检查键是否存在（不打开句柄） */
int reg_key_exists(uint32_t root, const char *subkey);
int reg_value_exists(reg_handle_t key, const char *name);

/* ---- 监视/追踪 ---- */
/* 监视事件类型 */
#define REG_WATCH_CREATE_KEY   0x01
#define REG_WATCH_DELETE_KEY  0x02
#define REG_WATCH_SET_VALUE   0x04
#define REG_WATCH_DELETE_VALUE 0x08
#define REG_WATCH_ALL         0xFF

typedef struct reg_watch_event {
    uint32_t timestamp;
    uint32_t event_type;       /* REG_WATCH_* */
    char     path[REG_MAX_PATH];
    char     name[REG_MAX_NAME];
    uint32_t root;
} reg_watch_event_t;

/* 监视回调函数类型 */
typedef void (*reg_watch_callback_t)(const reg_watch_event_t *event);

/* 添加监视项（返回监视 ID，-1 表示失败） */
int reg_watch_add(const char *path_prefix, uint32_t events,
                  reg_watch_callback_t callback);

/* 移除监视项 */
int reg_watch_remove(int watch_id);

/* 清除所有监视项 */
void reg_watch_clear(void);

/* 获取监视事件历史（返回事件数） */
int reg_watch_get_history(reg_watch_event_t *events, int max_events);

/* 启用/禁用监视（默认启用） */
void reg_watch_enable(int enabled);
int reg_watch_is_enabled(void);

/* 通过完整路径操作（自动解析根键） */
reg_handle_t reg_open_path(const char *path);
reg_handle_t reg_create_path(const char *path);
int reg_set_value_path(const char *path, const char *name,
                       uint32_t type, const void *data, uint32_t size);
int reg_get_value_path(const char *path, const char *name,
                       uint32_t *type, void *data, uint32_t *size);

/* ---- 查询与导出 ---- */
void reg_get_stats(reg_stats_t *stats);
void reg_reset_stats(void);

/* 把以 key 为根的子树打印到 buf（文本格式），返回写入字节数 */
uint32_t reg_export(reg_handle_t key, char *buf, uint32_t buf_size);

/* 把整个注册表导出到 klog */
void reg_dump_all(void);

/* ---- 备份与恢复 ---- */
/* 将整个注册表导出到缓冲区（用于备份文件） */
int reg_backup_to_buffer(char *buf, uint32_t buf_size, uint32_t *out_size);

/* 从备份缓冲区恢复注册表（会清空现有数据） */
int reg_restore_from_buffer(const char *buf, uint32_t size);

/* 将备份写入文件 */
int reg_backup_to_file(const char *filepath);

/* 从文件恢复备份 */
int reg_restore_from_file(const char *filepath);

/* 类型名称 */
const char *reg_type_name(uint32_t type);
const char *reg_root_name(uint32_t root);

#endif /* REGISTRY_H */
