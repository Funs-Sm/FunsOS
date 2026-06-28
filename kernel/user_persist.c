#include "user_persist.h"
#include "user.h"
#include "vfs.h"
#include "string.h"
#include "kheap.h"
#include "klog.h"

#define PASSWD_PATH "/etc/passwd"
#define GROUP_PATH  "/etc/group"
#define ENV_PATH    "/etc/funsos.env"

static int uint_to_str(uint32_t val, char *buf) {
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return 1; }
    char tmp[16];
    int t = 0;
    while (val > 0) {
        tmp[t++] = '0' + (val % 10);
        val /= 10;
    }
    int len = t;
    for (int i = 0; i < t; i++) buf[i] = tmp[t - 1 - i];
    buf[len] = '\0';
    return len;
}

static uint32_t str_to_uint(const char *s, int *consumed) {
    uint32_t val = 0;
    int i = 0;
    while (s[i] >= '0' && s[i] <= '9') {
        val = val * 10 + (s[i] - '0');
        i++;
    }
    if (consumed) *consumed = i;
    return val;
}

static int uint_to_hex(uint32_t val, char *buf) {
    const char *hex = "0123456789abcdef";
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return 1; }
    char tmp[16];
    int t = 0;
    while (val > 0) {
        tmp[t++] = hex[val & 0xF];
        val >>= 4;
    }
    int len = t;
    for (int i = 0; i < t; i++) buf[i] = tmp[t - 1 - i];
    buf[len] = '\0';
    return len;
}

static uint32_t hex_to_uint(const char *s, int *consumed) {
    uint32_t val = 0;
    int i = 0;
    while (1) {
        char c = s[i];
        if (c >= '0' && c <= '9') { val = (val << 4) | (c - '0'); i++; }
        else if (c >= 'a' && c <= 'f') { val = (val << 4) | (c - 'a' + 10); i++; }
        else if (c >= 'A' && c <= 'F') { val = (val << 4) | (c - 'A' + 10); i++; }
        else break;
    }
    if (consumed) *consumed = i;
    return val;
}

void user_persist_init(void) {
    vfs_mkdir("/etc", 0755);
    vfs_mkdir("/home", 0755);
    vfs_mkdir("/root", 0700);
    vfs_mkdir("/home/admin", 0755);
}

int user_persist_save(void) {
    char line_buf[512];
    char *data = (char *)kmalloc(8192);
    if (!data) return -1;
    uint32_t data_len = 0;

    uint32_t count = user_count();
    for (uint32_t i = 0; i < count; i++) {
        user_t *u = user_get_by_index(i);
        if (!u || !u->is_active) continue;

        int pos = 0;
        const char *p = u->username;
        while (*p && pos < 300) line_buf[pos++] = *p++;
        line_buf[pos++] = ':';
        pos += uint_to_str(u->uid, line_buf + pos);
        line_buf[pos++] = ':';
        pos += uint_to_str(u->gid, line_buf + pos);
        line_buf[pos++] = ':';
        line_buf[pos++] = u->is_admin ? '1' : '0';
        line_buf[pos++] = ':';
        pos += uint_to_hex(u->password_hash, line_buf + pos);
        line_buf[pos++] = ':';
        p = u->home;
        while (*p && pos < 450) line_buf[pos++] = *p++;
        line_buf[pos++] = ':';
        p = u->shell;
        while (*p && pos < 500) line_buf[pos++] = *p++;
        line_buf[pos++] = '\n';

        for (int j = 0; j < pos && data_len < 8191; j++) {
            data[data_len++] = line_buf[j];
        }
    }
    data[data_len] = '\0';

    file_t *file = 0;
    int32_t ret = vfs_open(PASSWD_PATH, FILE_MODE_WRITE | FILE_MODE_REG, &file);
    if (ret != 0 || !file) {
        vfs_creat(PASSWD_PATH, 0644);
        ret = vfs_open(PASSWD_PATH, FILE_MODE_WRITE | FILE_MODE_REG, &file);
        if (ret != 0 || !file) {
            kfree(data);
            return -1;
        }
    }
    vfs_write(file, data, data_len);
    vfs_close(file);
    kfree(data);

    user_persist_save_groups();
    return 0;
}

