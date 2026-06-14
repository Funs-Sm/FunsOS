#include "user.h"
#include "kheap.h"
#include "string.h"

static user_t users[USER_MAX_USERS];
static group_t groups[32];
static uint32_t user_count_val = 0;
static uint32_t group_count_val = 0;

/* Current logged-in user */
static uint32_t current_uid = 0;

void user_init(void) {
    memset(users, 0, sizeof(users));
    memset(groups, 0, sizeof(groups));
    user_count_val = 0;
group_count_val = 0;
    current_uid = 0;

    /* Create Sover user (uid=0, admin) - 最高权限用户 */
    user_create("Sover", 0, 0, 1);
    strcpy(users[0].home, "/home/sover");
    strcpy(users[0].shell, "/bin/sh");
    users[0].password_hash = 0;  /* no password by default */
    users[0].is_active = 1;

    /* Create default Admin user with password */
    user_create("Admin", 1000, 1000, 1);
    strcpy(users[1].home, "/home/admin");
    strcpy(users[1].shell, "/bin/sh");
    users[1].password_hash = user_hash_password_salt("admin", "Admin");
    users[1].is_active = 1;

    /* Create User user (no admin) */
    user_create("User", 1001, 1001, 0);
    strcpy(users[2].home, "/home/user");
    strcpy(users[2].shell, "/bin/sh");
    users[2].password_hash = 0;  /* no password */
    users[2].is_active = 1;

    /* Create Nobody */
    user_create("Nobody", 65534, 65534, 0);
    strcpy(users[3].home, "/nonexistent");
    strcpy(users[3].shell, "/bin/false");
    users[3].is_active = 0;

    group_create("sover", 0);
    group_create("admin", 1000);
    group_create("user", 1001);
    group_create("nobody", 65534);

    group_add_member(0, 0);      /* Sover in sover group */
    group_add_member(1000, 1000); /* Admin in admin group */
    group_add_member(1001, 1001); /* User in user group */
}

int user_create(const char *name, uint32_t uid, uint32_t gid, uint8_t admin) {
    if (user_count_val >= USER_MAX_USERS) return -1;

    for (uint32_t i = 0; i < user_count_val; i++) {
        if (users[i].uid == uid) return -1;
        if (strcmp(users[i].username, name) == 0) return -1;
    }

    user_t *u = &users[user_count_val];
    u->uid = uid;
    u->gid = gid;
    strncpy(u->username, name, USER_MAX_NAME - 1);
    u->username[USER_MAX_NAME - 1] = '\0';
    u->is_admin = admin;
    u->is_active = 1;
    u->password_hash = 0;
    strcpy(u->home, "/home/");
    strcat(u->home, name);
    strcpy(u->shell, "/bin/sh");

    user_count_val++;
    return 0;
}

int user_delete(const char *name) {
    /* Cannot delete Sover */
    if (strcmp(name, "Sover") == 0) return -1;

    for (uint32_t i = 0; i < user_count_val; i++) {
        if (strcmp(users[i].username, name) == 0) {
            /* Shift remaining users */
            for (uint32_t j = i; j < user_count_val - 1; j++) {
                users[j] = users[j + 1];
            }
            user_count_val--;
            memset(&users[user_count_val], 0, sizeof(user_t));
            return 0;
        }
    }
    return -1;
}

int user_set_password(const char *name, uint32_t password_hash) {
    user_t *u = user_find_by_name(name);
    if (!u) return -1;
    u->password_hash = password_hash;
    return 0;
}

int user_set_admin(const char *name, uint8_t admin) {
    user_t *u = user_find_by_name(name);
    if (!u) return -1;
    u->is_admin = admin;
    return 0;
}

user_t *user_find_by_name(const char *name) {
    for (uint32_t i = 0; i < user_count_val; i++) {
        if (strcmp(users[i].username, name) == 0) {
            return &users[i];
        }
    }
    return 0;
}

user_t *user_find_by_uid(uint32_t uid) {
    for (uint32_t i = 0; i < user_count_val; i++) {
        if (users[i].uid == uid) {
            return &users[i];
        }
    }
    return 0;
}

