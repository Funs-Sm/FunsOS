#include "permission.h"
#include "acl.h"
#include "user.h"
#include "string.h"
#include "kheap.h"

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
