#ifndef FUNSOS_SECURITY_H
#define FUNSOS_SECURITY_H

#include "stdint.h"
#include "stddef.h"

/*
 * FUNSOS SDK - 安全与权限管理子模块
 *
 * 提供文件权限、ACL、用户/组管理、能力(capability)等安全相关 API
 *
 * 版本: 1.0.0 (FunsCore v0.8 新增)
 */

#define FUNSOS_SECURITY_API_VERSION  0x0100

/* ---- 权限位定义（与内核 permission.h 保持一致） ---- */
#define FUNSOS_PERM_READ   0x04
#define FUNSOS_PERM_WRITE  0x02
#define FUNSOS_PERM_EXEC   0x01

/* ---- 特殊 UID/GID ---- */
#define FUNSOS_UID_SOVER    0
#define FUNSOS_UID_ADMIN    1
#define FUNSOS_UID_NOBODY   65534
#define FUNSOS_UID_USER_MIN 1000

#define FUNSOS_GID_ROOT     0
#define FUNSOS_GID_ADMIN    1
#define FUNSOS_GID_NOGROUP  65534

/* ---- 权限级别 ---- */
#define FUNSOS_LEVEL_SOVER   0
#define FUNSOS_LEVEL_ADMIN   1
#define FUNSOS_LEVEL_USER    2
#define FUNSOS_LEVEL_NOBODY  3

/* ---- ACL 条目 ---- */
#define FUNSOS_ACL_MAX_ENTRIES 16

typedef struct {
    uint32_t uid;
    uint32_t gid;
    uint8_t  perms;
} funsos_acl_entry_t;

typedef struct {
    uint32_t owner_uid;
    uint32_t group_gid;
    uint8_t  owner_perm;
    uint8_t  group_perm;
    uint8_t  other_perm;
    const funsos_acl_entry_t *entries;
    uint32_t entry_count;
} funsos_acl_t;

/* ---- 能力位（cap_effective） ---- */
#define FUNSOS_CAP_CHOWN          0
#define FUNSOS_CAP_DAC_OVERRIDE   1
#define FUNSOS_CAP_DAC_READ_SEARCH 2
#define FUNSOS_CAP_FOWNER         3
#define FUNSOS_CAP_FSETID         4
#define FUNSOS_CAP_KILL           5
#define FUNSOS_CAP_SETGID         6
#define FUNSOS_CAP_SETUID         7
#define FUNSOS_CAP_NET_BIND_SERVICE 10
#define FUNSOS_CAP_NET_RAW        13
#define FUNSOS_CAP_SYS_ADMIN      21
#define FUNSOS_CAP_SYS_BOOT       22
#define FUNSOS_CAP_SYS_MODULE     16
#define FUNSOS_CAP_SYS_TIME       25
#define FUNSOS_CAP_MAX            32

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 用户/组信息查询 ---- */

/*
 * 获取当前用户 ID
 */
uint32_t funsos_security_get_uid(void);

/*
 * 获取当前组 ID
 */
uint32_t funsos_security_get_gid(void);

/*
 * 获取当前用户权限级别 (0=Sover, 1=Admin, 2=User, 3=Nobody)
 */
int funsos_security_get_level(void);

/*
 * 获取权限级别名称
 */
const char *funsos_security_level_name(int level);

/*
 * 检查当前用户是否为 Sover (root)
 */
int funsos_security_is_sover(void);

/*
 * 检查当前用户是否为 Admin
 */
int funsos_security_is_admin(void);

/* ---- 文件权限 ---- */

/*
 * 检查对指定路径的访问权限
 * 参数: path - 文件路径; required - FUNSOS_PERM_* 位的或
 * 返回: 0 允许, -1 拒绝
 */
int funsos_security_check_path(const char *path, uint32_t required);

/*
 * 修改文件权限
 * 参数: path - 路径; mode - 八进制模式 (如 0755)
 * 返回: 0 成功, -1 失败 (权限不足或路径不存在)
 */
int funsos_security_chmod(const char *path, uint32_t mode);

/*
 * 修改文件所有者
 * 参数: path - 路径; uid - 新 UID; gid - 新 GID (0xFFFFFFFF 表示不修改)
 * 返回: 0 成功, -1 失败
 */
int funsos_security_chown(const char *path, uint32_t uid, uint32_t gid);

/*
 * 修改文件所属组
 * 参数: path - 路径; gid - 新 GID
 * 返回: 0 成功, -1 失败
 */
int funsos_security_chgrp(const char *path, uint32_t gid);

/* ---- ACL 管理 ---- */

/*
 * 获取文件 ACL
 * 参数: path - 路径; acl - 接收 ACL 信息的结构体
 * 返回: 0 成功, -1 失败
 */
int funsos_security_acl_get(const char *path, funsos_acl_t *acl);

/*
 * 添加 ACL 条目
 * 参数: path - 路径; entry - ACL 条目
 * 返回: 0 成功, -1 失败
 */
int funsos_security_acl_add(const char *path, const funsos_acl_entry_t *entry);

/*
 * 删除 ACL 条目
 * 参数: path - 路径; index - 条目索引
 * 返回: 0 成功, -1 失败
 */
int funsos_security_acl_remove(const char *path, uint32_t index);

/*
 * 清除所有扩展 ACL 条目
 * 参数: path - 路径
 * 返回: 0 成功, -1 失败
 */
int funsos_security_acl_clear(const char *path);

/* ---- 能力 (capability) ---- */

/*
 * 检查当前进程是否拥有指定能力
 * 参数: cap - 能力编号 (FUNSOS_CAP_*)
 * 返回: 1 拥有, 0 不拥有
 */
int funsos_security_has_capability(uint32_t cap);

/*
 * 获取当前进程的有效能力位图
 */
uint32_t funsos_security_get_capabilities(void);

/* ---- umask ---- */

/*
 * 获取当前 umask
 */
uint32_t funsos_security_umask_get(void);

/*
 * 设置当前 umask
 * 参数: mask - 新 umask (八进制)
 * 返回: 之前的 umask
 */
uint32_t funsos_security_umask_set(uint32_t mask);

/* ---- 用户管理 ---- */

/*
 * 创建新用户
 * 参数: name - 用户名; admin - 是否为管理员
 * 返回: 0 成功, -1 失败
 */
int funsos_security_user_create(const char *name, int admin);

/*
 * 删除用户
 * 参数: name - 用户名
 * 返回: 0 成功, -1 失败
 */
int funsos_security_user_delete(const char *name);

/*
 * 修改用户密码
 * 参数: name - 用户名; password - 新密码
 * 返回: 0 成功, -1 失败
 */
int funsos_security_user_set_password(const char *name, const char *password);

/*
 * 切换当前用户 (类似 su)
 * 参数: name - 目标用户名; password - 该用户密码
 * 返回: 0 成功, -1 失败 (认证失败)
 */
int funsos_security_user_switch(const char *name, const char *password);

#ifdef __cplusplus
}
#endif

#endif /* FUNSOS_SECURITY_H */
