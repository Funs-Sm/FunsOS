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

    /* 创建默认组 */
    group_create("root", 0);
    group_create("admin", 1);
    group_create("nogroup", 65534);

    /* Create Sover user (uid=0, gid=0, admin=1) - 最高权限用户 */
    user_create("sover", 0, 0, 1);
    strcpy(users[0].home, "/root");
    strcpy(users[0].shell, "/bin/sh");
    users[0].password_hash = 0;
    users[0].is_active = 1;

    /* Create default Admin user (uid=1, gid=1, admin=1) */
    user_create("admin", 1, 1, 1);
    strcpy(users[1].home, "/home/admin");
    strcpy(users[1].shell, "/bin/sh");
    users[1].password_hash = user_hash_password_salt("admin", "admin");
    users[1].is_active = 1;

    /* Create Nobody (uid=65534, gid=65534, admin=0) */
    user_create("nobody", 65534, 65534, 0);
    strcpy(users[2].home, "/nonexistent");
    strcpy(users[2].shell, "/bin/false");
    users[2].password_hash = 0;
    users[2].is_active = 0;

    /* 组成员关系 */
    group_add_member(0, 0);
    group_add_member(1, 1);
    group_add_member(65534, 65534);
}

int user_create(const char *name, uint32_t uid, uint32_t gid, uint8_t admin) {
    if (!name || name[0] == '\0') return -1;
    if (user_count_val >= USER_MAX_USERS) return -1;

    uint32_t name_len = 0;
    while (name[name_len]) name_len++;
    if (name_len >= USER_MAX_NAME) return -1;

    for (uint32_t i = 0; i < user_count_val; i++) {
        if (users[i].is_active && users[i].uid == uid) return -1;
        if (users[i].is_active && strcmp(users[i].username, name) == 0) return -1;
    }

    user_t *u = &users[user_count_val];
    memset(u, 0, sizeof(user_t));
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
    if (!name) return -1;
    if (strcmp(name, "sover") == 0) return -1;

    user_t *current = user_get_current();
    if (current && strcmp(current->username, name) == 0) return -1;

    for (uint32_t i = 0; i < user_count_val; i++) {
        if (users[i].is_active && strcmp(users[i].username, name) == 0) {
            users[i].is_active = 0;
            memset(users[i].username, 0, USER_MAX_NAME);
            return 0;
        }
    }
    return -1;
}

int user_set_password(const char *name, uint32_t password_hash) {
    if (!name) return -1;
    user_t *u = user_find_by_name(name);
    if (!u || !u->is_active) return -1;
    u->password_hash = password_hash;
    return 0;
}

int user_change_password(const char *name, const char *password) {
    if (!name || !password) return -1;
    user_t *u = user_find_by_name(name);
    if (!u || !u->is_active) return -1;
    u->password_hash = user_hash_password_salt(password, name);
    return 0;
}

int user_set_admin(const char *name, uint8_t admin) {
    if (!name) return -1;
    if (strcmp(name, "sover") == 0 && admin == 0) return -1;
    user_t *u = user_find_by_name(name);
    if (!u || !u->is_active) return -1;
    u->is_admin = admin;
    return 0;
}

int user_set_home(const char *name, const char *home) {
    if (!name || !home) return -1;
    user_t *u = user_find_by_name(name);
    if (!u || !u->is_active) return -1;
    strncpy(u->home, home, 127);
    u->home[127] = '\0';
    return 0;
}

int user_set_shell(const char *name, const char *shell) {
    if (!name || !shell) return -1;
    user_t *u = user_find_by_name(name);
    if (!u || !u->is_active) return -1;
    strncpy(u->shell, shell, 63);
    u->shell[63] = '\0';
    return 0;
}

user_t *user_find_by_name(const char *name) {
    if (!name) return 0;
    for (uint32_t i = 0; i < user_count_val; i++) {
        if (users[i].is_active && strcmp(users[i].username, name) == 0) {
            return &users[i];
        }
    }
    return 0;
}

user_t *user_find_by_uid(uint32_t uid) {
    for (uint32_t i = 0; i < user_count_val; i++) {
        if (users[i].is_active && users[i].uid == uid) {
            return &users[i];
        }
    }
    return 0;
}

int user_authenticate(const char *name, const char *password) {
    if (!name || !password) return -1;
    user_t *u = user_find_by_name(name);
    if (!u || !u->is_active) return -1;

    if (u->password_hash == 0) {
        if (password[0] == '\0') return 0;
        return -1;
    }

    uint32_t hash = user_hash_password_salt(password, name);
    return (hash == u->password_hash) ? 0 : -1;
}

uint32_t user_count(void) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < user_count_val; i++) {
        if (users[i].is_active) count++;
    }
    return count;
}

user_t *user_get_by_index(uint32_t index) {
    uint32_t active_idx = 0;
    for (uint32_t i = 0; i < user_count_val; i++) {
        if (users[i].is_active) {
            if (active_idx == index) return &users[i];
            active_idx++;
        }
    }
    return 0;
}

