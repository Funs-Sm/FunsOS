#include "permission.h"
#include "user.h"
#include "string.h"

/* ------------------------------------------------------------------ */
/* umask: 进程创建文件时的权限掩码, 默认 0022 (屏蔽 group/other 写)   */
/* ------------------------------------------------------------------ */
static uint32_t current_umask = 0022;

uint32_t perm_umask_get(void) {
    return current_umask;
}

void perm_umask_set(uint32_t mask) {
    /* 只保留低 9 位 (rwxrwxrwx) + 特殊位 */
    current_umask = mask & 07777;
}

/* ------------------------------------------------------------------ */
/* 基础权限检查                                                        */
/* ------------------------------------------------------------------ */

int perm_check(uint32_t file_uid, uint32_t file_gid, uint16_t file_mode, uint32_t proc_uid, uint32_t proc_gid, uint32_t access) {
    if (proc_uid == 0) return 0;

    if (proc_uid == file_uid) {
        uint16_t owner_bits = (file_mode >> 6) & 7;
        if ((owner_bits & access) == access) return 0;
        return -1;
    }

    if (proc_gid == file_gid) {
        uint16_t group_bits = (file_mode >> 3) & 7;
        if ((group_bits & access) == access) return 0;
        return -1;
    }

    uint16_t other_bits = file_mode & 7;
    if ((other_bits & access) == access) return 0;
    return -1;
}

int perm_check_read(uint16_t mode, uint32_t uid, uint32_t gid, uint32_t proc_uid, uint32_t proc_gid) {
    return perm_check(uid, gid, mode, proc_uid, proc_gid, PERM_READ);
}

int perm_check_write(uint16_t mode, uint32_t uid, uint32_t gid, uint32_t proc_uid, uint32_t proc_gid) {
    return perm_check(uid, gid, mode, proc_uid, proc_gid, PERM_WRITE);
}

int perm_check_exec(uint16_t mode, uint32_t uid, uint32_t gid, uint32_t proc_uid, uint32_t proc_gid) {
    return perm_check(uid, gid, mode, proc_uid, proc_gid, PERM_EXEC);
}

/* ------------------------------------------------------------------ */
/* ACL 权限检查                                                         */
/* 按顺序遍历 ACL 条目, 首条匹配的规则决定结果                          */
/* ------------------------------------------------------------------ */

int acl_check(const acl_t *acl, uint32_t uid, uint32_t gid, uint16_t required_perm) {
    if (!acl || acl->count == 0) {
        /* 无 ACL 时返回拒绝, 由调用方回退到传统 rwx 检查 */
        return -1;
    }

    uint32_t i;
    for (i = 0; i < acl->count; i++) {
        const acl_entry_t *e = &acl->entries[i];

        /* 检查 UID 匹配: 精确匹配或通配符 */
        int uid_match = (e->uid == (uint32_t)-1) || (e->uid == uid);

        /* 检查 GID 匹配: 精确匹配或通配符 */
        int gid_match = (e->gid == (uint32_t)-1) || (e->gid == gid);

        if (uid_match && gid_match) {
            /* 条目匹配, 检查是否拥有所需全部权限位 */
            if ((e->perms & required_perm) == required_perm) {
                return 0;   /* 授权 */
            }
            return -1;     /* 匹配但权限不足 -> 明确拒绝 */
        }
    }

    /* 无任何条目匹配 -> 默认拒绝 */
    return -1;
}

/* ------------------------------------------------------------------ */
/* 扩展权限检查: 先走传统 rwx, 若被拒绝则尝试 ACL                      */
/* ------------------------------------------------------------------ */

int perm_check_extended(uint32_t file_uid, uint32_t file_gid, uint16_t file_mode,
                        const acl_t *acl, uint32_t proc_uid, uint32_t proc_gid, uint32_t access) {
    /* Sover 跳过所有检查 */
    if (proc_uid == 0) return 0;

    /* 第一步: 传统 Unix rwx 权限检查 */
    int ret = perm_check(file_uid, file_gid, file_mode, proc_uid, proc_gid, access);
    if (ret == 0) return 0;

    /* 第二步: 传统检查未通过, 尝试 ACL 补充授权 */
    if (acl && acl->count > 0) {
        ret = acl_check(acl, proc_uid, proc_gid, (uint16_t)access);
        if (ret == 0) return 0;
    }

    return -1;
}

