#ifndef PERMISSION_H
#define PERMISSION_H

#include "stdint.h"

#define PERM_READ  0x04
#define PERM_WRITE 0x02
#define PERM_EXEC  0x01

/* 权限级别常量 */
#define PERM_LEVEL_SOVER   0    /* 最高权限，可操作一切 */
#define PERM_LEVEL_ADMIN   1    /* 管理员权限，可管理用户和系统 */
#define PERM_LEVEL_USER    2    /* 普通用户权限 */
#define PERM_LEVEL_NOBODY  3    /* 最低权限 */

int perm_check(uint32_t file_uid, uint32_t file_gid, uint16_t file_mode, uint32_t proc_uid, uint32_t proc_gid, uint32_t access);
int perm_check_read(uint16_t mode, uint32_t uid, uint32_t gid, uint32_t proc_uid, uint32_t proc_gid);
int perm_check_write(uint16_t mode, uint32_t uid, uint32_t gid, uint32_t proc_uid, uint32_t proc_gid);
int perm_check_exec(uint16_t mode, uint32_t uid, uint32_t gid, uint32_t proc_uid, uint32_t proc_gid);

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
