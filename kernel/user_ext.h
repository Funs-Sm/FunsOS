#ifndef USER_EXT_H
#define USER_EXT_H

#include "stdint.h"

#define USER_EXT_MAX_NAME      64
#define USER_EXT_MAX_PASS      64
#define USER_EXT_MAX_HOME      256
#define USER_EXT_MAX_SHELL     128
#define USER_EXT_MAX_GROUPS    16
#define USER_EXT_MAX_MEMBERS   256

/* 用户标志 */
#define USER_EXT_FLAG_ACTIVE     0x01
#define USER_EXT_FLAG_ADMIN      0x02
#define USER_EXT_FLAG_SYSTEM     0x04
#define USER_EXT_FLAG_NO_LOGIN   0x08

/* 权限级别 */
#define USER_EXT_PERM_NONE    0
#define USER_EXT_PERM_READ    0x04
#define USER_EXT_PERM_WRITE   0x02
#define USER_EXT_PERM_EXEC    0x01

/* 会话 */
#define USER_EXT_MAX_SESSIONS  256

/* 用户组 */
typedef struct user_group {
    uint32_t gid;
    char name[64];
    uint32_t members[256];
    uint32_t member_count;
    struct user_group *next;
} user_group_t;

/* 扩展用户 */
typedef struct user_ext {
    uint32_t uid;
    char username[64];
    char password_hash[64];
    char home_dir[256];
    char shell[128];
    uint32_t gid;
    uint32_t groups[16];
    uint32_t group_count;
    uint32_t created;
    uint32_t last_login;
    uint32_t login_count;
    uint32_t flags;
    uint32_t quota_soft;
    uint32_t quota_hard;
    uint32_t quota_used;
    struct user_ext *next;
} user_ext_t;

/* 用户会话 */
typedef struct user_session {
    uint32_t session_id;
    uint32_t uid;
    uint32_t login_time;
    uint32_t last_active;
    int active;
} user_session_t;

/* ---- API 声明 ---- */

void user_ext_init(void);

/* CRUD */
int user_ext_create(const char *username, const char *password, const char *home);
int user_ext_delete(const char *username);
int user_ext_set_password(const char *username, const char *new_password);

/* 认证 */
int user_ext_authenticate(const char *username, const char *password);
int user_ext_login(const char *username, const char *password);
int user_ext_logout(void);
user_ext_t *user_ext_get_current(void);

/* 查找 */
user_ext_t *user_ext_find_by_name(const char *username);
user_ext_t *user_ext_find_by_uid(uint32_t uid);

/* 组管理 */
int user_group_create(const char *name, uint32_t gid);
int user_group_delete(const char *name);
int user_group_add_member(const char *group_name, uint32_t uid);
int user_group_remove_member(const char *group_name, uint32_t uid);
user_group_t *user_group_find_by_name(const char *name);
user_group_t *user_group_find_by_gid(uint32_t gid);

/* 权限检查 */
int user_ext_check_permission(uint32_t uid, uint32_t gid, uint32_t required_perm);
int user_ext_is_root(void);

/* 用户会话 */
int user_ext_session_create(uint32_t uid);
int user_ext_session_destroy(uint32_t session_id);
user_session_t *user_ext_session_get(uint32_t session_id);

/* 用户目录 */
int user_ext_create_home(const char *username);

#endif /* USER_EXT_H */