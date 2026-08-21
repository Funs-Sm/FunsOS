#include "user.h"
#include "sha256.h"
#include "kheap.h"
#include "klog.h"
#include "string.h"
#include "stddef.h"
#include "stdint.h"

static user_t users[USER_MAX_USERS];
static group_t groups[USER_MAX_GROUPS];
static uint32_t user_count_val = 0;
static uint32_t group_count_val = 0;

/* Current logged-in user */
static uint32_t current_uid = 0;

extern uint32_t timer_get_ticks(void);

/* ============================================================
 * Secure credential helpers
 * ============================================================ */

static void random_bytes(uint8_t *buf, int n) {
    /* Lightweight xorshift PRNG seeded from a mix of timer + address.
     * Not crypto-strong, but sufficient for per-user salt since
     * the real defense is SHA-256(pw || salt). */
    static uint64_t state = 0xCAFEBABE12345678ULL;
    if (state == 0) state = 0xDEADBEEFDEADBEEFULL;
    for (int i = 0; i < n; i++) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        buf[i] = (uint8_t)(state & 0xFF);
    }
}

static void bytes_to_hex(const uint8_t *bytes, int n, char *out) {
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < n; i++) {
        out[i*2]     = hex[(bytes[i] >> 4) & 0xF];
        out[i*2 + 1] = hex[bytes[i] & 0xF];
    }
    out[n*2] = '\0';
}

static int hex_to_bytes(const char *hex, int expected_n, uint8_t *out) {
    for (int i = 0; i < expected_n; i++) {
        char c1 = hex[i*2], c2 = hex[i*2 + 1];
        int v1 = (c1 >= '0' && c1 <= '9') ? c1 - '0' :
                 (c1 >= 'a' && c1 <= 'f') ? c1 - 'a' + 10 :
                 (c1 >= 'A' && c1 <= 'F') ? c1 - 'A' + 10 : -1;
        int v2 = (c2 >= '0' && c2 <= '9') ? c2 - '0' :
                 (c2 >= 'a' && c2 <= 'f') ? c2 - 'a' + 10 :
                 (c2 >= 'A' && c2 <= 'F') ? c2 - 'A' + 10 : -1;
        if (v1 < 0 || v2 < 0) return -1;
        out[i] = (uint8_t)((v1 << 4) | v2);
    }
    return 0;
}

int user_password_meets_policy(const char *password) {
    if (!password) return 0;
    size_t len = strlen(password);
    if (len < USER_PASSWORD_MIN_LEN) return 0;
    if (len > 128) return 0;
    /* Require at least one non-letter character. */
    int has_other = 0;
    for (size_t i = 0; i < len; i++) {
        char c = password[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9'))) {
            has_other = 1;
        }
    }
    return has_other;
}

void user_make_credential(const char *password, const char *username,
                          char out[USER_HASH_BUF_LEN]) {
    if (!password || !out) {
        if (out) out[0] = '\0';
        return;
    }
    /* Generate 8-byte salt */
    uint8_t salt[8];
    random_bytes(salt, 8);

    /* Compute SHA-256(password || salt || username) */
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t *)password, strlen(password));
    sha256_update(&ctx, salt, 8);
    if (username) sha256_update(&ctx, (const uint8_t *)username, strlen(username));
    uint8_t digest[32];
    sha256_final(&ctx, digest);

    /* Format: salt_hex$hash_hex */
    char *p = out;
    bytes_to_hex(salt, 8, p);
    p += USER_SALT_HEX_LEN;
    *p++ = '$';
    bytes_to_hex(digest, 32, p);
    p += USER_HASH_HEX_LEN;
    *p = '\0';
}

