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

/* ================================================================ */
/*  1) 密码策略 (Password Policy)                                     */
/* ================================================================ */

#define USER_EXT_PASS_MIN_LEN     8
#define USER_EXT_PASS_MAX_LEN     64
#define USER_EXT_PASS_HISTORY_MAX 8
#define USER_EXT_PASS_EXPIRE_DAYS 90
#define USER_EXT_PASS_WARN_DAYS   7

/* 密码强度检查结果 */
#define USER_EXT_PASS_OK            0
#define USER_EXT_PASS_TOO_SHORT     1
#define USER_EXT_PASS_NO_UPPER      2
#define USER_EXT_PASS_NO_LOWER      3
#define USER_EXT_PASS_NO_DIGIT      4
#define USER_EXT_PASS_NO_SPECIAL    5
#define USER_EXT_PASS_IN_HISTORY    6
#define USER_EXT_PASS_EXPIRED       7

typedef struct user_ext_pass_policy {
    uint32_t min_length;
    int require_upper;
    int require_lower;
    int require_digit;
    int require_special;
    uint32_t expire_days;
    uint32_t warn_days;
    uint32_t history_size;
} user_ext_pass_policy_t;

typedef struct user_ext_pass_history {
    char password_hash[64];
    uint32_t changed_at;
} user_ext_pass_history_t;

int  user_ext_pass_policy_set(const user_ext_pass_policy_t *policy);
int  user_ext_pass_policy_get(user_ext_pass_policy_t *policy);
int  user_ext_pass_check_strength(const char *password);
int  user_ext_pass_is_expired(const char *username);
int  user_ext_pass_change(const char *username, const char *old_pass, const char *new_pass);
int  user_ext_pass_history_check(const char *username, const char *password);
int  user_ext_pass_history_add(const char *username, const char *password_hash);

/* ================================================================ */
/*  2) 账户锁定 (Account Lockout)                                     */
/* ================================================================ */

#define USER_EXT_LOCKOUT_MAX_FAILURES  5
#define USER_EXT_LOCKOUT_DURATION_SEC  300
#define USER_EXT_LOCKOUT_MAX_ENTRIES   256

typedef struct user_ext_lockout {
    char username[64];
    uint32_t uid;
    uint32_t failure_count;
    uint32_t first_failure;
    uint32_t last_failure;
    uint32_t locked_until;
    int locked;
} user_ext_lockout_t;

int  user_ext_lockout_init(void);
int  user_ext_lockout_record_failure(const char *username);
int  user_ext_lockout_record_success(const char *username);
int  user_ext_lockout_is_locked(const char *username);
int  user_ext_lockout_unlock(const char *username);
int  user_ext_lockout_unlock_all(void);
void user_ext_lockout_print_status(const char *username);

/* ================================================================ */
/*  3) 访问控制列表 (ACL)                                            */
/* ================================================================ */

#define USER_EXT_ACL_MAX_ENTRIES 128
#define USER_EXT_ACL_PERM_READ   0x04
#define USER_EXT_ACL_PERM_WRITE  0x02
#define USER_EXT_ACL_PERM_EXEC   0x01

typedef struct user_ext_acl_entry {
    uint32_t entry_id;
    char path[256];
    uint32_t uid;
    uint32_t gid;
    uint32_t permissions;
    int active;
} user_ext_acl_entry_t;

int  user_ext_acl_init(void);
int  user_ext_acl_set(const char *path, uint32_t uid, uint32_t gid, uint32_t perms);
int  user_ext_acl_get(const char *path, uint32_t uid, uint32_t *perms);
int  user_ext_acl_check(const char *path, uint32_t uid, uint32_t gid, uint32_t required_perm);
int  user_ext_acl_remove(const char *path, uint32_t uid);
int  user_ext_acl_remove_all(const char *path);
int  user_ext_acl_list(const char *path, void *buf, int max_entries);

/* ================================================================ */
/*  4) 用户资料 (User Profiles)                                       */
/* ================================================================ */

#define USER_EXT_PROFILE_MAX_LEN   256

typedef struct user_ext_profile {
    uint32_t uid;
    char full_name[128];
    char email[128];
    char description[256];
    char avatar_path[256];
    char language[32];
    char theme[32];
    uint32_t created;
    uint32_t updated;
    int active;
} user_ext_profile_t;

int  user_ext_profile_init(void);
int  user_ext_profile_set(uint32_t uid, const user_ext_profile_t *profile);
int  user_ext_profile_get(uint32_t uid, user_ext_profile_t *profile);
int  user_ext_profile_delete(uint32_t uid);
int  user_ext_profile_set_field(uint32_t uid, const char *field_name, const char *value);
int  user_ext_profile_get_field(uint32_t uid, const char *field_name, char *buf, int bufsize);

