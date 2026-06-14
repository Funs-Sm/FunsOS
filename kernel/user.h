#ifndef USER_H
#define USER_H

#include "stdint.h"

#define USER_MAX_NAME  32
#define USER_MAX_USERS 64
#define USER_MAX_PASS_HASH 0xFFFFFFFF

typedef struct {
    uint32_t uid;
    uint32_t gid;
    char username[USER_MAX_NAME];
    char home[128];
    char shell[64];
    uint8_t is_admin;
    uint8_t is_active;       /* account enabled */
    uint32_t password_hash;
} user_t;

typedef struct {
    uint32_t gid;
    char name[USER_MAX_NAME];
    uint32_t members[16];
    uint32_t member_count;
} group_t;

void user_init(void);
int user_create(const char *name, uint32_t uid, uint32_t gid, uint8_t admin);
int user_delete(const char *name);
int user_set_password(const char *name, uint32_t password_hash);
int user_set_admin(const char *name, uint8_t admin);
user_t *user_find_by_name(const char *name);
user_t *user_find_by_uid(uint32_t uid);
int user_authenticate(const char *name, const char *password);
uint32_t user_count(void);
user_t *user_get_by_index(uint32_t index);

int group_create(const char *name, uint32_t gid);
int group_add_member(uint32_t gid, uint32_t uid);

/* 组查询 API */
uint32_t group_count(void);
group_t *group_get_by_index(uint32_t index);
group_t *group_find_by_gid(uint32_t gid);

/* Current logged-in user tracking */
void user_set_current(uint32_t uid);
uint32_t user_get_current_uid(void);
user_t *user_get_current(void);

int user_rename(uint32_t uid, const char *new_name);

/* Simple hash function for passwords */
uint32_t user_hash_password(const char *password);

/* 带盐值的密码哈希: hash = djb2(password + username) */
uint32_t user_hash_password_salt(const char *password, const char *username);

#endif