int user_verify_credential(const char *credential, const char *password) {
    if (!credential || !password) return -1;
    if (credential[0] == '\0') {
        /* No password stored: only allow empty password. */
        return (password[0] == '\0') ? 0 : -1;
    }
    /* Parse: salt_hex$hash_hex */
    int salt_len = USER_SALT_HEX_LEN;
    if (credential[salt_len] != '$') return -1;
    const char *salt_hex = credential;
    const char *hash_hex = credential + salt_len + 1;

    /* Reconstruct salted input */
    uint8_t salt[8];
    if (hex_to_bytes(salt_hex, 8, salt) != 0) return -1;

    uint8_t expected[32];
    if (hex_to_bytes(hash_hex, 32, expected) != 0) return -1;

    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t *)password, strlen(password));
    sha256_update(&ctx, salt, 8);
    uint8_t actual[32];
    sha256_final(&ctx, actual);

    /* Constant-time compare */
    uint8_t diff = 0;
    for (int i = 0; i < 32; i++) diff |= actual[i] ^ expected[i];
    return (diff == 0) ? 0 : -1;
}

/* ============================================================
 * User lifecycle
 * ============================================================ */

void user_init(void) {
    memset(users, 0, sizeof(users));
    memset(groups, 0, sizeof(groups));
    user_count_val = 0;
    group_count_val = 0;
    current_uid = 0;

    /* Default groups */
    group_create("root", 0);
    group_create("admin", 1);
    group_create("users", USER_GID_USERS);
    group_create("nogroup", 65534);

    /* Sover (uid=0, gid=0, admin) - root, no password (root login) */
    user_create("sover", 0, 0, 1);
    strcpy(users[0].home, "/root");
    strcpy(users[0].shell, "/bin/sh");
    users[0].credential[0] = '\0';
    users[0].is_active = 1;
    users[0].failed_logins = 0;
    users[0].lock_until_tick = 0;

    /* Admin user (uid=1, gid=1, admin=1) - default password is "admin" but
     * the user MUST change it on first login (enforced by shell). */
    user_create("admin", 1, 1, 1);
    strcpy(users[1].home, "/home/admin");
    strcpy(users[1].shell, "/bin/sh");
    user_make_credential("admin", "admin", users[1].credential);
    users[1].is_active = 1;
    users[1].failed_logins = 0;
    users[1].lock_until_tick = 0;

    /* Nobody (uid=65534, idle) */
    user_create("nobody", 65534, 65534, 0);
    strcpy(users[2].home, "/nonexistent");
    strcpy(users[2].shell, "/bin/false");
    users[2].credential[0] = '\0';
    users[2].is_active = 0;
    users[2].failed_logins = 0;
    users[2].lock_until_tick = 0;

    /* Memberships */
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

    /* Usernames: [a-z0-9_-]+ starting with a letter */
    if (!(name[0] >= 'a' && name[0] <= 'z') &&
        !(name[0] >= 'A' && name[0] <= 'Z')) {
        return -1;
    }
    for (uint32_t i = 1; i < name_len; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return -1;
        }
    }

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
    u->credential[0] = '\0';
    u->failed_logins = 0;
    u->lock_until_tick = 0;
    strcpy(u->home, "/home/");
    /* strlen already validated */
    {
        size_t cur = strlen(u->home);
        size_t need = strlen(name);
        if (cur + need < sizeof(u->home)) {
            for (size_t k = 0; k < need; k++) u->home[cur + k] = name[k];
            u->home[cur + need] = '\0';
        }
    }
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
            memset(users[i].credential, 0, sizeof(users[i].credential));
            return 0;
        }
    }
    return -1;
}

int user_set_password(const char *name, const char *credential) {
    if (!name || !credential) return -1;
    user_t *u = user_find_by_name(name);
    if (!u || !u->is_active) return -1;
    strncpy(u->credential, credential, USER_HASH_BUF_LEN - 1);
    u->credential[USER_HASH_BUF_LEN - 1] = '\0';
    u->failed_logins = 0;
    u->lock_until_tick = 0;
    return 0;
}

