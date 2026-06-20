#ifndef ACL_H
#define ACL_H

#include "stdint.h"
#include "permission.h"

/* 初始化一个空的 ACL */
int acl_create(acl_t *acl);

/* 向 ACL 追加一条访问规则条目
 * uid: 匹配用户 UID, (uint32_t)-1 表示通配所有用户
 * gid: 匹配组 GID, (uint32_t)-1 表示通配所有组
 * perms: 授予的权限 (PERM_READ | PERM_WRITE | PERM_EXEC)
 * 返回 0 成功, -1 失败 (ACL 已满) */
int acl_add_entry(acl_t *acl, uint32_t uid, uint32_t gid, uint16_t perms);

/* 按索引删除 ACL 条目
 * index: 要删除的条目索引 (0-based)
 * 返回 0 成功, -1 失败 (索引越界) */
int acl_remove_entry(acl_t *acl, uint32_t index);

/* 检查指定用户/组是否拥有所需权限 (委托给 acl_check) */
int acl_check_access(const acl_t *acl, uint32_t uid, uint32_t gid, uint16_t required);

/* 清空 ACL 中所有条目 */
void acl_clear(acl_t *acl);

#endif
