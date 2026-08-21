#ifndef FUNSOS_PLUGIN_H
#define FUNSOS_PLUGIN_H

/*
 * FUNSOS 插件加载 API
 * 提供插件的加载、卸载、符号查找等功能。
 * 基于 kernel/kmod.c 的内核模块系统封装。
 */

#include "stdint.h"

/* 插件句柄类型 */
typedef void *funsos_plugin_t;

/* 插件标志 */
#define FUNSOS_PLUGIN_LAZY       0x0001   /* 延迟绑定 */
#define FUNSOS_PLUGIN_NOW        0x0002   /* 立即绑定 */
#define FUNSOS_PLUGIN_LOCAL      0x0004   /* 符号不导出 */
#define FUNSOS_PLUGIN_GLOBAL     0x0008   /* 符号全局可见 */
#define FUNSOS_PLUGIN_NOLOAD     0x0010   /* 不加载，仅查询 */
#define FUNSOS_PLUGIN_NODELETE   0x0020   /* 不卸载 */

/* 插件信息结构 */
typedef struct {
    char     name[64];         /* 插件名称 */
    char     version[32];      /* 插件版本 */
    char     description[256]; /* 插件描述 */
    char     author[128];      /* 作者 */
    uint32_t flags;            /* 标志 */
    uint32_t ref_count;        /* 引用计数 */
    void    *handle;           /* 内部句柄 */
} funsos_plugin_info_t;

/* 插件初始化/清理函数类型 */
typedef int (*funsos_plugin_init_t)(void);
typedef void (*funsos_plugin_fini_t)(void);

/* 插件描述结构（插件内导出） */
typedef struct {
    const char *name;              /* 插件名称 */
    const char *version;           /* 版本号 */
    const char *description;       /* 描述 */
    const char *author;            /* 作者 */
    funsos_plugin_init_t init;     /* 初始化函数 */
    funsos_plugin_fini_t fini;     /* 清理函数 */
} funsos_plugin_desc_t;

/* ---- 加载/卸载 ---- */

/*
 * 加载插件
 * 参数: filename - 插件文件路径; flags - 加载标志
 * 返回: 插件句柄, NULL 失败
 */
funsos_plugin_t funsos_plugin_load(const char *filename, int flags);

/*
 * 卸载插件
 * 参数: handle - 插件句柄
 * 返回: 0 成功, -1 失败
 */
int funsos_plugin_unload(funsos_plugin_t handle);

/* ---- 符号查找 ---- */

/*
 * 查找插件中的符号
 * 参数: handle - 插件句柄; symbol - 符号名称
 * 返回: 符号地址, NULL 未找到
 */
void *funsos_plugin_sym(funsos_plugin_t handle, const char *symbol);

/*
 * 获取插件错误信息
 * 返回: 错误描述字符串, NULL 无错误
 */
const char *funsos_plugin_error(void);

/* ---- 插件信息 ---- */

/*
 * 获取插件信息
 * 参数: handle - 插件句柄; info - 接收信息的结构
 * 返回: 0 成功, -1 失败
 */
int funsos_plugin_get_info(funsos_plugin_t handle, funsos_plugin_info_t *info);

/*
 * 获取已加载插件列表
 * 参数: plugins - 接收插件信息的数组; maxcount - 数组大小; count - 接收实际数量
 * 返回: 0 成功, -1 失败
 */
int funsos_plugin_list(funsos_plugin_info_t *plugins, uint32_t maxcount, uint32_t *count);

/* ---- 插件描述符访问 ---- */

/*
 * 获取插件描述符
 * 参数: handle - 插件句柄
 * 返回: 插件描述符指针, NULL 失败
 */
const funsos_plugin_desc_t *funsos_plugin_get_desc(funsos_plugin_t handle);

/*
 * 调用插件初始化函数
 * 参数: handle - 插件句柄
 * 返回: 0 成功, -1 失败
 */
int funsos_plugin_init(funsos_plugin_t handle);

/*
 * 调用插件清理函数
 * 参数: handle - 插件句柄
 * 返回: 0 成功, -1 失败
 */
int funsos_plugin_fini(funsos_plugin_t handle);

/* ---- 插件路径 ---- */

/*
 * 添加插件搜索路径
 * 参数: path - 搜索路径
 * 返回: 0 成功, -1 失败
 */
int funsos_plugin_add_path(const char *path);

/*
 * 设置插件搜索路径
 * 参数: paths - 路径列表 (NULL 结尾的字符串数组)
 * 返回: 0 成功, -1 失败
 */
int funsos_plugin_set_search_path(const char **paths);

/*
 * 获取插件搜索路径
 * 参数: paths - 接收路径列表的缓冲区; maxpaths - 最大路径数; count - 接收实际数量
 * 返回: 0 成功, -1 失败
 */
int funsos_plugin_get_search_path(char **paths, uint32_t maxpaths, uint32_t *count);

#endif /* FUNSOS_PLUGIN_H */
