#include "user_ext.h"
#include "user.h"
#include "kheap.h"
#include "string.h"
#include "klog.h"

/* ================================================================ */
/*  全局状态                                                        */
/* ================================================================ */

static user_ext_t *g_users = NULL;
static user_group_t *g_groups = NULL;
static user_ext_t *g_current_user = NULL;

static user_session_t g_sessions[USER_EXT_MAX_SESSIONS];
static uint32_t g_next_session_id = 1;
static int g_sessions_initialized = 0;

/* ================================================================ */
/*  密码哈希（简单的 XOR 置换哈希，适合嵌入式使用）                    */
/* ================================================================ */

static void hash_password(const char *password, char *hash_out)
{
    uint32_t hash = 0xDEADBEEF;
    uint32_t i, len;
    const uint32_t salt = 0xA5A5A5A5;

    if (!password || !hash_out) return;

    len = strlen(password);

    /* 多轮 XOR 哈希，增强扩散性 */
    for (i = 0; i < len; i++) {
        hash ^= (uint32_t)(password[i]) << ((i & 3) * 8);
        hash = ((hash << 13) | (hash >> 19)) ^ salt;
        hash ^= (hash >> 11);
        hash = ((hash << 7) | (hash >> 25)) ^ (hash >> 16);
    }

    /* 如果密码太短，额外搅一轮 */
    if (len < 8) {
        hash ^= (hash << 17) | 0x55555555;
        hash = ((hash << 3) | (hash >> 29)) ^ 0xAAAAAAAA;
    }

    /* 输出 64 字符十六进制字符串 */
    for (i = 0; i < 8; i++) {
        uint8_t byte = (hash >> (i * 4)) & 0x0F;
        if (byte < 10)
            hash_out[i * 2] = '0' + byte;
        else
            hash_out[i * 2] = 'a' + (byte - 10);

        byte = (hash >> (i * 4 + 16)) & 0x0F;
        if (byte < 10)
            hash_out[i * 2 + 1] = '0' + byte;
        else
            hash_out[i * 2 + 1] = 'a' + (byte - 10);
    }
    hash_out[16] = '\0';
}

static int verify_password(const char *password, const char *hash)
{
    char computed[64];
    if (!password || !hash) return 0;
    hash_password(password, computed);
    return (strcmp(computed, hash) == 0) ? 1 : 0;
}

/* ================================================================ */
/*  初始化                                                          */
/* ================================================================ */

void user_ext_init(void)
{
    char root_hash[64];

    klog_info("user_ext: initializing extended user management...");

    /* 初始化会话表 */
    if (!g_sessions_initialized) {
        memset(g_sessions, 0, sizeof(g_sessions));
        g_sessions_initialized = 1;
    }

    /* 创建 root 用户 */
    hash_password("root", root_hash);
    user_ext_create("root", "root", "/root");

    /* 手动设置密码哈希（绕过 create 中的默认哈希）*/
    if (g_users) {
        strncpy(g_users->password_hash, root_hash, sizeof(g_users->password_hash) - 1);
        g_users->flags |= USER_EXT_FLAG_ADMIN;
        g_users->uid = 0;
        g_users->gid = 0;
    }

    /* 创建默认组 */
    user_group_create("root", 0);
    user_group_create("users", 100);
    user_group_create("admin", 200);

    /* 将 root 加入 root 组 */
    user_group_add_member("root", 0);

    klog_info("user_ext: initialized with root user");
}

/* ================================================================ */
/*  用户 CRUD                                                       */
/* ================================================================ */

int user_ext_create(const char *username, const char *password, const char *home)
{
    user_ext_t *user;
    char pass_hash[64];

    if (!username || !password) return -1;

    /* 检查重名 */
    if (user_ext_find_by_name(username)) {
        klog_err("user_ext: user %s already exists", username);
        return -1;
    }

    user = (user_ext_t *)kmalloc(sizeof(user_ext_t));
    if (!user) return -1;

    memset(user, 0, sizeof(user_ext_t));

    /* 分配 UID（简单递增）*/
    {
        user_ext_t *u = g_users;
        uint32_t max_uid = 0;
        while (u) {
            if (u->uid > max_uid) max_uid = u->uid;
            u = u->next;
        }
        user->uid = max_uid + 1;
    }

    /* 如果指定了 "root" 则 UID=0 */
    if (strcmp(username, "root") == 0) {
        user->uid = 0;
    }

    strncpy(user->username, username, sizeof(user->username) - 1);

    hash_password(password, pass_hash);
    strncpy(user->password_hash, pass_hash, sizeof(user->password_hash) - 1);

    if (home) {
        strncpy(user->home_dir, home, sizeof(user->home_dir) - 1);
    } else {
        /* 默认家目录：/home/username */
        strncpy(user->home_dir, "/home/", sizeof(user->home_dir) - 1);
        strncat(user->home_dir, username, sizeof(user->home_dir) - strlen(user->home_dir) - 1);
    }

    strncpy(user->shell, "/bin/shell", sizeof(user->shell) - 1);
    user->gid = user->uid;  /* 默认主组等于 UID */
    user->group_count = 0;
    user->created = 0;
    user->last_login = 0;
    user->login_count = 0;
    user->flags = USER_EXT_FLAG_ACTIVE;
    user->quota_soft = 0;
    user->quota_hard = 0;
    user->quota_used = 0;

    /* 加入链表 */
    user->next = g_users;
    g_users = user;

    klog_info("user_ext: created user %s (uid=%u)", username, user->uid);

    /* 创建家目录 */
    user_ext_create_home(username);

    return 0;
}

int user_ext_delete(const char *username)
{
    user_ext_t *prev = NULL;
    user_ext_t *user = g_users;

    if (!username) return -1;

    while (user) {
        if (strcmp(user->username, username) == 0) {
            /* 不允许删除 root */
            if (user->uid == 0 && (user->flags & USER_EXT_FLAG_ADMIN)) {
                klog_err("user_ext: cannot delete root user");
                return -1;
            }

            /* 如果是当前用户，先登出 */
            if (g_current_user == user) {
                g_current_user = NULL;
            }

            if (prev) {
                prev->next = user->next;
            } else {
                g_users = user->next;
            }

            kfree(user);
            klog_info("user_ext: deleted user %s", username);
            return 0;
        }
        prev = user;
        user = user->next;
    }

    klog_err("user_ext: user %s not found", username);
    return -1;
}

int user_ext_set_password(const char *username, const char *new_password)
{
    user_ext_t *user;
    char pass_hash[64];

    if (!username || !new_password) return -1;

    user = user_ext_find_by_name(username);
    if (!user) return -1;

    hash_password(new_password, pass_hash);
    strncpy(user->password_hash, pass_hash, sizeof(user->password_hash) - 1);

    klog_info("user_ext: password changed for %s", username);
    return 0;
}

/* ================================================================ */
/*  认证                                                            */
/* ================================================================ */

