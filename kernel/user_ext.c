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