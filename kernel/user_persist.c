#include "user_persist.h"
#include "user.h"
#include "vfs.h"
#include "string.h"
#include "kheap.h"
#include "klog.h"

/* passwd 文件格式: username:uid:gid:is_admin:password_hash:home:shell\n
 * 每行一个用户，字段用冒号分隔 */

#define PASSWD_PATH "/etc/passwd"
#define ENV_PATH    "/etc/funsos.env"

/* 内部辅助: 将 uint32_t 转为字符串，返回写入的字符数 */
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

/* 内部辅助: 从字符串解析 uint32_t */
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

/* 内部辅助: 将 uint32_t 转为十六进制字符串 */
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

/* 内部辅助: 从十六进制字符串解析 uint32_t */
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
    /* 确保目录存在 */
    vfs_mkdir("/etc", 0755);
}

int user_persist_save(void) {
    /* 将所有活跃用户写入 /etc/passwd */
    char line_buf[512];
    /* 先收集所有数据到缓冲区 */
    char *data = (char *)kmalloc(8192);
    if (!data) return -1;
    uint32_t data_len = 0;

    uint32_t count = user_count();
    for (uint32_t i = 0; i < count; i++) {
        user_t *u = user_get_by_index(i);
        if (!u || !u->is_active) continue;

        /* 格式: username:uid:gid:is_admin:password_hash:home:shell\n */
        int pos = 0;
        /* username */
        const char *p = u->username;
        while (*p && pos < 300) line_buf[pos++] = *p++;
        line_buf[pos++] = ':';
        /* uid */
        pos += uint_to_str(u->uid, line_buf + pos);
        line_buf[pos++] = ':';
        /* gid */
        pos += uint_to_str(u->gid, line_buf + pos);
        line_buf[pos++] = ':';
        /* is_admin */
        line_buf[pos++] = u->is_admin ? '1' : '0';
        line_buf[pos++] = ':';
        /* password_hash (hex) */
        pos += uint_to_hex(u->password_hash, line_buf + pos);
        line_buf[pos++] = ':';
        /* home */
        p = u->home;
        while (*p && pos < 450) line_buf[pos++] = *p++;
        line_buf[pos++] = ':';
        /* shell */
        p = u->shell;
        while (*p && pos < 500) line_buf[pos++] = *p++;
        line_buf[pos++] = '\n';

        /* 复制到 data 缓冲区 */
        for (int j = 0; j < pos && data_len < 8191; j++) {
            data[data_len++] = line_buf[j];
        }
    }
    data[data_len] = '\0';

    /* 写入文件 */
    file_t *file = 0;
    int32_t ret = vfs_open(PASSWD_PATH, FILE_MODE_WRITE | FILE_MODE_REG, &file);
    if (ret != 0 || !file) {
        /* 尝试创建文件 */
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
    return 0;
}

int user_persist_load(void) {
    /* 从 /etc/passwd 加载用户数据 */
    file_t *file = 0;
    int32_t ret = vfs_open(PASSWD_PATH, FILE_MODE_READ | FILE_MODE_REG, &file);
    if (ret != 0 || !file) return -1;

    char *data = (char *)kmalloc(8192);
    if (!data) { vfs_close(file); return -1; }

    int32_t n = vfs_read(file, data, 8191);
    vfs_close(file);
    if (n <= 0) { kfree(data); return -1; }
    data[n] = '\0';

    /* 逐行解析 */
    int pos = 0;
    while (pos < n) {
        /* 找行尾 */
        int line_start = pos;
        while (pos < n && data[pos] != '\n') pos++;
        if (pos == line_start) { pos++; continue; }
        data[pos] = '\0';

        /* 解析行: username:uid:gid:is_admin:password_hash:home:shell */
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

            /* 查找或创建用户 */
            user_t *existing = user_find_by_name(username);
            if (existing) {
                /* 更新现有用户 */
                existing->uid = uid;
                existing->gid = gid;
                existing->is_admin = is_admin;
                existing->password_hash = password_hash;
                if (field_count >= 6 && fields[5][0]) {
                    strncpy(existing->home, fields[5], 127);
                    existing->home[127] = '\0';
                }
                if (field_count >= 7 && fields[6][0]) {
                    strncpy(existing->shell, fields[6], 63);
                    existing->shell[63] = '\0';
                }
            } else {
                /* 创建新用户 */
                if (user_create(username, uid, gid, is_admin) == 0) {
                    user_t *u = user_find_by_name(username);
                    if (u) {
                        u->password_hash = password_hash;
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
        pos++; /* 跳过 \n */
    }

    kfree(data);
    return 0;
}

int user_persist_save_env(void) {
    /* 环境变量保存 - 简单实现: NAME=VALUE\n 格式 */
    /* 此函数需要在 shell.c 中调用，因为 env_vars 是 static 的 */
    /* 这里提供接口，实际保存由 shell 命令触发 */
    return 0;
}

int user_persist_load_env(void) {
    /* 环境变量加载 */
    file_t *file = 0;
    int32_t ret = vfs_open(ENV_PATH, FILE_MODE_READ | FILE_MODE_REG, &file);
    if (ret != 0 || !file) return -1;

    char *data = (char *)kmalloc(4096);
    if (!data) { vfs_close(file); return -1; }

    int32_t n = vfs_read(file, data, 4095);
    vfs_close(file);
    if (n <= 0) { kfree(data); return -1; }
    data[n] = '\0';

    /* 环境变量加载由 shell 内部处理，这里只读取数据 */
    kfree(data);
    return 0;
}