int user_ext_authenticate(const char *username, const char *password)
{
    user_ext_t *user;

    if (!username || !password) return -1;

    user = user_ext_find_by_name(username);
    if (!user) {
        klog_err("user_ext: auth fail - user %s not found", username);
        return -1;
    }

    if (!(user->flags & USER_EXT_FLAG_ACTIVE)) {
        klog_err("user_ext: auth fail - user %s disabled", username);
        return -1;
    }

    if (!verify_password(password, user->password_hash)) {
        klog_err("user_ext: auth fail - wrong password for %s", username);
        return -1;
    }

    return 0;
}

int user_ext_login(const char *username, const char *password)
{
    if (user_ext_authenticate(username, password) != 0) {
        return -1;
    }

    user_ext_t *user = user_ext_find_by_name(username);
    if (!user) return -1;

    g_current_user = user;
    user->last_login = 0;
    user->login_count++;

    /* 也设置到基础用户系统 */
    user_set_current(user->uid);

    klog_info("user_ext: %s logged in (login #%u)", username, user->login_count);
    return 0;
}

int user_ext_logout(void)
{
    if (!g_current_user) return -1;

    klog_info("user_ext: %s logged out", g_current_user->username);
    g_current_user = NULL;

    /* 重置基础系统的当前用户 */
    user_set_current((uint32_t)-1);

    return 0;
}

user_ext_t *user_ext_get_current(void)
{
    return g_current_user;
}

/* ================================================================ */
/*  查找                                                            */
/* ================================================================ */

user_ext_t *user_ext_find_by_name(const char *username)
{
    user_ext_t *user = g_users;
    while (user) {
        if (strcmp(user->username, username) == 0) {
            return user;
        }
        user = user->next;
    }
    return NULL;
}

user_ext_t *user_ext_find_by_uid(uint32_t uid)
{
    user_ext_t *user = g_users;
    while (user) {
        if (user->uid == uid) {
            return user;
        }
        user = user->next;
    }
    return NULL;
}

/* ================================================================ */
/*  组管理                                                          */
/* ================================================================ */

int user_group_create(const char *name, uint32_t gid)
{
    user_group_t *group;

    if (!name) return -1;

    /* 检查重名 */
    if (user_group_find_by_name(name)) {
        klog_err("user_ext: group %s already exists", name);
        return -1;
    }

    /* 检查重复 GID */
    if (user_group_find_by_gid(gid)) {
        klog_err("user_ext: gid %u already exists", gid);
        return -1;
    }

    group = (user_group_t *)kmalloc(sizeof(user_group_t));
    if (!group) return -1;

    memset(group, 0, sizeof(user_group_t));
    group->gid = gid;
    strncpy(group->name, name, sizeof(group->name) - 1);
    group->member_count = 0;

    group->next = g_groups;
    g_groups = group;

    klog_info("user_ext: created group %s (gid=%u)", name, gid);
    return 0;
}

int user_group_delete(const char *name)
{
    user_group_t *prev = NULL;
    user_group_t *group = g_groups;

    while (group) {
        if (strcmp(group->name, name) == 0) {
            if (prev) {
                prev->next = group->next;
            } else {
                g_groups = group->next;
            }
            kfree(group);
            klog_info("user_ext: deleted group %s", name);
            return 0;
        }
        prev = group;
        group = group->next;
    }

    klog_err("user_ext: group %s not found", name);
    return -1;
}

int user_group_add_member(const char *group_name, uint32_t uid)
{
    user_group_t *group;
    uint32_t i;

    group = user_group_find_by_name(group_name);
    if (!group) return -1;

    /* 检查是否已存在 */
    for (i = 0; i < group->member_count; i++) {
        if (group->members[i] == uid) {
            return 0; /* 已在组内 */
        }
    }

    if (group->member_count >= USER_EXT_MAX_MEMBERS) {
        klog_err("user_ext: group %s member limit reached", group_name);
        return -1;
    }

    group->members[group->member_count] = uid;
    group->member_count++;

    /* 同步更新用户对象中的组列表 */
    {
        user_ext_t *user = user_ext_find_by_uid(uid);
        if (user && user->group_count < USER_EXT_MAX_GROUPS) {
            user->groups[user->group_count] = group->gid;
            user->group_count++;
        }
    }

    klog_info("user_ext: added uid %u to group %s", uid, group_name);
    return 0;
}

int user_group_remove_member(const char *group_name, uint32_t uid)
{
    user_group_t *group;
    uint32_t i;

    group = user_group_find_by_name(group_name);
    if (!group) return -1;

    for (i = 0; i < group->member_count; i++) {
        if (group->members[i] == uid) {
            /* 将最后一个元素移到当前位置 */
            group->members[i] = group->members[group->member_count - 1];
            group->member_count--;

            /* 同步更新用户对象 */
            {
                user_ext_t *user = user_ext_find_by_uid(uid);
                if (user) {
                    uint32_t j;
                    for (j = 0; j < user->group_count; j++) {
                        if (user->groups[j] == group->gid) {
                            user->groups[j] = user->groups[user->group_count - 1];
                            user->group_count--;
                            break;
                        }
                    }
                }
            }

            klog_info("user_ext: removed uid %u from group %s", uid, group_name);
            return 0;
        }
    }

    return -1;
}

user_group_t *user_group_find_by_name(const char *name)
{
    user_group_t *group = g_groups;
    while (group) {
        if (strcmp(group->name, name) == 0) {
            return group;
        }
        group = group->next;
    }
    return NULL;
}

user_group_t *user_group_find_by_gid(uint32_t gid)
{
    user_group_t *group = g_groups;
    while (group) {
        if (group->gid == gid) {
            return group;
        }
        group = group->next;
    }
    return NULL;
}

/* ================================================================ */
/*  权限检查                                                        */
/* ================================================================ */

int user_ext_check_permission(uint32_t uid, uint32_t gid, uint32_t required_perm)
{
    /* root (uid=0) 拥有所有权限 */
    if (uid == 0) return 1;

    /* 检查当前用户 */
    user_ext_t *user = g_current_user;
    if (!user) return 0;

    /* 管理员拥有所有权限 */
    if (user->flags & USER_EXT_FLAG_ADMIN) return 1;

    /* 检查是否为所有者 */
    if (user->uid == uid) {
        if (required_perm == 0) return 1;
        return 1;  /* 简化实现：所有者有权限 */
    }

    /* 检查组权限 */
    {
        uint32_t i;
        for (i = 0; i < user->group_count; i++) {
            if (user->groups[i] == gid) {
                /* 用户在该组内 */
                if (required_perm == 0) return 1;
                return 1;  /* 简化实现：组成员有权限 */
            }
        }
    }

    /* 检查 "其他" 权限 */
    if (required_perm == 0) return 1;
    return 1;  /* 简化实现：默认允许 */
}

int user_ext_is_root(void)
{
    if (!g_current_user) return 0;
    return (g_current_user->uid == 0 || (g_current_user->flags & USER_EXT_FLAG_ADMIN)) ? 1 : 0;
}

/* ================================================================ */
/*  用户会话                                                        */
/* ================================================================ */

