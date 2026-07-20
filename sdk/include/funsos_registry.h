/* funsos_registry.h - 系统注册表 API
 * 类似 Windows 注册表的层次化配置存储，基于 FunDB 持久化
 *
 * 五个根键：
 *   HKLM  HKEY_LOCAL_MACHINE   系统级配置（硬件、内核、软件）
 *   HKCU  HKEY_CURRENT_USER    当前用户配置（桌面、偏好）
 *   HKCR  HKEY_CLASSES_ROOT    文件关联与类型
 *   HKU   HKEY_USERS           所有用户配置
 *   HKCC  HKEY_CURRENT_CONFIG  当前硬件配置
 */

#ifndef FUNSOS_REGISTRY_H
#define FUNSOS_REGISTRY_H

#include "stdint.h"
#include "stddef.h"

/* ---- 根键常量 ---- */
#define FUNSOS_HKEY_LOCAL_MACHINE   0
#define FUNSOS_HKEY_CURRENT_USER    1
#define FUNSOS_HKEY_CLASSES_ROOT    2
#define FUNSOS_HKEY_USERS           3
#define FUNSOS_HKEY_CURRENT_CONFIG  4
#define FUNSOS_HKEY_COUNT           5

/* 根键路径前缀（用于完整路径，如 "HKLM\System\Kernel"） */
#define FUNSOS_HKLM  "HKLM"
#define FUNSOS_HKCU  "HKCU"
#define FUNSOS_HKCR  "HKCR"
#define FUNSOS_HKU   "HKU"
#define FUNSOS_HKCC  "HKCC"

/* ---- 值类型 ---- */
#define FUNSOS_REG_TYPE_NONE      0
#define FUNSOS_REG_TYPE_STRING    1   /* 以 null 结尾的字符串 */
#define FUNSOS_REG_TYPE_INT       2   /* 32 位整数 */
#define FUNSOS_REG_TYPE_BINARY    3   /* 二进制数据 */
#define FUNSOS_REG_TYPE_DWORD     4   /* 同 INT（Windows 习惯） */
#define FUNSOS_REG_TYPE_MULTI_SZ  5   /* 多字符串（以 \0 分隔） */

/* ---- 限制 ---- */
#define FUNSOS_REG_MAX_NAME        64
#define FUNSOS_REG_MAX_DATA        256
#define FUNSOS_REG_MAX_PATH        320

/* ---- 错误码 ---- */
#define FUNSOS_REG_OK              0
#define FUNSOS_REG_ERROR          -1
#define FUNSOS_REG_NO_KEY         -2
#define FUNSOS_REG_NO_VALUE       -3
#define FUNSOS_REG_EXISTS         -4
#define FUNSOS_REG_NO_MEMORY      -5
#define FUNSOS_REG_INVALID_PATH   -6
#define FUNSOS_REG_TOO_LONG       -7

/* ---- 句柄类型（不透明指针） ---- */
typedef void *funsos_reg_hkey_t;
#define FUNSOS_REG_INVALID_KEY  ((funsos_reg_hkey_t)0)

/* ---- 注册表统计 ---- */
typedef struct {
    uint32_t total_keys;
    uint32_t total_values;
    uint32_t total_size_bytes;
    uint32_t per_root_keys[FUNSOS_HKEY_COUNT];
    uint32_t per_root_values[FUNSOS_HKEY_COUNT];
} funsos_reg_stats_t;

/* ---- 值枚举结构 ---- */
typedef struct {
    char     name[FUNSOS_REG_MAX_NAME];
    uint32_t type;
    char     data[FUNSOS_REG_MAX_DATA];
    uint32_t size;
} funsos_reg_value_t;

/* ---- 子键枚举结构 ---- */
typedef struct {
    char name[FUNSOS_REG_MAX_NAME];
} funsos_reg_subkey_t;

/* ---- 应用注册信息 ---- */
typedef struct {
    char     id[32];          /* 应用标识符 */
    char     name[64];        /* 显示名称 */
    char     path[128];       /* 可执行文件路径 */
    char     version[16];     /* 版本号 */
    char     description[128];/* 描述 */
    char     category[32];    /* 分类：Utility/Application/Game/System */
} funsos_app_info_t;

/* ============================================================
 * 注册表键操作
 * ============================================================ */

/*
 * 通过路径打开键
 * 参数: path - 完整路径，如 "HKLM\System\Kernel"
 * 返回: 键句柄，FUNSOS_REG_INVALID_KEY 表示失败
 */
funsos_reg_hkey_t funsos_reg_open(const char *path);

/*
 * 通过路径创建键（如果不存在则创建，否则返回已有键）
 * 参数: path - 完整路径
 * 返回: 键句柄，FUNSOS_REG_INVALID_KEY 表示失败
 */
funsos_reg_hkey_t funsos_reg_create(const char *path);

/*
 * 关闭键句柄（释放引用）
 * 参数: key - 键句柄
 */
void funsos_reg_close(funsos_reg_hkey_t key);

/*
 * 删除键（连同所有子键和值）
 * 参数: path - 完整路径
 * 返回: FUNSOS_REG_OK 成功, 其他见错误码
 */
