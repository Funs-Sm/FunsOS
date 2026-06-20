#ifndef PERMISSION_H
#define PERMISSION_H

#include "stdint.h"

#define PERM_READ  0x04
#define PERM_WRITE 0x02
#define PERM_EXEC  0x01

/* 特殊权限位: setuid / setgid / sticky bit */
#define S_ISUID    04000u   /* 设置用户 ID 位 (执行时以文件 owner 身份运行) */
#define S_ISGID    02000u   /* 设置组 ID 位 (执行时以文件 group 身份运行) */
#define S_ISVTX    01000u   /* 粘滞位 (仅 owner 可删除目录中的文件) */

/* ACL (Access Control List) 最大条目数 */
#define ACL_MAX_ENTRIES 16

/* ACL 单条目: 匹配指定 uid/gid 的权限规则 */
typedef struct {
    uint32_t uid;        /* 匹配的用户 UID, (uint32_t)-1 表示匹配所有 */
    uint32_t gid;        /* 匹配的组 GID, (uint32_t)-1 表示匹配所有 */
    uint16_t perms;      /* rwx 权限位 (PERM_READ|PERM_WRITE|PERM_EXEC) */
} acl_entry_t;

/* ACL 结构体: 一组有序的访问控制条目 */
typedef struct {
    acl_entry_t entries[ACL_MAX_ENTRIES];
    uint32_t count;
} acl_t;

/* 权限级别常量 */
#define PERM_LEVEL_SOVER   0    /* 最高权限，可操作一切 */
#define PERM_LEVEL_ADMIN   1    /* 管理员权限，可管理用户和系统 */
#define PERM_LEVEL_USER    2    /* 普通用户权限 */
#define PERM_LEVEL_NOBODY  3    /* 最低权限 */

/* 基础权限检查函数 */
int perm_check(uint32_t file_uid, uint32_t file_gid, uint16_t file_mode, uint32_t proc_uid, uint32_t proc_gid, uint32_t access);
int perm_check_read(uint16_t mode, uint32_t uid, uint32_t gid, uint32_t proc_uid, uint32_t proc_gid);
int perm_check_write(uint16_t mode, uint32_t uid, uint32_t gid, uint32_t proc_uid, uint32_t proc_gid);
int perm_check_exec(uint16_t mode, uint32_t uid, uint32_t gid, uint32_t proc_uid, uint32_t proc_gid);

/* ACL 权限检查 */
int acl_check(const acl_t *acl, uint32_t uid, uint32_t gid, uint16_t required_perm);

/* 扩展权限检查: 先走传统 rwx, 再查 ACL */
int perm_check_extended(uint32_t file_uid, uint32_t file_gid, uint16_t file_mode,
                        const acl_t *acl, uint32_t proc_uid, uint32_t proc_gid, uint32_t access);

/* 文件模式修改: 仅允许 owner 或 admin 更改 */
int perm_set_mode(uint16_t *mode, uint16_t new_mode, uint32_t proc_uid, int is_admin);

/* umask 管理 */
uint32_t perm_umask_get(void);
void perm_umask_set(uint32_t mask);

/* 检查当前用户是否为 Sover */
int perm_is_sover(void);

/* 检查当前用户是否为管理员（Sover 或 Admin）*/
int perm_is_admin(void);

/* 检查当前用户对指定路径的访问权限 */
int perm_check_path(const char *path, uint32_t required_perm);

/* 获取权限不足的详细提示信息 */
const char *perm_denied_reason(const char *path, uint32_t required_perm);

/* 获取当前用户的权限级别 */
int perm_get_level(void);

/* 获取权限级别对应的名称 */
const char *perm_level_name(int level);

#endif