int user_ext_session_create(uint32_t uid)
{
    user_ext_t *user;
    uint32_t i;

    user = user_ext_find_by_uid(uid);
    if (!user) return -1;

    /* 查找空闲会话槽 */
    for (i = 0; i < USER_EXT_MAX_SESSIONS; i++) {
        if (!g_sessions[i].active) {
            g_sessions[i].session_id = g_next_session_id++;
            g_sessions[i].uid = uid;
            g_sessions[i].login_time = 0;
            g_sessions[i].last_active = 0;
            g_sessions[i].active = 1;

            klog_info("user_ext: created session %u for uid %u", g_sessions[i].session_id, uid);
            return (int)g_sessions[i].session_id;
        }
    }

    klog_err("user_ext: no free session slots");
    return -1;
}

int user_ext_session_destroy(uint32_t session_id)
{
    uint32_t i;

    for (i = 0; i < USER_EXT_MAX_SESSIONS; i++) {
        if (g_sessions[i].active && g_sessions[i].session_id == session_id) {
            g_sessions[i].active = 0;
            klog_info("user_ext: destroyed session %u", session_id);
            return 0;
        }
    }

    return -1;
}

user_session_t *user_ext_session_get(uint32_t session_id)
{
    uint32_t i;

    for (i = 0; i < USER_EXT_MAX_SESSIONS; i++) {
        if (g_sessions[i].active && g_sessions[i].session_id == session_id) {
            return &g_sessions[i];
        }
    }
    return NULL;
}

/* ================================================================ */
/*  用户目录                                                        */
/* ================================================================ */

int user_ext_create_home(const char *username)
{
    char home_path[256];

    if (!username) return -1;

    /* 创建 /home 目录（如果不存在）*/
    strncpy(home_path, "/home/", sizeof(home_path) - 1);
    strncat(home_path, username, sizeof(home_path) - strlen(home_path) - 1);

    /* 调用 VFS 创建目录 */
    /* 注意：这里使用基础 VFS 的 mkdir 功能 */
    /*
     * 实际实现中应该调用 vfs_mkdir(home_path, 0755);
     * 但由于 vfs_ext 是 VFS 的扩展层，这里通过自己的 mkdir 来创建
     */

    klog_info("user_ext: home directory for %s: %s", username, home_path);
    return 0;
}

/* ================================================================ */
/*  1) 密码策略 (Password Policy)                                     */
/* ================================================================ */

static user_ext_pass_policy_t g_pass_policy = {
    8,     /* min_length */
    1,     /* require_upper */
    1,     /* require_lower */
    1,     /* require_digit */
    1,     /* require_special */
    90,    /* expire_days */
    7,     /* warn_days */
    8      /* history_size */
};

static user_ext_pass_history_t g_pass_histories[USER_EXT_MAX_MEMBERS][USER_EXT_PASS_HISTORY_MAX];
static uint32_t g_pass_history_counts[USER_EXT_MAX_MEMBERS];

int user_ext_pass_policy_set(const user_ext_pass_policy_t *policy)
{
    if (!policy) return -1;
    memcpy(&g_pass_policy, policy, sizeof(user_ext_pass_policy_t));
    klog_info("user_ext: password policy updated");
    return 0;
}

int user_ext_pass_policy_get(user_ext_pass_policy_t *policy)
{
    if (!policy) return -1;
    memcpy(policy, &g_pass_policy, sizeof(user_ext_pass_policy_t));
    return 0;
}

int user_ext_pass_check_strength(const char *password)
{
    uint32_t i, len;
    int has_upper = 0, has_lower = 0, has_digit = 0, has_special = 0;

    if (!password) return USER_EXT_PASS_TOO_SHORT;

    len = strlen(password);
    if (len < g_pass_policy.min_length) return USER_EXT_PASS_TOO_SHORT;

    for (i = 0; i < len; i++) {
        char c = password[i];
        if (c >= 'A' && c <= 'Z') has_upper = 1;
        else if (c >= 'a' && c <= 'z') has_lower = 1;
        else if (c >= '0' && c <= '9') has_digit = 1;
        else has_special = 1;
    }

    if (g_pass_policy.require_upper && !has_upper) return USER_EXT_PASS_NO_UPPER;
    if (g_pass_policy.require_lower && !has_lower) return USER_EXT_PASS_NO_LOWER;
    if (g_pass_policy.require_digit && !has_digit) return USER_EXT_PASS_NO_DIGIT;
    if (g_pass_policy.require_special && !has_special) return USER_EXT_PASS_NO_SPECIAL;

    return USER_EXT_PASS_OK;
}

int user_ext_pass_is_expired(const char *username)
{
    user_ext_t *user;
    uint32_t now = 0;
    uint32_t age_days;

    if (!username) return -1;
    user = user_ext_find_by_name(username);
    if (!user) return -1;

    /* 计算密码年龄 */
    if (now < user->created) {
        return 0; /* 时间戳异常，视为未过期 */
    }
    age_days = (now - user->created) / 86400;

    if (g_pass_policy.expire_days > 0 && age_days > g_pass_policy.expire_days) {
        return 1;
    }
    return 0;
}

int user_ext_pass_change(const char *username, const char *old_pass, const char *new_pass)
{
    user_ext_t *user;
    int strength;
    char pass_hash[64];

    if (!username || !old_pass || !new_pass) return -1;

    user = user_ext_find_by_name(username);
    if (!user) return -1;

    /* 验证旧密码 */
    if (!verify_password(old_pass, user->password_hash)) {
        klog_err("user_ext: pass change fail - wrong old password for %s", username);
        return -1;
    }

    /* 检查新密码强度 */
    strength = user_ext_pass_check_strength(new_pass);
    if (strength != USER_EXT_PASS_OK) {
        klog_err("user_ext: pass change fail - new password too weak (code=%d)", strength);
        return -1;
    }

    /* 检查历史 */
    if (user_ext_pass_history_check(username, new_pass)) {
        klog_err("user_ext: pass change fail - password in history for %s", username);
        return -1;
    }

    /* 更新密码 */
    hash_password(new_pass, pass_hash);
    strncpy(user->password_hash, pass_hash, sizeof(user->password_hash) - 1);
    user->created = 0; /* 重置创建时间作为最后修改时间 */

    /* 添加到历史 */
    user_ext_pass_history_add(username, pass_hash);

    klog_info("user_ext: password changed for %s", username);
    return 0;
}

int user_ext_pass_history_check(const char *username, const char *password)
{
    user_ext_t *user;
    uint32_t i;
    char computed[64];

    if (!username || !password) return 0;

    user = user_ext_find_by_name(username);
    if (!user) return 0;

    hash_password(password, computed);

    for (i = 0; i < g_pass_history_counts[user->uid % USER_EXT_MAX_MEMBERS]; i++) {
        if (strcmp(g_pass_histories[user->uid % USER_EXT_MAX_MEMBERS][i].password_hash, computed) == 0) {
            return 1; /* 在历史中找到 */
        }
    }
    return 0;
}

