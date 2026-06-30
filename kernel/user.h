#ifndef USER_H
#define USER_H

#include "stdint.h"

#define USER_MAX_NAME  32
#define USER_MAX_USERS 64
#define USER_MAX_PASS_HASH 0xFFFFFFFF
#define USER_MAX_GROUPS 32
#define USER_MAX_GROUP_MEMBERS 16

#define USER_UID_SOVER    0
#define USER_UID_ADMIN    1
#define USER_UID_NOBODY   65534
#define USER_UID_MIN      1000
#define USER_UID_MAX      60000

#define USER_GID_ROOT     0
#define USER_GID_ADMIN    1
#define USER_GID_USERS    100
#define USER_GID_NOGROUP  65534

typedef enum {
    USER_ROLE_SOVER = 0,
    USER_ROLE_ADMIN = 1,
    USER_ROLE_USER  = 2,
    USER_ROLE_NOBODY = 3
} user_role_t;

typedef struct {
    uint32_t uid;
    uint32_t gid;
    char username[USER_MAX_NAME];
    char home[128];
    char shell[64];
    uint8_t is_admin;
    uint8_t is_active;
    uint32_t password_hash;
} user_t;

typedef struct {
    uint32_t gid;
    char name[USER_MAX_NAME];
    uint32_t members[USER_MAX_GROUP_MEMBERS];
    uint32_t member_count;
} group_t;

void user_init(void);
int user_create(const char *name, uint32_t uid, uint32_t gid, uint8_t admin);
int user_delete(const char *name);
int user_set_password(const char *name, uint32_t password_hash);
int user_change_password(const char *name, const char *password);
int user_set_admin(const char *name, uint8_t admin);
int user_set_home(const char *name, const char *home);
int user_set_shell(const char *name, const char *shell);
user_t *user_find_by_name(const char *name);
user_t *user_find_by_uid(uint32_t uid);
int user_authenticate(const char *name, const char *password);
uint32_t user_count(void);
user_t *user_get_by_index(uint32_t index);

int group_create(const char *name, uint32_t gid);
int group_add_member(uint32_t gid, uint32_t uid);
int group_remove_member(uint32_t gid, uint32_t uid);

uint32_t group_count(void);
group_t *group_get_by_index(uint32_t index);
group_t *group_find_by_gid(uint32_t gid);
group_t *group_find_by_name(const char *name);

void user_set_current(uint32_t uid);
uint32_t user_get_current_uid(void);
user_t *user_get_current(void);

int user_rename(uint32_t uid, const char *new_name);

uint32_t user_hash_password(const char *password);
uint32_t user_hash_password_salt(const char *password, const char *username);

user_role_t user_get_role(uint32_t uid);
const char *user_role_name(user_role_t role);
int user_is_sover(uint32_t uid);
int user_is_admin(uint32_t uid);
int user_is_regular(uint32_t uid);
int user_is_nobody(uint32_t uid);

uint32_t user_alloc_uid(void);
uint32_t user_alloc_gid(void);

int user_get_groups(uint32_t uid, uint32_t *groups, uint32_t max_groups);
int user_in_group(uint32_t uid, uint32_t gid);

#endif