int user_change_password(const char *name, const char *password) {
    if (!name || !password) return -1;
    user_t *u = user_find_by_name(name);
    if (!u || !u->is_active) return -1;
    if (!user_password_meets_policy(password)) {
        return -2;  /* policy failure */
    }
    user_make_credential(password, name, u->credential);
    u->failed_logins = 0;
    u->lock_until_tick = 0;
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

    /* Lockout check */
    uint32_t now = timer_get_ticks();
    if (u->lock_until_tick != 0 && now < u->lock_until_tick) {
        return -2;  /* locked */
    }

    int rc = user_verify_credential(u->credential, password);
    if (rc == 0) {
        u->failed_logins = 0;
        u->lock_until_tick = 0;
        return 0;
    }

    /* Failed login - increment counter and possibly lock. */
    if (u->failed_logins < 255) u->failed_logins++;
    if (u->failed_logins >= USER_PASSWORD_MAX_FAILS) {
        /* Lock for ~10 seconds (1000 ticks at 100 Hz). */
        u->lock_until_tick = now + 1000;
    }
    return -1;
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
    if (group_count_val >= USER_MAX_GROUPS) return -1;

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
            if (groups[i].member_count >= USER_MAX_GROUP_MEMBERS) return -1;
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

int user_rename(uint32_t uid, const char *new_name) {
    if (!new_name) return -1;
    user_t *u = user_find_by_uid(uid);
    if (!u) return -1;
    if (user_find_by_name(new_name)) return -1;
    size_t len = strlen(new_name);
    if (len >= USER_MAX_NAME) return -1;
    for (size_t i = 0; i < len; i++) {
        char c = new_name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return -1;
        }
    }
    strncpy(u->username, new_name, USER_MAX_NAME - 1);
    u->username[USER_MAX_NAME - 1] = '\0';
    return 0;
}

user_role_t user_get_role(uint32_t uid) {
    user_t *u = user_find_by_uid(uid);
    if (!u || !u->is_active) return USER_ROLE_NOBODY;
    if (uid == 0) return USER_ROLE_SOVER;
    if (uid == 1 || u->is_admin) return USER_ROLE_ADMIN;
    return USER_ROLE_USER;
}

const char *user_role_name(user_role_t role) {
    switch (role) {
        case USER_ROLE_SOVER:  return "sover";
        case USER_ROLE_ADMIN:  return "admin";
        case USER_ROLE_USER:   return "user";
        case USER_ROLE_NOBODY: return "nobody";
        default:               return "?";
    }
}

int user_is_sover(uint32_t uid)  { return uid == USER_UID_SOVER; }
int user_is_admin(uint32_t uid)  { return user_get_role(uid) == USER_ROLE_ADMIN; }
int user_is_regular(uint32_t uid){ return user_get_role(uid) == USER_ROLE_USER; }
int user_is_nobody(uint32_t uid) { return uid == USER_UID_NOBODY; }

uint32_t user_alloc_uid(void) {
    for (uint32_t uid = USER_UID_MIN; uid <= USER_UID_MAX; uid++) {
        if (!user_find_by_uid(uid)) return uid;
    }
    return 0;
}

uint32_t user_alloc_gid(void) {
    for (uint32_t gid = 200; gid < 65534; gid++) {
        if (!group_find_by_gid(gid)) return gid;
    }
    return 0;
}

int user_get_groups(uint32_t uid, uint32_t *groups, uint32_t max_groups) {
    if (!groups || max_groups == 0) return -1;
    int n = 0;
    user_t *u = user_find_by_uid(uid);
    if (!u) return 0;
    /* Primary group */
    if (n < (int)max_groups) groups[n++] = u->gid;
    /* Memberships discovered through group_count() / group_get_by_index() */
    for (uint32_t gi = 0; gi < group_count() && (uint32_t)n < max_groups; gi++) {
        group_t *g = group_get_by_index(gi);
        if (!g) continue;
        for (uint32_t mj = 0; mj < g->member_count; mj++) {
            if (g->members[mj] == uid && g->gid != u->gid) {
                groups[n++] = g->gid;
                break;
            }
        }
    }
    return n;
}

int user_in_group(uint32_t uid, uint32_t gid) {
    user_t *u = user_find_by_uid(uid);
    if (!u) return 0;
    if (u->gid == gid) return 1;
    for (uint32_t gi = 0; gi < group_count(); gi++) {
        group_t *g = group_get_by_index(gi);
        if (!g || g->gid != gid) continue;
        for (uint32_t mj = 0; mj < g->member_count; mj++) {
            if (g->members[mj] == uid) return 1;
        }
    }
    return 0;
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