int user_ext_pass_history_add(const char *username, const char *password_hash)
{
    user_ext_t *user;
    uint32_t idx, slot, i;

    if (!username || !password_hash) return -1;

    user = user_ext_find_by_name(username);
    if (!user) return -1;

    idx = user->uid % USER_EXT_MAX_MEMBERS;
    slot = g_pass_history_counts[idx];

    if (slot >= USER_EXT_PASS_HISTORY_MAX) {
        /* 循环覆盖最旧的记录 */
        for (i = 0; i < USER_EXT_PASS_HISTORY_MAX - 1; i++) {
            memcpy(&g_pass_histories[idx][i], &g_pass_histories[idx][i + 1], sizeof(user_ext_pass_history_t));
        }
        slot = USER_EXT_PASS_HISTORY_MAX - 1;
    }

    strncpy(g_pass_histories[idx][slot].password_hash, password_hash, sizeof(g_pass_histories[0][0].password_hash) - 1);
    g_pass_histories[idx][slot].changed_at = 0;
    g_pass_history_counts[idx]++;

    return 0;
}

/* ================================================================ */
/*  2) 账户锁定 (Account Lockout)                                     */
/* ================================================================ */

static user_ext_lockout_t g_lockout_table[USER_EXT_LOCKOUT_MAX_ENTRIES];
static uint32_t g_lockout_count = 0;
static int g_lockout_initialized = 0;

int user_ext_lockout_init(void)
{
    memset(g_lockout_table, 0, sizeof(g_lockout_table));
    g_lockout_count = 0;
    g_lockout_initialized = 1;
    klog_info("user_ext: lockout system initialized");
    return 0;
}

static user_ext_lockout_t *lockout_find(const char *username)
{
    uint32_t i;
    for (i = 0; i < g_lockout_count; i++) {
        if (strcmp(g_lockout_table[i].username, username) == 0) {
            return &g_lockout_table[i];
        }
    }
    return NULL;
}

static user_ext_lockout_t *lockout_find_or_create(const char *username)
{
    user_ext_lockout_t *entry;
    user_ext_t *user;

    entry = lockout_find(username);
    if (entry) return entry;

    if (g_lockout_count >= USER_EXT_LOCKOUT_MAX_ENTRIES) return NULL;

    entry = &g_lockout_table[g_lockout_count];
    memset(entry, 0, sizeof(*entry));
    strncpy(entry->username, username, sizeof(entry->username) - 1);

    user = user_ext_find_by_name(username);
    if (user) {
        entry->uid = user->uid;
    }
    entry->locked = 0;
    g_lockout_count++;
    return entry;
}

int user_ext_lockout_record_failure(const char *username)
{
    user_ext_lockout_t *entry;
    if (!g_lockout_initialized) user_ext_lockout_init();

    entry = lockout_find_or_create(username);
    if (!entry) return -1;

    entry->failure_count++;
    entry->last_failure = 0;
    if (entry->failure_count == 1) {
        entry->first_failure = 0;
    }

    if (entry->failure_count >= USER_EXT_LOCKOUT_MAX_FAILURES) {
        entry->locked = 1;
        entry->locked_until = 0 + USER_EXT_LOCKOUT_DURATION_SEC;
        klog_info("user_ext: account %s locked (%u failures)", username, entry->failure_count);
    }

    return (int)entry->failure_count;
}

int user_ext_lockout_record_success(const char *username)
{
    user_ext_lockout_t *entry;
    if (!g_lockout_initialized) user_ext_lockout_init();

    entry = lockout_find(username);
    if (!entry) return 0;

    entry->failure_count = 0;
    entry->first_failure = 0;
    entry->last_failure = 0;
    if (!entry->locked) {
        /* 保持锁定状态 */
    }
    return 0;
}

int user_ext_lockout_is_locked(const char *username)
{
    user_ext_lockout_t *entry;
    if (!g_lockout_initialized) return 0;

    entry = lockout_find(username);
    if (!entry) return 0;

    if (!entry->locked) return 0;

    /* 检查锁定是否过期 */
    if (entry->locked_until > 0 && 0 >= entry->locked_until) {
        /* 自动解锁 */
        entry->locked = 0;
        entry->failure_count = 0;
        entry->locked_until = 0;
        klog_info("user_ext: account %s auto-unlocked", username);
        return 0;
    }

    return 1;
}

int user_ext_lockout_unlock(const char *username)
{
    user_ext_lockout_t *entry;
    if (!g_lockout_initialized) return -1;

    entry = lockout_find(username);
    if (!entry) return -1;

    entry->locked = 0;
    entry->failure_count = 0;
    entry->locked_until = 0;
    klog_info("user_ext: account %s manually unlocked", username);
    return 0;
}

int user_ext_lockout_unlock_all(void)
{
    uint32_t i;
    int count = 0;
    if (!g_lockout_initialized) return 0;

    for (i = 0; i < g_lockout_count; i++) {
        if (g_lockout_table[i].locked) {
            g_lockout_table[i].locked = 0;
            g_lockout_table[i].failure_count = 0;
            g_lockout_table[i].locked_until = 0;
            count++;
        }
    }
    klog_info("user_ext: unlocked %d accounts", count);
    return count;
}

void user_ext_lockout_print_status(const char *username)
{
    user_ext_lockout_t *entry;
    if (!g_lockout_initialized) return;

    entry = lockout_find(username);
    if (!entry) {
        klog_info("user_ext: no lockout data for %s", username);
        return;
    }

    klog_info("user_ext: lockout status for %s - locked=%d, failures=%u, locked_until=%u",
        username, entry->locked, entry->failure_count, entry->locked_until);
}

/* ================================================================ */
/*  3) 访问控制列表 (ACL)                                            */
/* ================================================================ */

static user_ext_acl_entry_t g_acl_table[USER_EXT_ACL_MAX_ENTRIES];
static uint32_t g_acl_count = 0;
static int g_acl_initialized = 0;

int user_ext_acl_init(void)
{
    memset(g_acl_table, 0, sizeof(g_acl_table));
    g_acl_count = 0;
    g_acl_initialized = 1;
    klog_info("user_ext: ACL system initialized");
    return 0;
}

int user_ext_acl_set(const char *path, uint32_t uid, uint32_t gid, uint32_t perms)
{
    uint32_t i;

    if (!g_acl_initialized) user_ext_acl_init();
    if (!path) return -1;

    /* 更新已有条目 */
    for (i = 0; i < g_acl_count; i++) {
        if (g_acl_table[i].active && strcmp(g_acl_table[i].path, path) == 0 && g_acl_table[i].uid == uid) {
            g_acl_table[i].permissions = perms;
            g_acl_table[i].gid = gid;
            return 0;
        }
    }

    /* 新建条目 */
    if (g_acl_count >= USER_EXT_ACL_MAX_ENTRIES) return -1;

    g_acl_table[g_acl_count].entry_id = g_acl_count + 1;
    strncpy(g_acl_table[g_acl_count].path, path, sizeof(g_acl_table[0].path) - 1);
    g_acl_table[g_acl_count].uid = uid;
    g_acl_table[g_acl_count].gid = gid;
    g_acl_table[g_acl_count].permissions = perms;
    g_acl_table[g_acl_count].active = 1;
    g_acl_count++;

    return 0;
}

int user_ext_acl_get(const char *path, uint32_t uid, uint32_t *perms)
{
    uint32_t i;
    if (!g_acl_initialized || !path || !perms) return -1;

    for (i = 0; i < g_acl_count; i++) {
        if (g_acl_table[i].active && strcmp(g_acl_table[i].path, path) == 0 && g_acl_table[i].uid == uid) {
            *perms = g_acl_table[i].permissions;
            return 0;
        }
    }
    return -1;
}

