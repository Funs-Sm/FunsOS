#ifndef FUNSOS_PACKAGE_H
#define FUNSOS_PACKAGE_H

/*
 * FUNSOS 软件包管理 API
 * 提供软件包的安装、删除、更新、查询等功能。
 */

#include "stdint.h"

/* ---- 包状态 ---- */
#define FUNSOS_PKG_STATE_INSTALLED  1
#define FUNSOS_PKG_STATE_AVAILABLE  2
#define FUNSOS_PKG_STATE_UPDATING   3

/* ---- 错误码（与 funsos_error_t 保持一致） ---- */
#define FUNSOS_PKG_OK               0
#define FUNSOS_PKG_ERR_INVAL       -1
#define FUNSOS_PKG_ERR_NOMEM       -2
#define FUNSOS_PKG_ERR_NOENT       -3   /* 包不存在 */
#define FUNSOS_PKG_ERR_EXIST       -4   /* 包已安装 */
#define FUNSOS_PKG_ERR_IO          -5
#define FUNSOS_PKG_ERR_NETWORK     -6
#define FUNSOS_PKG_ERR_BADPKG      -7   /* 包格式错误 */
#define FUNSOS_PKG_ERR_DEPENDENCY  -8
#define FUNSOS_PKG_ERR_FULL        -9   /* 包表已满 */
#define FUNSOS_PKG_ERR_PERM        -10

/* ---- 包信息结构体 ---- */
typedef struct {
    char     name[64];          /* 包名 */
    char     version[32];       /* 版本号 */
    char     description[128];  /* 描述 */
    uint32_t state;             /* 状态 */
    uint32_t size;              /* 大小（字节） */
    char     depends[256];      /* 依赖列表（逗号分隔） */
    char     install_path[128]; /* 安装路径 */
    char     author[64];        /* 作者 */
    char     license[32];       /* 许可证 */
    uint32_t install_time;      /* 安装时间（ticks） */
} funsos_pkg_info_t;

/*
 * 安装软件包
 * 参数: name - 包名或 URL
 * 返回: FUNSOS_PKG_OK 成功, 其他值为错误码
 */
int funsos_pkg_install(const char *name);

/*
 * 删除软件包
 * 参数: name - 包名
 * 返回: FUNSOS_PKG_OK 成功, 其他值为错误码
 */
int funsos_pkg_remove(const char *name);

/*
 * 更新软件包
 * 参数: name - 包名
 * 返回: FUNSOS_PKG_OK 成功, 其他值为错误码
 */
int funsos_pkg_update(const char *name);

/*
 * 更新所有已安装的软件包
 * 参数: updated - 接收成功更新的数量（可为 NULL）
 *      failed  - 接收失败的数量（可为 NULL）
 * 返回: FUNSOS_PKG_OK 全部成功, 否则为错误码
 */
int funsos_pkg_update_all(uint32_t *updated, uint32_t *failed);

/*
 * 列出已安装的软件包
 * 参数: callback - 每个包的回调函数, 返回 0 继续, 返回非 0 停止
 * 返回: 列出的包数量, 负数表示错误
 */
int funsos_pkg_list(int (*callback)(const funsos_pkg_info_t *pkg, void *userdata), void *userdata);

/*
 * 搜索软件包
 * 参数: keyword  - 搜索关键词
 *      callback - 每个匹配包的回调函数
 * 返回: 找到的包数量, 负数表示错误
 */
int funsos_pkg_search(const char *keyword,
                     int (*callback)(const funsos_pkg_info_t *pkg, void *userdata),
                     void *userdata);

/*
 * 获取指定包的详细信息
 * 参数: name - 包名; info - 接收信息的结构体指针
 * 返回: FUNSOS_PKG_OK 成功, 其他值为错误码
 */
int funsos_pkg_get_info(const char *name, funsos_pkg_info_t *info);

/*
 * 下载文件到包缓存
 * 参数: url            - 下载 URL
 *      save_path      - 接收保存路径的缓冲区（可为 NULL）
 *      save_path_size - 缓冲区大小
 * 返回: FUNSOS_PKG_OK 成功, 其他值为错误码
 */
int funsos_pkg_download(const char *url, char *save_path, uint32_t save_path_size);

/*
 * 获取错误码的描述字符串
 * 参数: err - 错误码
 * 返回: 错误描述字符串
 */
const char *funsos_pkg_strerror(int err);

/*
 * 获取已安装包的总数
 * 返回: 包数量
 */
uint32_t funsos_pkg_count(void);

#endif /* FUNSOS_PACKAGE_H */
