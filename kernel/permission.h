#ifndef PERMISSION_H
#define PERMISSION_H

#include "stdint.h"

#define PERM_READ  0x04
#define PERM_WRITE 0x02
#define PERM_EXEC  0x01

#define S_ISUID    04000u
#define S_ISGID    02000u
#define S_ISVTX    01000u

#define ACL_MAX_ENTRIES 16

typedef struct {
    uint32_t uid;
    uint32_t gid;
    uint8_t  perms;
} acl_entry_t;

typedef struct acl {
    uint32_t owner_uid;
    uint32_t group_gid;
    uint8_t  owner_perm;
    uint8_t  group_perm;
    uint8_t  other_perm;
    acl_entry_t *entries;
    uint32_t entry_count;
} acl_t;

#define PERM_LEVEL_SOVER   0
#define PERM_LEVEL_ADMIN   1
#define PERM_LEVEL_USER    2
#define PERM_LEVEL_NOBODY  3

#define UID_SOVER    0
#define UID_ADMIN    1
#define UID_NOBODY   65534
#define UID_USER_MIN 1000
#define UID_USER_MAX 60000

#define GID_ROOT     0
#define GID_ADMIN    1
#define GID_NOGROUP  65534
#define GID_USER_MIN 1000

int perm_check(uint32_t file_uid, uint32_t file_gid, uint16_t file_mode,
               uint32_t proc_uid, uint32_t proc_gid, uint32_t access);

int perm_check_read(uint16_t mode, uint32_t uid, uint32_t gid,
                    uint32_t proc_uid, uint32_t proc_gid);
int perm_check_write(uint16_t mode, uint32_t uid, uint32_t gid,
                     uint32_t proc_uid, uint32_t proc_gid);
int perm_check_exec(uint16_t mode, uint32_t uid, uint32_t gid,
                    uint32_t proc_uid, uint32_t proc_gid);

int perm_check_current_read(uint16_t mode, uint32_t uid, uint32_t gid);
int perm_check_current_write(uint16_t mode, uint32_t uid, uint32_t gid);
int perm_check_current_exec(uint16_t mode, uint32_t uid, uint32_t gid);

int perm_check_extended(uint32_t file_uid, uint32_t file_gid, uint16_t file_mode,
                        struct acl *acl, uint32_t proc_uid, uint32_t proc_gid, uint32_t access);

int perm_set_mode(uint16_t *mode, uint16_t new_mode, uint32_t file_uid,
                  uint32_t proc_uid, int is_admin);

uint32_t perm_umask_get(void);
void perm_umask_set(uint32_t mask);

int perm_is_sover(void);
int perm_is_admin(void);

int perm_check_path(const char *path, uint32_t required_perm);

const char *perm_denied_reason(const char *path, uint32_t required_perm);
int perm_get_level(void);
const char *perm_level_name(int level);

int permission_check_file(const char *path, uint32_t required_perm);
int permission_check_user(uint32_t target_uid);
int permission_check_user_operation(uint32_t target_uid, int is_admin_op);
int permission_can_chmod(uint32_t file_uid);
int permission_can_chown(uint32_t file_uid);
int permission_can_create_user(void);
int permission_can_delete_user(uint32_t target_uid);
int permission_can_modify_user(uint32_t target_uid);

const char *perm_mode_string(uint16_t mode, char *buf, int buf_size);
const char *perm_uid_to_name(uint32_t uid, char *buf, int buf_size);
const char *perm_gid_to_name(uint32_t gid, char *buf, int buf_size);

#endif