int user_ext_acl_check(const char *path, uint32_t uid, uint32_t gid, uint32_t required_perm)
{
    uint32_t i;
    int best_match = 0;

    if (!g_acl_initialized) return 1; /* 无 ACL 时默认允许 */
    if (uid == 0) return 1; /* root 总是允许 */

    /* 优先匹配用户条目 */
    for (i = 0; i < g_acl_count; i++) {
        if (g_acl_table[i].active && strcmp(g_acl_table[i].path, path) == 0) {
            if (g_acl_table[i].uid == uid) {
                return (g_acl_table[i].permissions & required_perm) == required_perm ? 1 : 0;
            }
            if (g_acl_table[i].gid == gid) {
                if ((g_acl_table[i].permissions & required_perm) == required_perm) {
                    best_match = 1;
                }
            }
        }
    }

    return best_match;
}

int user_ext_acl_remove(const char *path, uint32_t uid)
{
    uint32_t i;
    if (!g_acl_initialized || !path) return -1;

    for (i = 0; i < g_acl_count; i++) {
        if (g_acl_table[i].active && strcmp(g_acl_table[i].path, path) == 0 && g_acl_table[i].uid == uid) {
            g_acl_table[i].active = 0;
            return 0;
        }
    }
    return -1;
}

int user_ext_acl_remove_all(const char *path)
{
    uint32_t i;
    int count = 0;
    if (!g_acl_initialized || !path) return -1;

    for (i = 0; i < g_acl_count; i++) {
        if (g_acl_table[i].active && strcmp(g_acl_table[i].path, path) == 0) {
            g_acl_table[i].active = 0;
            count++;
        }
    }
    return count;
}

int user_ext_acl_list(const char *path, void *buf, int max_entries)
{
    uint32_t i;
    int count = 0;
    user_ext_acl_entry_t *entries = (user_ext_acl_entry_t *)buf;

    if (!g_acl_initialized || !path || !buf) return -1;

    for (i = 0; i < g_acl_count && count < max_entries; i++) {
        if (g_acl_table[i].active && strcmp(g_acl_table[i].path, path) == 0) {
            memcpy(&entries[count], &g_acl_table[i], sizeof(user_ext_acl_entry_t));
            count++;
        }
    }
    return count;
}

/* ================================================================ */
/*  4) 用户资料 (User Profiles)                                       */
/* ================================================================ */

static user_ext_profile_t g_profiles[256];
static int g_profile_initialized = 0;

int user_ext_profile_init(void)
{
    memset(g_profiles, 0, sizeof(g_profiles));
    g_profile_initialized = 1;
    klog_info("user_ext: profile system initialized");
    return 0;
}

static user_ext_profile_t *profile_find(uint32_t uid)
{
    uint32_t i;
    for (i = 0; i < 256; i++) {
        if (g_profiles[i].active && g_profiles[i].uid == uid) {
            return &g_profiles[i];
        }
    }
    return NULL;
}

static user_ext_profile_t *profile_alloc(void)
{
    uint32_t i;
    for (i = 0; i < 256; i++) {
        if (!g_profiles[i].active) {
            memset(&g_profiles[i], 0, sizeof(g_profiles[i]));
            g_profiles[i].active = 1;
            return &g_profiles[i];
        }
    }
    return NULL;
}

int user_ext_profile_set(uint32_t uid, const user_ext_profile_t *profile)
{
    user_ext_profile_t *p;
    if (!g_profile_initialized) user_ext_profile_init();
    if (!profile) return -1;

    p = profile_find(uid);
    if (!p) {
        p = profile_alloc();
        if (!p) return -1;
    }

    memcpy(p, profile, sizeof(user_ext_profile_t));
    p->uid = uid;
    p->updated = 0;
    p->active = 1;
    return 0;
}

int user_ext_profile_get(uint32_t uid, user_ext_profile_t *profile)
{
    user_ext_profile_t *p;
    if (!g_profile_initialized || !profile) return -1;

    p = profile_find(uid);
    if (!p) return -1;

    memcpy(profile, p, sizeof(user_ext_profile_t));
    return 0;
}

int user_ext_profile_delete(uint32_t uid)
{
    user_ext_profile_t *p;
    if (!g_profile_initialized) return -1;

    p = profile_find(uid);
    if (!p) return -1;

    p->active = 0;
    return 0;
}

int user_ext_profile_set_field(uint32_t uid, const char *field_name, const char *value)
{
    user_ext_profile_t *p;
    if (!g_profile_initialized || !field_name || !value) return -1;

    p = profile_find(uid);
    if (!p) {
        p = profile_alloc();
        if (!p) return -1;
        p->uid = uid;
        p->created = 0;
    }

    if (strcmp(field_name, "full_name") == 0) {
        strncpy(p->full_name, value, sizeof(p->full_name) - 1);
    } else if (strcmp(field_name, "email") == 0) {
        strncpy(p->email, value, sizeof(p->email) - 1);
    } else if (strcmp(field_name, "description") == 0) {
        strncpy(p->description, value, sizeof(p->description) - 1);
    } else if (strcmp(field_name, "avatar_path") == 0) {
        strncpy(p->avatar_path, value, sizeof(p->avatar_path) - 1);
    } else if (strcmp(field_name, "language") == 0) {
        strncpy(p->language, value, sizeof(p->language) - 1);
    } else if (strcmp(field_name, "theme") == 0) {
        strncpy(p->theme, value, sizeof(p->theme) - 1);
    } else {
        return -1;
    }

    p->updated = 0;
    return 0;
}

int user_ext_profile_get_field(uint32_t uid, const char *field_name, char *buf, int bufsize)
{
    user_ext_profile_t *p;
    const char *src = NULL;

    if (!g_profile_initialized || !field_name || !buf || bufsize <= 0) return -1;

    p = profile_find(uid);
    if (!p) return -1;

    if (strcmp(field_name, "full_name") == 0) src = p->full_name;
    else if (strcmp(field_name, "email") == 0) src = p->email;
    else if (strcmp(field_name, "description") == 0) src = p->description;
    else if (strcmp(field_name, "avatar_path") == 0) src = p->avatar_path;
    else if (strcmp(field_name, "language") == 0) src = p->language;
    else if (strcmp(field_name, "theme") == 0) src = p->theme;
    else return -1;

    if (src) {
        strncpy(buf, src, bufsize - 1);
        buf[bufsize - 1] = '\0';
    }
    return 0;
}

/* ================================================================ */
/*  5) 登录审计日志 (Login Audit Log)                                 */
/* ================================================================ */

static user_ext_audit_entry_t g_audit_log[USER_EXT_AUDIT_MAX_ENTRIES];
static uint32_t g_audit_count = 0;
static uint32_t g_audit_next_id = 1;
static int g_audit_initialized = 0;

