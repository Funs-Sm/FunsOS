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

#endif
