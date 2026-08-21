/* funsos_security.c - 安全与权限管理子模块实现
 *
 * 桥接 SDK 安全 API 到内核 permission/user/acl 子系统
 */

#include "funsos.h"
#include "funsos_security.h"
#include "permission.h"
#include "user.h"
#include "acl.h"
#include "vfs.h"
#include "kheap.h"
#include "string.h"

/* ---- 用户/组信息查询 ---- */

uint32_t funsos_security_get_uid(void) {
    user_t *u = user_get_current();
    return u ? u->uid : 65534;
}

uint32_t funsos_security_get_gid(void) {
    user_t *u = user_get_current();
    return u ? u->gid : 65534;
}

int funsos_security_get_level(void) {
    return perm_get_level();
}

const char *funsos_security_level_name(int level) {
    return perm_level_name(level);
}

int funsos_security_is_sover(void) {
    return perm_is_sover();
}

int funsos_security_is_admin(void) {
    return perm_is_admin();
}

/* ---- 文件权限 ---- */

int funsos_security_check_path(const char *path, uint32_t required) {
    if (!path) return -1;
    return perm_check_path(path, required);
}

int funsos_security_chmod(const char *path, uint32_t mode) {
    if (!path) return -1;
    if (vfs_chmod(path, mode) != 0) return -1;
    return 0;
}

int funsos_security_chown(const char *path, uint32_t uid, uint32_t gid) {
    if (!path) return -1;
    /* 如果 gid 不修改，使用 -1 表示 */
    if (gid == 0xFFFFFFFF) {
        inode_t st;
        if (vfs_stat(path, &st) != 0) return -1;
        gid = st.gid;
    }
    if (vfs_chown(path, uid, gid) != 0) return -1;
    return 0;
}

int funsos_security_chgrp(const char *path, uint32_t gid) {
    if (!path) return -1;
    inode_t st;
    if (vfs_stat(path, &st) != 0) return -1;
    if (vfs_chown(path, st.uid, gid) != 0) return -1;
    return 0;
}

/* ---- ACL 管理 ---- */

int funsos_security_acl_get(const char *path, funsos_acl_t *acl) {
    if (!path || !acl) return -1;
    inode_t st;
    if (vfs_stat(path, &st) != 0) return -1;
    acl->owner_uid = st.uid;
    acl->group_gid = st.gid;
    acl->owner_perm = (uint8_t)((st.mode >> 6) & 7);
    acl->group_perm = (uint8_t)((st.mode >> 3) & 7);
    acl->other_perm = (uint8_t)(st.mode & 7);
    /* 内核 inode_t 中的 acl 是 struct acl* */
    if (st.acl) {
        acl->entries = (const funsos_acl_entry_t *)st.acl->entries;
        acl->entry_count = st.acl->entry_count;
    } else {
        acl->entries = NULL;
        acl->entry_count = 0;
    }
    return 0;
}

int funsos_security_acl_add(const char *path, const funsos_acl_entry_t *entry) {
    if (!path || !entry) return -1;
    inode_t st;
    if (vfs_stat(path, &st) != 0) return -1;
    if (!st.acl) return -1;
    /* 通过内核 ACL API 添加条目 */
    if (acl_add_entry(st.acl, entry->uid, entry->gid, (uint16_t)entry->perms) != 0) {
        return -1;
    }
    return 0;
}

int funsos_security_acl_remove(const char *path, uint32_t index) {
    if (!path) return -1;
    inode_t st;
    if (vfs_stat(path, &st) != 0) return -1;
    if (!st.acl) return -1;
    if (acl_remove_entry(st.acl, index) != 0) return -1;
    return 0;
}

int funsos_security_acl_clear(const char *path) {
    if (!path) return -1;
    inode_t st;
    if (vfs_stat(path, &st) != 0) return -1;
    if (!st.acl) return 0;
    acl_clear(st.acl);
    return 0;
}

/* ---- 能力 (capability) ---- */

int funsos_security_has_capability(uint32_t cap) {
    if (cap >= 32) return 0;
    /* Sover 拥有所有能力 */
    if (perm_is_sover()) return 1;
    /* Admin 拥有大部分能力（除 SYS_ADMIN/SYS_BOOT/SYS_MODULE 外） */
    if (perm_is_admin()) {
        if (cap == FUNSOS_CAP_SYS_ADMIN ||
            cap == FUNSOS_CAP_SYS_BOOT ||
            cap == FUNSOS_CAP_SYS_MODULE) {
            return 0;
        }
        return 1;
    }
    /* 普通用户仅拥有 NET_BIND_SERVICE 等基础能力 */
    if (cap == FUNSOS_CAP_NET_BIND_SERVICE) return 1;
    return 0;
}

uint32_t funsos_security_get_capabilities(void) {
    uint32_t caps = 0;
    for (uint32_t i = 0; i < 32; i++) {
        if (funsos_security_has_capability(i)) {
            caps |= (1u << i);
        }
    }
    return caps;
}

/* ---- umask ---- */

uint32_t funsos_security_umask_get(void) {
    return perm_umask_get();
}

uint32_t funsos_security_umask_set(uint32_t mask) {
    uint32_t old = perm_umask_get();
    perm_umask_set(mask);
    return old;
}

/* ---- 用户管理 ---- */

int funsos_security_user_create(const char *name, int admin) {
    if (!name || !*name) return -1;
    if (!permission_can_create_user()) return -1;
    uint32_t uid = user_alloc_uid();
    if (uid == 0) return -1;
    uint32_t gid = uid;
    if (group_find_by_gid(gid)) {
        gid = user_alloc_gid();
        if (gid == 0) return -1;
    }
    if (user_create(name, uid, gid, (uint8_t)(admin ? 1 : 0)) != 0) return -1;
    group_create(name, gid);
    group_add_member(gid, uid);
    return 0;
}

int funsos_security_user_delete(const char *name) {
    if (!name || !*name) return -1;
    user_t *target = user_find_by_name(name);
    if (!target) return -1;
    if (!permission_can_delete_user(target->uid)) return -1;
    if (user_delete(name) != 0) return -1;
    return 0;
}

int funsos_security_user_set_password(const char *name, const char *password) {
    if (!name || !password) return -1;
    if (user_change_password(name, password) != 0) return -1;
    return 0;
}

int funsos_security_user_switch(const char *name, const char *password) {
    if (!name || !password) return -1;
    if (user_authenticate(name, password) != 0) return -1;
    user_t *target = user_find_by_name(name);
    if (!target) return -1;
    user_set_current(target->uid);
    return 0;
}