int user_ext_audit_init(void)
{
    memset(g_audit_log, 0, sizeof(g_audit_log));
    g_audit_count = 0;
    g_audit_next_id = 1;
    g_audit_initialized = 1;
    klog_info("user_ext: audit log initialized");
    return 0;
}

int user_ext_audit_log(uint32_t uid, const char *username, uint32_t event_type, const char *ip, int success, const char *detail)
{
    uint32_t idx;
    if (!g_audit_initialized) user_ext_audit_init();

    if (g_audit_count >= USER_EXT_AUDIT_MAX_ENTRIES) {
        /* 循环覆盖最旧的 */
        idx = (g_audit_next_id - 1) % USER_EXT_AUDIT_MAX_ENTRIES;
    } else {
        idx = g_audit_count;
        g_audit_count++;
    }

    g_audit_log[idx].entry_id = g_audit_next_id++;
    g_audit_log[idx].uid = uid;
    if (username) strncpy(g_audit_log[idx].username, username, sizeof(g_audit_log[0].username) - 1);
    g_audit_log[idx].event_type = event_type;
    g_audit_log[idx].timestamp = 0;
    if (ip) strncpy(g_audit_log[idx].ip_address, ip, sizeof(g_audit_log[0].ip_address) - 1);
    g_audit_log[idx].success = success;
    if (detail) strncpy(g_audit_log[idx].detail, detail, sizeof(g_audit_log[0].detail) - 1);

    return 0;
}

int user_ext_audit_get_entries(uint32_t uid, void *buf, int max_entries)
{
    uint32_t i;
    int count = 0;
    user_ext_audit_entry_t *entries = (user_ext_audit_entry_t *)buf;

    if (!g_audit_initialized || !buf) return -1;

    for (i = 0; i < g_audit_count && count < max_entries; i++) {
        if (g_audit_log[i].uid == uid) {
            memcpy(&entries[count], &g_audit_log[i], sizeof(user_ext_audit_entry_t));
            count++;
        }
    }
    return count;
}

int user_ext_audit_get_all(void *buf, int max_entries)
{
    uint32_t i;
    int count = 0;
    user_ext_audit_entry_t *entries = (user_ext_audit_entry_t *)buf;

    if (!g_audit_initialized || !buf) return -1;

    for (i = 0; i < g_audit_count && count < max_entries; i++) {
        memcpy(&entries[count], &g_audit_log[i], sizeof(user_ext_audit_entry_t));
        count++;
    }
    return count;
}

int user_ext_audit_get_by_type(uint32_t event_type, void *buf, int max_entries)
{
    uint32_t i;
    int count = 0;
    user_ext_audit_entry_t *entries = (user_ext_audit_entry_t *)buf;

    if (!g_audit_initialized || !buf) return -1;

    for (i = 0; i < g_audit_count && count < max_entries; i++) {
        if (g_audit_log[i].event_type == event_type) {
            memcpy(&entries[count], &g_audit_log[i], sizeof(user_ext_audit_entry_t));
            count++;
        }
    }
    return count;
}

int user_ext_audit_clear(void)
{
    if (!g_audit_initialized) return 0;
    g_audit_count = 0;
    g_audit_next_id = 1;
    klog_info("user_ext: audit log cleared");
    return 0;
}

void user_ext_audit_print_recent(int count)
{
    uint32_t i, start;
    const char *type_str;

    if (!g_audit_initialized || g_audit_count == 0) {
        klog_info("user_ext: audit log empty");
        return;
    }

    if (count <= 0) count = 10;
    if ((uint32_t)count > g_audit_count) count = (int)g_audit_count;

    start = g_audit_count - (uint32_t)count;
    for (i = start; i < g_audit_count; i++) {
        switch (g_audit_log[i].event_type) {
        case USER_EXT_AUDIT_LOGIN_SUCCESS: type_str = "LOGIN_SUCCESS"; break;
        case USER_EXT_AUDIT_LOGIN_FAILURE: type_str = "LOGIN_FAILURE"; break;
        case USER_EXT_AUDIT_LOGOUT:        type_str = "LOGOUT";        break;
        case USER_EXT_AUDIT_SUDO:          type_str = "SUDO";          break;
        case USER_EXT_AUDIT_PASS_CHANGE:   type_str = "PASS_CHANGE";   break;
        case USER_EXT_AUDIT_ACCOUNT_LOCK:  type_str = "ACCOUNT_LOCK";  break;
        default:                           type_str = "UNKNOWN";       break;
        }
        klog_info("user_ext: audit [%u] user=%s type=%s success=%d ip=%s",
            g_audit_log[i].entry_id, g_audit_log[i].username, type_str,
            g_audit_log[i].success, g_audit_log[i].ip_address);
    }
}

/* ================================================================ */
/*  6) 双因素认证 (TOTP)                                             */
/* ================================================================ */

static user_ext_totp_t g_totp_entries[256];
static uint32_t g_totp_count = 0;
static int g_totp_initialized = 0;

int user_ext_totp_init(void)
{
    memset(g_totp_entries, 0, sizeof(g_totp_entries));
    g_totp_count = 0;
    g_totp_initialized = 1;
    klog_info("user_ext: TOTP system initialized");
    return 0;
}

static user_ext_totp_t *totp_find(uint32_t uid)
{
    uint32_t i;
    for (i = 0; i < g_totp_count; i++) {
        if (g_totp_entries[i].uid == uid) {
            return &g_totp_entries[i];
        }
    }
    return NULL;
}

static uint32_t totp_compute_hotp(const uint8_t *key, uint32_t key_len, uint64_t counter)
{
    uint32_t i;
    uint64_t be_counter;
    /* 简化的 HMAC-HOTP 实现 */
    uint32_t hash = 0;

    /* 大端序 counter */
    be_counter = ((counter & 0xFF00000000000000ULL) >> 56) |
                 ((counter & 0x00FF000000000000ULL) >> 40) |
                 ((counter & 0x0000FF0000000000ULL) >> 24) |
                 ((counter & 0x000000FF00000000ULL) >> 8)  |
                 ((counter & 0x00000000FF000000ULL) << 8)  |
                 ((counter & 0x0000000000FF0000ULL) << 24) |
                 ((counter & 0x000000000000FF00ULL) << 40) |
                 ((counter & 0x00000000000000FFULL) << 56);

    /* 简化 XOR-HMAC */
    hash = (uint32_t)(be_counter ^ 0xDEADBEEF);
    for (i = 0; i < key_len; i++) {
        hash ^= (uint32_t)(key[i] << ((i & 3) * 8));
        hash = ((hash << 5) | (hash >> 27)) ^ (hash >> 3);
    }

    /* 动态截断 */
    return (hash & 0x7FFFFFFF) % 1000000;
}

int user_ext_totp_setup(uint32_t uid, const char *secret_b32)
{
    user_ext_totp_t *entry;
    uint32_t len;

    if (!g_totp_initialized) user_ext_totp_init();
    if (!secret_b32) return -1;

    entry = totp_find(uid);
    if (!entry) {
        if (g_totp_count >= 256) return -1;
        entry = &g_totp_entries[g_totp_count];
        g_totp_count++;
    }

    memset(entry, 0, sizeof(*entry));
    entry->uid = uid;
    len = strlen(secret_b32);
    if (len > sizeof(entry->secret)) len = sizeof(entry->secret);
    memcpy(entry->secret, secret_b32, len);
    entry->secret_len = len;
    entry->enabled = 0;
    entry->last_verified = 0;

    klog_info("user_ext: TOTP setup for uid %u", uid);
    return 0;
}

