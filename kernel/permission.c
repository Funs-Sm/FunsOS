#include "permission.h"
#include "acl.h"
#include "user.h"
#include "string.h"
#include "kheap.h"
#include "vfs.h"
#include "dentry.h"
#include "path.h"

static uint32_t current_umask = 0022;

uint32_t perm_umask_get(void) {
    return current_umask;
}

void perm_umask_set(uint32_t mask) {
    current_umask = mask & 07777;
}

int perm_check(uint32_t file_uid, uint32_t file_gid, uint16_t file_mode,
               uint32_t proc_uid, uint32_t proc_gid, uint32_t access) {
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

int perm_check_read(uint16_t mode, uint32_t uid, uint32_t gid,
                    uint32_t proc_uid, uint32_t proc_gid) {
    return perm_check(uid, gid, mode, proc_uid, proc_gid, PERM_READ);
}

int perm_check_write(uint16_t mode, uint32_t uid, uint32_t gid,
                     uint32_t proc_uid, uint32_t proc_gid) {
    return perm_check(uid, gid, mode, proc_uid, proc_gid, PERM_WRITE);
}

int perm_check_exec(uint16_t mode, uint32_t uid, uint32_t gid,
                    uint32_t proc_uid, uint32_t proc_gid) {
    return perm_check(uid, gid, mode, proc_uid, proc_gid, PERM_EXEC);
}

int perm_check_current_read(uint16_t mode, uint32_t uid, uint32_t gid) {
    user_t *u = user_get_current();
    uint32_t proc_uid = u ? u->uid : 65534;
    uint32_t proc_gid = u ? u->gid : 65534;
    return perm_check(uid, gid, mode, proc_uid, proc_gid, PERM_READ);
}

int perm_check_current_write(uint16_t mode, uint32_t uid, uint32_t gid) {
    user_t *u = user_get_current();
    uint32_t proc_uid = u ? u->uid : 65534;
    uint32_t proc_gid = u ? u->gid : 65534;
    return perm_check(uid, gid, mode, proc_uid, proc_gid, PERM_WRITE);
}

int perm_check_current_exec(uint16_t mode, uint32_t uid, uint32_t gid) {
    user_t *u = user_get_current();
    uint32_t proc_uid = u ? u->uid : 65534;
    uint32_t proc_gid = u ? u->gid : 65534;
    return perm_check(uid, gid, mode, proc_uid, proc_gid, PERM_EXEC);
}

int perm_check_extended(uint32_t file_uid, uint32_t file_gid, uint16_t file_mode,
                        acl_t *acl, uint32_t proc_uid, uint32_t proc_gid, uint32_t access) {
    if (proc_uid == 0) return 0;

    int ret = perm_check(file_uid, file_gid, file_mode, proc_uid, proc_gid, access);
    if (ret == 0) return 0;

    if (acl) {
        if (acl_check(acl, proc_uid, proc_gid, (uint8_t)access)) {
            return 0;
        }
    }

    return -1;
}

int perm_set_mode(uint16_t *mode, uint16_t new_mode, uint32_t file_uid,
                  uint32_t proc_uid, int is_admin) {
    if (!mode) return -1;

    if (proc_uid == 0 || is_admin) {
        *mode = new_mode & 07777;
        return 0;
    }

    if (proc_uid == file_uid) {
        *mode = new_mode & 07777;
        return 0;
    }

    return -1;
}

int perm_is_sover(void) {
    return user_get_current_uid() == 0;
}

int perm_is_admin(void) {
    uint32_t uid = user_get_current_uid();
    if (uid == 0) return 1;
    user_t *u = user_get_current();
    if (!u) return 0;
    return u->is_admin;
}

static int path_starts_with(const char *path, const char *prefix) {
    while (*prefix) {
        if (*path != *prefix) return 0;
        path++;
        prefix++;
    }
    if (*path == '/' || *path == '\0') return 1;
    return 0;
}

int perm_check_path(const char *path, uint32_t required_perm) {
    if (!path) return -1;

    if (perm_is_sover()) return 0;

    if (path_starts_with(path, "/sys") && (required_perm & PERM_WRITE)) {
        return -1;
    }

    if (path_starts_with(path, "/dev") && (required_perm & PERM_WRITE)) {
        if (!perm_is_admin()) return -1;
    }

    return 0;
}

int perm_get_level(void) {
    user_t *u = user_get_current();
    if (!u) return PERM_LEVEL_NOBODY;
    if (u->uid == 0) return PERM_LEVEL_SOVER;
    if (u->is_admin) return PERM_LEVEL_ADMIN;
    if (u->uid == 65534) return PERM_LEVEL_NOBODY;
    return PERM_LEVEL_USER;
}

const char *perm_level_name(int level) {
    switch (level) {
        case PERM_LEVEL_SOVER:  return "Sover";
        case PERM_LEVEL_ADMIN:  return "Admin";
        case PERM_LEVEL_USER:   return "User";
        case PERM_LEVEL_NOBODY: return "Nobody";
        default: return "Unknown";
    }
}

const char *perm_denied_reason(const char *path, uint32_t required_perm) {
    int level = perm_get_level();

    if (path && path_starts_with(path, "/sys") && (required_perm & PERM_WRITE)) {
        return "Permission denied: /sys is read-only (Sover only)";
    }

    if (path && path_starts_with(path, "/dev") && (required_perm & PERM_WRITE)) {
        return "Permission denied: /dev write access requires Admin or Sover";
    }

    if (required_perm & PERM_EXEC) {
        return "Permission denied: execute permission required";
    }

    if (required_perm & PERM_WRITE) {
        return "Permission denied: write permission required";
    }

    if (required_perm & PERM_READ) {
        return "Permission denied: read permission required";
    }

    return "Permission denied";
}

int permission_check_file(const char *path, uint32_t required_perm) {
    if (!path) return -1;

    if (perm_is_sover()) return 0;

    if (perm_check_path(path, required_perm) != 0) return -1;

    dentry_t *dentry = 0;
    if (path_resolve(path, &dentry) != 0 || !dentry || !dentry->inode) {
        return -1;
    }

    user_t *u = user_get_current();
    uint32_t proc_uid = u ? u->uid : UID_NOBODY;
    uint32_t proc_gid = u ? u->gid : GID_NOGROUP;

    return perm_check(dentry->inode->uid, dentry->inode->gid,
                      (uint16_t)dentry->inode->mode,
                      proc_uid, proc_gid, required_perm);
}

int permission_check_user(uint32_t target_uid) {
    user_t *u = user_get_current();
    if (!u) return -1;

    if (u->uid == target_uid) return 0;

    if (u->uid == UID_SOVER) return 0;

    if (u->is_admin && target_uid != UID_SOVER) return 0;

    return -1;
}

int permission_check_user_operation(uint32_t target_uid, int is_admin_op) {
    user_t *u = user_get_current();
    if (!u) return -1;

    if (u->uid == UID_SOVER) return 0;

    if (is_admin_op && !u->is_admin) return -1;

    if (target_uid == UID_SOVER && u->uid != UID_SOVER) return -1;

    if (u->is_admin) return 0;

    if (u->uid == target_uid && !is_admin_op) return 0;

    return -1;
}

int permission_can_chmod(uint32_t file_uid) {
    user_t *u = user_get_current();
    if (!u) return 0;

    if (u->uid == UID_SOVER) return 1;

    if (u->is_admin) return 1;

    if (u->uid == file_uid) return 1;

    return 0;
}

int permission_can_chown(uint32_t file_uid) {
    user_t *u = user_get_current();
    if (!u) return 0;

    if (u->uid == UID_SOVER) return 1;

    if (u->is_admin) return 1;

    return 0;
}

int permission_can_create_user(void) {
    return perm_is_admin();
}

int permission_can_delete_user(uint32_t target_uid) {
    if (target_uid == UID_SOVER) return 0;

    return perm_is_admin();
}

int permission_can_modify_user(uint32_t target_uid) {
    user_t *u = user_get_current();
    if (!u) return 0;

    if (u->uid == target_uid) return 1;

    if (u->uid == UID_SOVER) return 1;

    if (u->is_admin && target_uid != UID_SOVER) return 1;

    return 0;
}

const char *perm_mode_string(uint16_t mode, char *buf, int buf_size) {
    if (!buf || buf_size < 11) return "----------";

    int i = 0;

    if (mode & 0040000) buf[i++] = 'd';
    else if (mode & 0120000) buf[i++] = 'l';
    else buf[i++] = '-';

    buf[i++] = (mode & 0400) ? 'r' : '-';
    buf[i++] = (mode & 0200) ? 'w' : '-';
    if (mode & 04000) {
        buf[i++] = (mode & 0100) ? 's' : 'S';
    } else {
        buf[i++] = (mode & 0100) ? 'x' : '-';
    }

    buf[i++] = (mode & 0040) ? 'r' : '-';
    buf[i++] = (mode & 0020) ? 'w' : '-';
    if (mode & 02000) {
        buf[i++] = (mode & 0010) ? 's' : 'S';
    } else {
        buf[i++] = (mode & 0010) ? 'x' : '-';
    }

    buf[i++] = (mode & 0004) ? 'r' : '-';
    buf[i++] = (mode & 0002) ? 'w' : '-';
    if (mode & 01000) {
        buf[i++] = (mode & 0001) ? 't' : 'T';
    } else {
        buf[i++] = (mode & 0001) ? 'x' : '-';
    }

    buf[i] = '\0';
    return buf;
}

const char *perm_uid_to_name(uint32_t uid, char *buf, int buf_size) {
    if (!buf || buf_size <= 0) return "?";

    user_t *u = user_find_by_uid(uid);
    if (u) {
        strncpy(buf, u->username, buf_size - 1);
        buf[buf_size - 1] = '\0';
        return buf;
    }

    char *p = buf;
    char tmp[16];
    int n = 0;
    if (uid == 0) tmp[n++] = '0';
    else {
        uint32_t val = uid;
        while (val > 0) {
            tmp[n++] = '0' + (val % 10);
            val /= 10;
        }
    }
    for (int i = n - 1; i >= 0; i--) {
        *p++ = tmp[i];
    }
    *p = '\0';
    return buf;
}

const char *perm_gid_to_name(uint32_t gid, char *buf, int buf_size) {
    if (!buf || buf_size <= 0) return "?";

    group_t *g = group_find_by_gid(gid);
    if (g) {
        strncpy(buf, g->name, buf_size - 1);
        buf[buf_size - 1] = '\0';
        return buf;
    }

    char *p = buf;
    char tmp[16];
    int n = 0;
    if (gid == 0) tmp[n++] = '0';
    else {
        uint32_t val = gid;
        while (val > 0) {
            tmp[n++] = '0' + (val % 10);
            val /= 10;
        }
    }
    for (int i = n - 1; i >= 0; i--) {
        *p++ = tmp[i];
    }
    *p = '\0';
    return buf;
}