int user_authenticate(const char *name, const char *password) {
    user_t *u = user_find_by_name(name);
    if (!u || !u->is_active) return -1;

    /* 无密码账户：password_hash==0 且输入为空 → 直接通过 */
    if (u->password_hash == 0) {
        if (password == 0 || password[0] == '\0') return 0;
        /* 如果用户输入了密码但账户无密码，也拒绝 */
        return -1;
    }

    /* 有密码账户：验证密码哈希 */
    uint32_t hash = user_hash_password_salt(password, name);
    return (hash == u->password_hash) ? 0 : -1;
}

uint32_t user_count(void) {
    return user_count_val;
}

user_t *user_get_by_index(uint32_t index) {
    if (index >= user_count_val) return 0;
    return &users[index];
}

int group_create(const char *name, uint32_t gid) {
    if (group_count_val >= 32) return -1;

    for (uint32_t i = 0; i < group_count_val; i++) {
        if (groups[i].gid == gid) return -1;
        if (strcmp(groups[i].name, name) == 0) return -1;
    }

    group_t *g = &groups[group_count_val];
    g->gid = gid;
    strncpy(g->name, name, USER_MAX_NAME - 1);
    g->name[USER_MAX_NAME - 1] = '\0';
    g->member_count = 0;

    group_count_val++;
    return 0;
}

int group_add_member(uint32_t gid, uint32_t uid) {
    for (uint32_t i = 0; i < group_count_val; i++) {
        if (groups[i].gid == gid) {
            if (groups[i].member_count >= 16) return -1;
            for (uint32_t j = 0; j < groups[i].member_count; j++) {
                if (groups[i].members[j] == uid) return 0;
            }
            groups[i].members[groups[i].member_count++] = uid;
            return 0;
        }
    }
    return -1;
}

void user_set_current(uint32_t uid) {
    current_uid = uid;
}

uint32_t user_get_current_uid(void) {
    return current_uid;
}

user_t *user_get_current(void) {
    return user_find_by_uid(current_uid);
}

/* Simple djb2-like hash function for passwords */
uint32_t user_hash_password(const char *password) {
    uint32_t hash = 5381;
    while (*password) {
        hash = ((hash << 5) + hash) + (uint8_t)(*password);
        password++;
    }
    return hash;
}

/* 带盐值的密码哈希: 将 username 作为 salt 拼接在 password 后面再计算 djb2
 * 这样相同密码不同用户会有不同的 hash 值 */
uint32_t user_hash_password_salt(const char *password, const char *username) {
    uint32_t hash = 5381;
    /* 先哈希密码 */
    while (*password) {
        hash = ((hash << 5) + hash) + (uint8_t)(*password);
        password++;
    }
    /* 再哈希用户名(作为salt) */
    while (*username) {
        hash = ((hash << 5) + hash) + (uint8_t)(*username);
        username++;
    }
    return hash;
}

/* ---- 组查询 API ---- */

uint32_t group_count(void) {
    return group_count_val;
}

group_t *group_get_by_index(uint32_t index) {
    if (index >= group_count_val) return 0;
    return &groups[index];
}

group_t *group_find_by_gid(uint32_t gid) {
    for (uint32_t i = 0; i < group_count_val; i++) {
        if (groups[i].gid == gid) {
            return &groups[i];
        }
    }
    return 0;
}

/* 重命名用户: 根据 uid 查找用户并修改用户名 */
int user_rename(uint32_t uid, const char *new_name) {
    if (!new_name) return -1;

    /* 检查新名称长度不超过31字符 */
    uint32_t len = 0;
    while (new_name[len]) len++;
    if (len == 0 || len >= USER_MAX_NAME) return -1;

    /* 查找 uid 对应的用户 */
    user_t *u = user_find_by_uid(uid);
    if (!u) return -1;

    /* 检查新名称是否已被其他用户使用 */
    user_t *existing = user_find_by_name(new_name);
    if (existing && existing->uid != uid) return -1;

    /* 修改用户名 */
    strncpy(u->username, new_name, USER_MAX_NAME - 1);
    u->username[USER_MAX_NAME - 1] = '\0';

    return 0;
}