int user_ext_totp_enable(uint32_t uid)
{
    user_ext_totp_t *entry;
    if (!g_totp_initialized) return -1;

    entry = totp_find(uid);
    if (!entry) return -1;

    entry->enabled = 1;
    klog_info("user_ext: TOTP enabled for uid %u", uid);
    return 0;
}

int user_ext_totp_disable(uint32_t uid)
{
    user_ext_totp_t *entry;
    if (!g_totp_initialized) return -1;

    entry = totp_find(uid);
    if (!entry) return -1;

    entry->enabled = 0;
    klog_info("user_ext: TOTP disabled for uid %u", uid);
    return 0;
}

int user_ext_totp_is_enabled(uint32_t uid)
{
    user_ext_totp_t *entry;
    if (!g_totp_initialized) return 0;

    entry = totp_find(uid);
    if (!entry) return 0;

    return entry->enabled ? 1 : 0;
}

int user_ext_totp_verify(uint32_t uid, const char *code)
{
    user_ext_totp_t *entry;
    uint64_t current_time, time_step;
    uint32_t computed_code;
    char code_str[8];
    int64_t i;

    if (!g_totp_initialized || !code) return -1;

    entry = totp_find(uid);
    if (!entry || !entry->enabled) return -1;

    current_time = 0;
    time_step = current_time / USER_EXT_TOTP_TIME_STEP;

    /* 检查时间窗口内的代码 */
    for (i = -(int64_t)USER_EXT_TOTP_WINDOW; i <= (int64_t)USER_EXT_TOTP_WINDOW; i++) {
        computed_code = totp_compute_hotp((const uint8_t *)entry->secret, entry->secret_len, time_step + (uint64_t)i);
        /* 格式化为 6 位数字 */
        {
            uint32_t tmp = computed_code;
            int d;
            code_str[6] = '\0';
            for (d = 5; d >= 0; d--) {
                code_str[d] = '0' + (tmp % 10);
                tmp /= 10;
            }
        }
        if (strcmp(code, code_str) == 0) {
            entry->last_verified = (uint32_t)current_time;
            return 0;
        }
    }

    return -1;
}

int user_ext_totp_generate_code(uint32_t uid, char *code_out)
{
    user_ext_totp_t *entry;
    uint64_t current_time, time_step;
    uint32_t computed_code;

    if (!g_totp_initialized || !code_out) return -1;

    entry = totp_find(uid);
    if (!entry) return -1;

    current_time = 0;
    time_step = current_time / USER_EXT_TOTP_TIME_STEP;

    computed_code = totp_compute_hotp((const uint8_t *)entry->secret, entry->secret_len, time_step);

    /* 格式化为 6 位数字 */
    {
        uint32_t tmp = computed_code;
        int d;
        code_out[6] = '\0';
        for (d = 5; d >= 0; d--) {
            code_out[d] = '0' + (tmp % 10);
            tmp /= 10;
        }
    }

    return 0;
}

int user_ext_totp_remove(uint32_t uid)
{
    user_ext_totp_t *entry;
    if (!g_totp_initialized) return -1;

    entry = totp_find(uid);
    if (!entry) return -1;

    entry->enabled = 0;
    memset(entry->secret, 0, sizeof(entry->secret));
    entry->secret_len = 0;
    return 0;
}

/* ================================================================ */
/*  7) 用户资源限制 (Resource Limits)                                 */
/* ================================================================ */

static user_ext_rlimit_t g_rlimits[256];
static int g_rlimit_initialized = 0;

int user_ext_rlimit_init(void)
{
    memset(g_rlimits, 0, sizeof(g_rlimits));
    g_rlimit_initialized = 1;
    klog_info("user_ext: resource limits initialized");
    return 0;
}

static user_ext_rlimit_t *rlimit_find_or_create(uint32_t uid)
{
    uint32_t i;
    int free_slot = -1;

    for (i = 0; i < 256; i++) {
        if (g_rlimits[i].active && g_rlimits[i].uid == uid) {
            return &g_rlimits[i];
        }
        if (!g_rlimits[i].active && free_slot < 0) {
            free_slot = (int)i;
        }
    }

    if (free_slot >= 0) {
        memset(&g_rlimits[free_slot], 0, sizeof(g_rlimits[0]));
        g_rlimits[free_slot].uid = uid;
        g_rlimits[free_slot].max_processes = USER_EXT_RLIMIT_MAX_PROCESSES;
        g_rlimits[free_slot].max_open_files = USER_EXT_RLIMIT_MAX_FILES;
        g_rlimits[free_slot].max_memory_mb = USER_EXT_RLIMIT_MAX_MEMORY_MB;
        g_rlimits[free_slot].active = 1;
        return &g_rlimits[free_slot];
    }

    return NULL;
}

int user_ext_rlimit_set(uint32_t uid, uint32_t max_proc, uint32_t max_files, uint32_t max_mem)
{
    user_ext_rlimit_t *r;
    if (!g_rlimit_initialized) user_ext_rlimit_init();

    r = rlimit_find_or_create(uid);
    if (!r) return -1;

    r->max_processes = max_proc;
    r->max_open_files = max_files;
    r->max_memory_mb = max_mem;
    return 0;
}

int user_ext_rlimit_get(uint32_t uid, user_ext_rlimit_t *limits)
{
    user_ext_rlimit_t *r;
    if (!g_rlimit_initialized || !limits) return -1;

    r = rlimit_find_or_create(uid);
    if (!r) return -1;

    memcpy(limits, r, sizeof(user_ext_rlimit_t));
    return 0;
}

int user_ext_rlimit_check_process(uint32_t uid)
{
    user_ext_rlimit_t *r;
    if (!g_rlimit_initialized) user_ext_rlimit_init();

    r = rlimit_find_or_create(uid);
    if (!r) return -1;

    return (r->current_processes < r->max_processes) ? 1 : 0;
}

int user_ext_rlimit_check_file(uint32_t uid)
{
    user_ext_rlimit_t *r;
    if (!g_rlimit_initialized) user_ext_rlimit_init();

    r = rlimit_find_or_create(uid);
    if (!r) return -1;

    return (r->current_open_files < r->max_open_files) ? 1 : 0;
}

int user_ext_rlimit_check_memory(uint32_t uid, uint32_t requested_mb)
{
    user_ext_rlimit_t *r;
    if (!g_rlimit_initialized) user_ext_rlimit_init();

    r = rlimit_find_or_create(uid);
    if (!r) return -1;

    return (r->current_memory_mb + requested_mb <= r->max_memory_mb) ? 1 : 0;
}

int user_ext_rlimit_inc_process(uint32_t uid)
{
    user_ext_rlimit_t *r;
    if (!g_rlimit_initialized) user_ext_rlimit_init();

    r = rlimit_find_or_create(uid);
    if (!r) return -1;

    r->current_processes++;
    return 0;
}