int funsos_reg_delete_key(const char *path);

/*
 * 枚举子键
 * 参数: key - 父键句柄; index - 索引（从 0 开始）;
 *      subkey - 接收子键信息
 * 返回: FUNSOS_REG_OK 成功, FUNSOS_REG_NO_KEY 越界
 */
int funsos_reg_enum_subkey(funsos_reg_hkey_t key, uint32_t index,
                           funsos_reg_subkey_t *subkey);

/*
 * 枚举键下的值
 * 参数: key - 父键句柄; index - 索引; value - 接收值信息
 * 返回: FUNSOS_REG_OK 成功, FUNSOS_REG_NO_KEY 越界
 */
int funsos_reg_enum_value(funsos_reg_hkey_t key, uint32_t index,
                          funsos_reg_value_t *value);

/* ============================================================
 * 值操作
 * ============================================================ */

/*
 * 设置值（不存在则创建）
 * 参数: key - 键句柄; name - 值名（NULL 表示默认值）;
 *      type - 类型; data - 数据; size - 数据字节长度
 * 返回: FUNSOS_REG_OK 成功
 */
int funsos_reg_set_value(funsos_reg_hkey_t key, const char *name,
                         uint32_t type, const void *data, uint32_t size);

/*
 * 获取值
 * 参数: key - 键句柄; name - 值名;
 *      type - 输出类型; data - 输出缓冲区; size - 输入缓冲区大小/输出实际大小
 * 返回: FUNSOS_REG_OK 成功, FUNSOS_REG_NO_VALUE 不存在
 */
int funsos_reg_get_value(funsos_reg_hkey_t key, const char *name,
                         uint32_t *type, void *data, uint32_t *size);

/*
 * 删除值
 * 参数: key - 键句柄; name - 值名
 * 返回: FUNSOS_REG_OK 成功
 */
int funsos_reg_delete_value(funsos_reg_hkey_t key, const char *name);

/* ============================================================
 * 便捷函数
 * ============================================================ */

/*
 * 设置字符串值
 * 参数: key - 键句柄; name - 值名; value - 字符串
 * 返回: FUNSOS_REG_OK 成功
 */
int funsos_reg_set_string(funsos_reg_hkey_t key, const char *name, const char *value);

/*
 * 设置 DWORD 值
 * 参数: key - 键句柄; name - 值名; value - 数值
 * 返回: FUNSOS_REG_OK 成功
 */
int funsos_reg_set_dword(funsos_reg_hkey_t key, const char *name, uint32_t value);

/*
 * 获取字符串值（通过路径）
 * 参数: path - 完整键路径; name - 值名; def - 默认值
 * 返回: 字符串指针（指向内部缓冲区，无需释放）
 */
const char *funsos_reg_get_string(const char *path, const char *name, const char *def);

/*
 * 获取 DWORD 值（通过路径）
 * 参数: path - 完整键路径; name - 值名; def - 默认值
 * 返回: 数值
 */
uint32_t funsos_reg_get_dword(const char *path, const char *name, uint32_t def);

/* ============================================================
 * 统计与导出
 * ============================================================ */

/*
 * 获取注册表统计信息
 * 参数: stats - 接收统计的结构体指针
 */
void funsos_reg_get_stats(funsos_reg_stats_t *stats);

/*
 * 导出键的子树到文本缓冲区
 * 参数: path - 键路径; buf - 缓冲区; buf_size - 缓冲区大小
 * 返回: 实际写入字节数
 */
uint32_t funsos_reg_export(const char *path, char *buf, uint32_t buf_size);

/*
 * 获取类型名称字符串
 * 参数: type - 类型常量
 * 返回: 类型名（如 "STRING", "DWORD"）
 */
const char *funsos_reg_type_name(uint32_t type);

/*
 * 获取根键名称
 * 参数: root - 根键常量
 * 返回: 根键名（如 "HKLM"）
 */
const char *funsos_reg_root_name(uint32_t root);

/* ============================================================
 * 内置应用查询
 * ============================================================ */

/*
 * 获取内置应用数量
 * 返回: 应用总数
 */
uint32_t funsos_apps_count(void);

/*
 * 枚举内置应用
 * 参数: index - 索引（从 0 开始）; info - 接收应用信息
 * 返回: FUNSOS_REG_OK 成功, FUNSOS_REG_NO_KEY 越界
 */
int funsos_apps_enum(uint32_t index, funsos_app_info_t *info);

/*
 * 按应用 ID 查询应用信息
 * 参数: id - 应用标识符（如 "calc", "notepad"）; info - 接收应用信息
 * 返回: FUNSOS_REG_OK 成功, FUNSOS_REG_NO_VALUE 不存在
 */
int funsos_apps_lookup(const char *id, funsos_app_info_t *info);

/*
 * 启动内置应用
 * 参数: id - 应用标识符
 * 返回: FUNSOS_REG_OK 成功, FUNSOS_REG_NO_VALUE 不存在
 */
int funsos_apps_run(const char *id);

#endif /* FUNSOS_REGISTRY_H */