int group_create(const char *name, uint32_t gid) {
    if (!name) return -1;
    if (group_count_val >= 32) return -1;

    for (uint32_t i = 0; i < group_count_val; i++) {
        if (groups[i].gid == gid) return -1;
        if (strcmp(groups[i].name, name) == 0) return -1;
    }

    group_t *g = &groups[group_count_val];
    memset(g, 0, sizeof(group_t));
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

int group_remove_member(uint32_t gid, uint32_t uid) {
    for (uint32_t i = 0; i < group_count_val; i++) {
        if (groups[i].gid == gid) {
            for (uint32_t j = 0; j < groups[i].member_count; j++) {
                if (groups[i].members[j] == uid) {
                    for (uint32_t k = j; k < groups[i].member_count - 1; k++) {
                        groups[i].members[k] = groups[i].members[k + 1];
                    }
                    groups[i].member_count--;
                    return 0;
                }
            }
            return -1;
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

uint32_t user_hash_password(const char *password) {
    if (!password) return 0;
    uint32_t hash = 5381;
    while (*password) {
        hash = ((hash << 5) + hash) + (uint8_t)(*password);
        password++;
    }
    return hash;
}

uint32_t user_hash_password_salt(const char *password, const char *username) {
    if (!password || !username) return 0;
    uint32_t hash = 5381;
    while (*password) {
        hash = ((hash << 5) + hash) + (uint8_t)(*password);
        password++;
    }
    while (*username) {
        hash = ((hash << 5) + hash) + (uint8_t)(*username);
        username++;
    }
    return hash;
}

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

group_t *group_find_by_name(const char *name) {
    if (!name) return 0;
    for (uint32_t i = 0; i < group_count_val; i++) {
        if (strcmp(groups[i].name, name) == 0) {
            return &groups[i];
        }
    }
    return 0;
}

int user_rename(uint32_t uid, const char *new_name) {
    if (!new_name) return -1;

    uint32_t len = 0;
    while (new_name[len]) len++;
    if (len == 0 || len >= USER_MAX_NAME) return -1;

    user_t *u = user_find_by_uid(uid);
    if (!u) return -1;

    user_t *existing = user_find_by_name(new_name);
    if (existing && existing->uid != uid) return -1;

    strncpy(u->username, new_name, USER_MAX_NAME - 1);
    u->username[USER_MAX_NAME - 1] = '\0';

    return 0;
}

user_role_t user_get_role(uint32_t uid) {
    if (uid == USER_UID_SOVER) return USER_ROLE_SOVER;
    if (uid == USER_UID_NOBODY) return USER_ROLE_NOBODY;

    user_t *u = user_find_by_uid(uid);
    if (!u) return USER_ROLE_NOBODY;

    if (u->is_admin) return USER_ROLE_ADMIN;
    return USER_ROLE_USER;
}

const char *user_role_name(user_role_t role) {
    switch (role) {
        case USER_ROLE_SOVER:  return "Sover";
        case USER_ROLE_ADMIN:  return "Admin";
        case USER_ROLE_USER:   return "User";
        case USER_ROLE_NOBODY: return "Nobody";
        default: return "Unknown";
    }
}

int user_is_sover(uint32_t uid) {
    return uid == USER_UID_SOVER;
}

int user_is_admin(uint32_t uid) {
    if (uid == USER_UID_SOVER) return 1;
    user_t *u = user_find_by_uid(uid);
    return u ? u->is_admin : 0;
}

int user_is_regular(uint32_t uid) {
    user_t *u = user_find_by_uid(uid);
    if (!u) return 0;
    if (uid == USER_UID_SOVER) return 0;
    if (uid == USER_UID_NOBODY) return 0;
    return !u->is_admin;
}

int user_is_nobody(uint32_t uid) {
    return uid == USER_UID_NOBODY;
}

uint32_t user_alloc_uid(void) {
    uint32_t uid = USER_UID_MIN;
    while (user_find_by_uid(uid)) {
        uid++;
        if (uid > USER_UID_MAX) return 0;
    }
    return uid;
}

uint32_t user_alloc_gid(void) {
    uint32_t gid = USER_UID_MIN;
    while (group_find_by_gid(gid)) {
        gid++;
        if (gid > USER_UID_MAX) return 0;
    }
    return gid;
}

int user_get_groups(uint32_t uid, uint32_t *groups, uint32_t max_groups) {
    if (!groups || max_groups == 0) return -1;

    user_t *u = user_find_by_uid(uid);
    if (!u) return -1;

    uint32_t count = 0;

    if (count < max_groups) {
        groups[count++] = u->gid;
    }

    uint32_t gc = group_count();
    for (uint32_t i = 0; i < gc && count < max_groups; i++) {
        group_t *g = group_get_by_index(i);
        if (!g) continue;
        if (g->gid == u->gid) continue;

        for (uint32_t j = 0; j < g->member_count; j++) {
            if (g->members[j] == uid) {
                groups[count++] = g->gid;
                break;
            }
        }
    }

    return (int)count;
}

int user_in_group(uint32_t uid, uint32_t gid) {
    user_t *u = user_find_by_uid(uid);
    if (!u) return 0;

    if (u->gid == gid) return 1;

    group_t *g = group_find_by_gid(gid);
    if (!g) return 0;

    for (uint32_t i = 0; i < g->member_count; i++) {
        if (g->members[i] == uid) return 1;
    }

    return 0;
}