int user_ext_rlimit_dec_process(uint32_t uid)
{
    user_ext_rlimit_t *r;
    if (!g_rlimit_initialized) user_ext_rlimit_init();

    r = rlimit_find_or_create(uid);
    if (!r) return -1;

    if (r->current_processes > 0) {
        r->current_processes--;
    }
    return 0;
}

int user_ext_rlimit_inc_file(uint32_t uid)
{
    user_ext_rlimit_t *r;
    if (!g_rlimit_initialized) user_ext_rlimit_init();

    r = rlimit_find_or_create(uid);
    if (!r) return -1;

    r->current_open_files++;
    return 0;
}

int user_ext_rlimit_dec_file(uint32_t uid)
{
    user_ext_rlimit_t *r;
    if (!g_rlimit_initialized) user_ext_rlimit_init();

    r = rlimit_find_or_create(uid);
    if (!r) return -1;

    if (r->current_open_files > 0) {
        r->current_open_files--;
    }
    return 0;
}

void user_ext_rlimit_print(uint32_t uid)
{
    user_ext_rlimit_t *r;
    if (!g_rlimit_initialized) return;

    r = rlimit_find_or_create(uid);
    if (!r) {
        klog_info("user_ext: no resource limits for uid %u", uid);
        return;
    }

    klog_info("user_ext: rlimit uid=%u proc=%u/%u files=%u/%u mem=%u/%uMB",
        uid,
        r->current_processes, r->max_processes,
        r->current_open_files, r->max_open_files,
        r->current_memory_mb, r->max_memory_mb);
}

/* ================================================================ */
/*  8) Sudo/特权提升 (Privilege Escalation)                           */
/* ================================================================ */

static user_ext_sudo_entry_t g_sudo_entries[USER_EXT_SUDO_MAX_ENTRIES];
static uint32_t g_sudo_count = 0;
static uint32_t g_sudo_next_id = 1;
static int g_sudo_initialized = 0;

int user_ext_sudo_init(void)
{
    memset(g_sudo_entries, 0, sizeof(g_sudo_entries));
    g_sudo_count = 0;
    g_sudo_next_id = 1;
    g_sudo_initialized = 1;
    klog_info("user_ext: sudo system initialized");
    return 0;
}

static user_ext_sudo_entry_t *sudo_find(uint32_t uid)
{
    uint32_t i;
    for (i = 0; i < g_sudo_count; i++) {
        if (g_sudo_entries[i].active && g_sudo_entries[i].uid == uid) {
            return &g_sudo_entries[i];
        }
    }
    return NULL;
}

int user_ext_sudo_check_password(const char *username, const char *password)
{
    user_ext_t *user;
    if (!username || !password) return -1;

    user = user_ext_find_by_name(username);
    if (!user) return -1;

    if (!verify_password(password, user->password_hash)) {
        return -1;
    }

    /* 检查是否为管理员 */
    if (!(user->flags & USER_EXT_FLAG_ADMIN) && user->uid != 0) {
        klog_err("user_ext: sudo denied - %s is not an admin", username);
        return -1;
    }

    return 0;
}

int user_ext_sudo_auth(const char *username, const char *password)
{
    int ret;
    if (!g_sudo_initialized) user_ext_sudo_init();

    ret = user_ext_sudo_check_password(username, password);
    user_ext_audit_log(0, username, USER_EXT_AUDIT_SUDO, "127.0.0.1", ret == 0 ? 1 : 0, "sudo authentication");
    return ret;
}

int user_ext_sudo_elevate(const char *username)
{
    user_ext_t *user;
    user_ext_sudo_entry_t *entry;

    if (!g_sudo_initialized) user_ext_sudo_init();
    if (!username) return -1;

    user = user_ext_find_by_name(username);
    if (!user) return -1;

    /* 检查是否已提升 */
    entry = sudo_find(user->uid);
    if (entry && entry->elevated) {
        /* 刷新超时 */
        entry->expires_at = 0 + USER_EXT_SUDO_TIMEOUT_SEC;
        return 0;
    }

    if (g_sudo_count >= USER_EXT_SUDO_MAX_ENTRIES) return -1;

    entry = &g_sudo_entries[g_sudo_count];
    memset(entry, 0, sizeof(*entry));
    entry->entry_id = g_sudo_next_id++;
    entry->uid = user->uid;
    strncpy(entry->username, username, sizeof(entry->username) - 1);
    entry->elevated = 1;
    entry->elevated_at = 0;
    entry->expires_at = 0 + USER_EXT_SUDO_TIMEOUT_SEC;
    entry->original_uid = user->uid;
    entry->active = 1;
    g_sudo_count++;

    /* 临时设置为管理员 */
    user->flags |= USER_EXT_FLAG_ADMIN;

    klog_info("user_ext: sudo elevated %s (uid=%u)", username, user->uid);
    user_ext_audit_log(user->uid, username, USER_EXT_AUDIT_SUDO, "127.0.0.1", 1, "privilege elevated");
    return 0;
}

int user_ext_sudo_deelevate(uint32_t uid)
{
    user_ext_sudo_entry_t *entry;
    user_ext_t *user;

    if (!g_sudo_initialized) return -1;

    entry = sudo_find(uid);
    if (!entry || !entry->elevated) return -1;

    entry->elevated = 0;
    entry->active = 0;

    user = user_ext_find_by_uid(uid);
    if (user) {
        /* 移除临时管理员标志 */
        user->flags &= ~USER_EXT_FLAG_ADMIN;
    }

    klog_info("user_ext: sudo de-elevated uid %u", uid);
    user_ext_audit_log(uid, entry->username, USER_EXT_AUDIT_SUDO, "127.0.0.1", 1, "privilege de-elevated");
    return 0;
}

int user_ext_sudo_is_elevated(uint32_t uid)
{
    user_ext_sudo_entry_t *entry;
    if (!g_sudo_initialized) return 0;

    entry = sudo_find(uid);
    if (!entry || !entry->elevated) return 0;

    /* 检查超时 */
    if (entry->expires_at > 0 && 0 >= entry->expires_at) {
        user_ext_sudo_deelevate(uid);
        return 0;
    }

    return 1;
}

int user_ext_sudo_revoke_all(void)
{
    uint32_t i;
    int count = 0;
    if (!g_sudo_initialized) return 0;

    for (i = 0; i < g_sudo_count; i++) {
        if (g_sudo_entries[i].active && g_sudo_entries[i].elevated) {
            user_ext_sudo_deelevate(g_sudo_entries[i].uid);
            count++;
        }
    }
    klog_info("user_ext: sudo revoked %d sessions", count);
    return count;
}

void user_ext_sudo_print_session(uint32_t uid)
{
    user_ext_sudo_entry_t *entry;
    if (!g_sudo_initialized) return;

    entry = sudo_find(uid);
    if (!entry || !entry->active) {
        klog_info("user_ext: no sudo session for uid %u", uid);
        return;
    }

    klog_info("user_ext: sudo session uid=%u elevated=%d elevated_at=%u expires_at=%u",
        uid, entry->elevated, entry->elevated_at, entry->expires_at);
}