/* ------------------------------------------------------------------ */
/* 文件模式修改权限控制                                                 */
/* 仅允许文件 owner 或管理员更改模式                                    */
/* ------------------------------------------------------------------ */

int perm_set_mode(uint16_t *mode, uint16_t new_mode, uint32_t proc_uid, int is_admin) {
    if (!mode) return -1;

    /* admin 可直接修改 */
    if (is_admin) {
        *mode = new_mode & 07777;
        return 0;
    }

    /* owner 可修改自身文件的权限 */
    if (proc_uid != 0) {
        *mode = new_mode & 07777;
        return 0;
    }

    return -1;
}

/* ------------------------------------------------------------------ */
/* 用户身份检查                                                         */
/* ------------------------------------------------------------------ */

/* 检查当前用户是否为 Sover (uid == 0) */
int perm_is_sover(void) {
    uint32_t uid = user_get_current_uid();
    return uid == 0;
}

/* 检查当前用户是否为管理员（Sover 或 Admin）*/
int perm_is_admin(void) {
    user_t *u = user_get_current();
    if (!u) return 0;
    return u->is_admin;
}

/* ------------------------------------------------------------------ */
/* 路径权限检查                                                         */
/* ------------------------------------------------------------------ */

/* 检查路径前缀: 判断 path 是否以 prefix 开头 */
static int path_starts_with(const char *path, const char *prefix) {
    while (*prefix) {
        if (*path != *prefix) return 0;
        path++;
        prefix++;
    }
    /* 确保匹配到路径分隔符或字符串结尾 */
    if (*path == '/' || *path == '\0') return 1;
    return 0;
}

/* 检查当前用户对指定路径的访问权限 */
int perm_check_path(const char *path, uint32_t required_perm) {
    if (!path) return -1;

    /* Sover 可以操作一切 */
    if (perm_is_sover()) return 0;

    /* 对 /sys 路径只允许 Sover 写入 */
    if (path_starts_with(path, "/sys") && (required_perm & PERM_WRITE)) {
        return -1;
    }

    /* 对 /dev 只允许 Sover 和 Admin 写入 */
    if (path_starts_with(path, "/dev") && (required_perm & PERM_WRITE)) {
        if (!perm_is_admin()) return -1;
    }

    /* 其他路径按常规 rwx 检查（由调用者通过 perm_check 完成）*/
    return 0;
}

/* ------------------------------------------------------------------ */
/* 权限级别与提示信息                                                   */
/* ------------------------------------------------------------------ */

/* 获取当前用户的权限级别 */
int perm_get_level(void) {
    user_t *u = user_get_current();
    if (!u) return PERM_LEVEL_NOBODY;
    if (u->uid == 0) return PERM_LEVEL_SOVER;
    if (u->is_admin) return PERM_LEVEL_ADMIN;
    if (u->uid == 65534) return PERM_LEVEL_NOBODY;
    return PERM_LEVEL_USER;
}

/* 获取权限级别对应的名称 */
const char *perm_level_name(int level) {
    switch (level) {
        case PERM_LEVEL_SOVER:  return "Sover";
        case PERM_LEVEL_ADMIN: return "Admin";
        case PERM_LEVEL_USER:  return "User";
        case PERM_LEVEL_NOBODY: return "Nobody";
        default: return "Unknown";
    }
}

/* 获取权限不足的详细提示信息 */
const char *perm_denied_reason(const char *path, uint32_t required_perm) {
    int level = perm_get_level();

    /* /sys 路径写操作 */
    if (path && path_starts_with(path, "/sys") && (required_perm & PERM_WRITE)) {
        return "Permission denied: /sys is read-only (Sover only)";
    }

    /* /dev 路径写操作 */
    if (path && path_starts_with(path, "/dev") && (required_perm & PERM_WRITE)) {
        return "Permission denied: /dev write access requires Admin or Sover";
    }

    /* 需要 Sover 权限的操作 */
    if (level > PERM_LEVEL_SOVER && required_perm == PERM_LEVEL_SOVER) {
        return "Permission denied: Only Sover can perform this operation";
    }

    /* 需要管理员权限的操作 */
    if (level > PERM_LEVEL_ADMIN && required_perm == PERM_LEVEL_ADMIN) {
        return "Permission denied: Admin or Sover required";
    }

    return "Permission denied";
}