int user_persist_load(void) {
    file_t *file = 0;
    int32_t ret = vfs_open(PASSWD_PATH, FILE_MODE_READ | FILE_MODE_REG, &file);
    if (ret != 0 || !file) return -1;

    char *data = (char *)kmalloc(8192);
    if (!data) { vfs_close(file); return -1; }

    int32_t n = vfs_read(file, data, 8191);
    vfs_close(file);
    if (n <= 0) { kfree(data); return -1; }
    data[n] = '\0';

    int pos = 0;
    while (pos < n) {
        int line_start = pos;
        while (pos < n && data[pos] != '\n') pos++;
        if (pos == line_start) { pos++; continue; }
        data[pos] = '\0';

        char *fields[7];
        int field_count = 0;
        int fpos = line_start;
        fields[0] = &data[fpos];
        field_count = 1;
        while (data[fpos] && field_count < 7) {
            if (data[fpos] == ':') {
                data[fpos] = '\0';
                fields[field_count++] = &data[fpos + 1];
            }
            fpos++;
        }

        if (field_count >= 5) {
            const char *username = fields[0];
            int consumed = 0;
            uint32_t uid = str_to_uint(fields[1], &consumed);
            uint32_t gid = str_to_uint(fields[2], &consumed);
            uint8_t is_admin = (fields[3][0] == '1') ? 1 : 0;
            uint32_t password_hash = hex_to_uint(fields[4], &consumed);

            user_t *existing = user_find_by_name(username);
            if (existing) {
                existing->uid = uid;
                existing->gid = gid;
                existing->is_admin = is_admin;
                existing->password_hash = password_hash;
                existing->is_active = 1;
                if (field_count >= 6 && fields[5][0]) {
                    strncpy(existing->home, fields[5], 127);
                    existing->home[127] = '\0';
                }
                if (field_count >= 7 && fields[6][0]) {
                    strncpy(existing->shell, fields[6], 63);
                    existing->shell[63] = '\0';
                }
            } else {
                if (user_create(username, uid, gid, is_admin) == 0) {
                    user_t *u = user_find_by_name(username);
                    if (u) {
                        u->password_hash = password_hash;
                        u->is_active = 1;
                        if (field_count >= 6 && fields[5][0]) {
                            strncpy(u->home, fields[5], 127);
                            u->home[127] = '\0';
                        }
                        if (field_count >= 7 && fields[6][0]) {
                            strncpy(u->shell, fields[6], 63);
                            u->shell[63] = '\0';
                        }
                    }
                }
            }
        }
        pos++;
    }

    kfree(data);

    user_persist_load_groups();
    return 0;
}

int user_persist_save_groups(void) {
    char line_buf[256];
    char *data = (char *)kmalloc(4096);
    if (!data) return -1;
    uint32_t data_len = 0;

    uint32_t count = group_count();
    for (uint32_t i = 0; i < count; i++) {
        group_t *g = group_get_by_index(i);
        if (!g) continue;

        int pos = 0;
        const char *p = g->name;
        while (*p && pos < 100) line_buf[pos++] = *p++;
        line_buf[pos++] = ':';
        pos += uint_to_str(g->gid, line_buf + pos);
        line_buf[pos++] = ':';
        for (uint32_t j = 0; j < g->member_count && pos < 240; j++) {
            if (j > 0) line_buf[pos++] = ',';
            pos += uint_to_str(g->members[j], line_buf + pos);
        }
        line_buf[pos++] = '\n';

        for (int j = 0; j < pos && data_len < 4095; j++) {
            data[data_len++] = line_buf[j];
        }
    }
    data[data_len] = '\0';

    file_t *file = 0;
    int32_t ret = vfs_open(GROUP_PATH, FILE_MODE_WRITE | FILE_MODE_REG, &file);
    if (ret != 0 || !file) {
        vfs_creat(GROUP_PATH, 0644);
        ret = vfs_open(GROUP_PATH, FILE_MODE_WRITE | FILE_MODE_REG, &file);
        if (ret != 0 || !file) {
            kfree(data);
            return -1;
        }
    }
    vfs_write(file, data, data_len);
    vfs_close(file);
    kfree(data);
    return 0;
}

int user_persist_load_groups(void) {
    file_t *file = 0;
    int32_t ret = vfs_open(GROUP_PATH, FILE_MODE_READ | FILE_MODE_REG, &file);
    if (ret != 0 || !file) return -1;

    char *data = (char *)kmalloc(4096);
    if (!data) { vfs_close(file); return -1; }

    int32_t n = vfs_read(file, data, 4095);
    vfs_close(file);
    if (n <= 0) { kfree(data); return -1; }
    data[n] = '\0';

    int pos = 0;
    while (pos < n) {
        int line_start = pos;
        while (pos < n && data[pos] != '\n') pos++;
        if (pos == line_start) { pos++; continue; }
        data[pos] = '\0';

        char *name = &data[line_start];
        char *colon1 = 0;
        char *colon2 = 0;
        int fpos = line_start;
        while (fpos < pos) {
            if (data[fpos] == ':') {
                if (!colon1) { colon1 = &data[fpos]; data[fpos] = '\0'; }
                else if (!colon2) { colon2 = &data[fpos]; data[fpos] = '\0'; break; }
            }
            fpos++;
        }

        if (colon1 && colon2) {
            int consumed = 0;
            uint32_t gid = str_to_uint(colon1 + 1, &consumed);

            group_t *existing = group_find_by_gid(gid);
            if (!existing) {
                group_create(name, gid);
                existing = group_find_by_gid(gid);
            }
            if (existing) {
                char *memb = colon2 + 1;
                while (*memb && memb < &data[pos]) {
                    uint32_t uid = str_to_uint(memb, &consumed);
                    if (consumed > 0) {
                        group_add_member(gid, uid);
                        memb += consumed;
                    }
                    if (*memb == ',') memb++;
                    else break;
                }
            }
        }
        pos++;
    }

    kfree(data);
    return 0;
}

int user_persist_save_env(void) {
    return 0;
}

int user_persist_load_env(void) {
    file_t *file = 0;
    int32_t ret = vfs_open(ENV_PATH, FILE_MODE_READ | FILE_MODE_REG, &file);
    if (ret != 0 || !file) return -1;

    char *data = (char *)kmalloc(4096);
    if (!data) { vfs_close(file); return -1; }

    int32_t n = vfs_read(file, data, 4095);
    vfs_close(file);
    if (n <= 0) { kfree(data); return -1; }
    data[n] = '\0';

    kfree(data);
    return 0;
}