/* ================================================================ */
/*  5) 登录审计日志 (Login Audit Log)                                 */
/* ================================================================ */

#define USER_EXT_AUDIT_MAX_ENTRIES 1024
#define USER_EXT_AUDIT_LOGIN_SUCCESS  1
#define USER_EXT_AUDIT_LOGIN_FAILURE  2
#define USER_EXT_AUDIT_LOGOUT         3
#define USER_EXT_AUDIT_SUDO           4
#define USER_EXT_AUDIT_PASS_CHANGE    5
#define USER_EXT_AUDIT_ACCOUNT_LOCK   6

typedef struct user_ext_audit_entry {
    uint32_t entry_id;
    uint32_t uid;
    char username[64];
    uint32_t event_type;
    uint32_t timestamp;
    char ip_address[64];
    char detail[256];
    int success;
} user_ext_audit_entry_t;

int  user_ext_audit_init(void);
int  user_ext_audit_log(uint32_t uid, const char *username, uint32_t event_type, const char *ip, int success, const char *detail);
int  user_ext_audit_get_entries(uint32_t uid, void *buf, int max_entries);
int  user_ext_audit_get_all(void *buf, int max_entries);
int  user_ext_audit_get_by_type(uint32_t event_type, void *buf, int max_entries);
int  user_ext_audit_clear(void);
void user_ext_audit_print_recent(int count);

/* ================================================================ */
/*  6) 双因素认证 (TOTP)                                             */
/* ================================================================ */

#define USER_EXT_TOTP_SECRET_LEN  32
#define USER_EXT_TOTP_CODE_LEN    6
#define USER_EXT_TOTP_TIME_STEP   30
#define USER_EXT_TOTP_WINDOW      2

typedef struct user_ext_totp {
    uint32_t uid;
    char secret[32];
    uint32_t secret_len;
    int enabled;
    uint32_t last_verified;
} user_ext_totp_t;

int  user_ext_totp_init(void);
int  user_ext_totp_setup(uint32_t uid, const char *secret_b32);
int  user_ext_totp_enable(uint32_t uid);
int  user_ext_totp_disable(uint32_t uid);
int  user_ext_totp_is_enabled(uint32_t uid);
int  user_ext_totp_verify(uint32_t uid, const char *code);
int  user_ext_totp_generate_code(uint32_t uid, char *code_out);
int  user_ext_totp_remove(uint32_t uid);

/* ================================================================ */
/*  7) 用户资源限制 (Resource Limits)                                 */
/* ================================================================ */

#define USER_EXT_RLIMIT_MAX_PROCESSES  256
#define USER_EXT_RLIMIT_MAX_FILES      1024
#define USER_EXT_RLIMIT_MAX_MEMORY_MB  4096

typedef struct user_ext_rlimit {
    uint32_t uid;
    uint32_t max_processes;
    uint32_t max_open_files;
    uint32_t max_memory_mb;
    uint32_t current_processes;
    uint32_t current_open_files;
    uint32_t current_memory_mb;
    int active;
} user_ext_rlimit_t;

int  user_ext_rlimit_init(void);
int  user_ext_rlimit_set(uint32_t uid, uint32_t max_proc, uint32_t max_files, uint32_t max_mem);
int  user_ext_rlimit_get(uint32_t uid, user_ext_rlimit_t *limits);
int  user_ext_rlimit_check_process(uint32_t uid);
int  user_ext_rlimit_check_file(uint32_t uid);
int  user_ext_rlimit_check_memory(uint32_t uid, uint32_t requested_mb);
int  user_ext_rlimit_inc_process(uint32_t uid);
int  user_ext_rlimit_dec_process(uint32_t uid);
int  user_ext_rlimit_inc_file(uint32_t uid);
int  user_ext_rlimit_dec_file(uint32_t uid);
void user_ext_rlimit_print(uint32_t uid);

/* ================================================================ */
/*  8) Sudo/特权提升 (Privilege Escalation)                           */
/* ================================================================ */

#define USER_EXT_SUDO_MAX_ENTRIES 256
#define USER_EXT_SUDO_TIMEOUT_SEC 300

typedef struct user_ext_sudo_entry {
    uint32_t entry_id;
    uint32_t uid;
    char username[64];
    int elevated;
    uint32_t elevated_at;
    uint32_t expires_at;
    uint32_t original_uid;
    int active;
} user_ext_sudo_entry_t;

int  user_ext_sudo_init(void);
int  user_ext_sudo_auth(const char *username, const char *password);
int  user_ext_sudo_elevate(const char *username);
int  user_ext_sudo_deelevate(uint32_t uid);
int  user_ext_sudo_is_elevated(uint32_t uid);
int  user_ext_sudo_check_password(const char *username, const char *password);
int  user_ext_sudo_revoke_all(void);
void user_ext_sudo_print_session(uint32_t uid);

#endif /* USER_EXT_H */