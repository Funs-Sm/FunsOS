/* config.c - 系统配置管理实现
 * 从 /etc/funsos.conf 加载键值对配置
 * 在 shell 中添加 config 命令
 */

#include "config.h"
#include "kheap.h"
#include "klog.h"
#include "string.h"
#include "stdlib.h"
#include "vfs.h"

/* 配置存储 */
#define CONFIG_MAX_ENTRIES 128
#define CONFIG_DEFAULT_PATH "/etc/funsos.conf"

static config_entry_t g_config[CONFIG_MAX_ENTRIES];
static uint32_t g_config_count = 0;
static uint8_t g_config_initialized = 0;

/* 初始化配置系统 */
void config_init(void)
{
    if (g_config_initialized) return;

    g_config_count = 0;
    memset(g_config, 0, sizeof(g_config));

    /* 确保 /etc 目录存在 */
    vfs_mkdir("/etc", 0x1FF);

    /* 加载默认配置 */
    config_load(CONFIG_DEFAULT_PATH);

    /* 如果没有配置文件，设置默认值 */
    if (g_config_count == 0) {
        config_set("hostname", "funsos");
        config_set("version", "0.3");
        config_set("kernel", "FunsCore");
        config_set("shell", "/bin/shell");
        config_set("display.width", "800");
        config_set("display.height", "600");
        config_set("display.bpp", "32");
        config_set("display.refresh", "60");
        config_set("audio.device", "default");
        config_set("audio.volume", "75");
        config_set("network.hostname", "funsos");
        config_set("network.dhcp", "1");
        config_set("network.dns", "8.8.8.8");
        config_set("fs.root", "ext2");
        config_set("fs.cache_size", "4096");
        config_set("scheduler.quantum", "10");
        config_set("scheduler.algorithm", "round_robin");
        config_set("memory.swap", "1");
        config_set("memory.swap_size", "65536");
        config_set("theme", "default");
        config_set("language", "zh_CN");
        config_set("timezone", "Asia/Shanghai");
        config_set("keyboard.layout", "us");
        config_set("mouse.speed", "5");
        config_set("db.path", "/var/db");
        config_set("log.level", "info");
        config_set("log.path", "/var/log");

        /* 保存默认配置 */
        config_save(CONFIG_DEFAULT_PATH);
    }

    g_config_initialized = 1;
    klog_info("Config: Configuration system initialized (%d entries)", g_config_count);
}

/* 从文件加载配置 */
int config_load(const char *path)
{
    file_t *f = NULL;
    vfs_open(path, FILE_MODE_READ, &f);  /* 只读 */
    if (f == NULL) {
        klog_warn("Config: Cannot open %s", path);
        return -1;
    }

    char buf[512];
    int n;

    while ((n = vfs_read(f, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';

        /* 逐行解析 */
        int line_start = 0;
        for (int i = 0; i <= n; i++) {
            if (buf[i] == '\n' || buf[i] == '\0') {
                int line_len = i - line_start;
                if (line_len > 0 && line_len < 512) {
                    char line[512];
                    for (int j = 0; j < line_len; j++)
                        line[j] = buf[line_start + j];
                    line[line_len] = '\0';

                    /* 跳过注释和空行 */
                    char *p = line;
                    while (*p == ' ' || *p == '\t') p++;

                    if (*p == '#' || *p == '\0' || *p == '\n')
                        continue;

                    /* 解析 key = value */
                    char key[64] = {0};
                    char value[256] = {0};
                    int ki = 0, vi = 0;
                    int found_eq = 0;

                    for (int j = 0; p[j]; j++) {
                        if (p[j] == '=' && !found_eq) {
                            found_eq = 1;
                            continue;
                        }

                        if (!found_eq) {
                            if (p[j] != ' ' && p[j] != '\t' && ki < 63)
                                key[ki++] = p[j];
                        } else {
                            if (p[j] != '\n' && p[j] != '\r' && vi < 255)
                                value[vi++] = p[j];
                        }
                    }

                    /* 去除 value 前导空格 */
                    int vs = 0;
                    while (value[vs] == ' ' || value[vs] == '\t') vs++;

                    if (ki > 0 && vi > vs) {
                        /* 移动 value 到开头 */
                        if (vs > 0) {
                            for (int j = vs; value[j]; j++)
                                value[j - vs] = value[j];
                            value[vi - vs] = '\0';
                        }

                        /* 存储配置项 */
                        config_set(key, value);
                    }
                }

                line_start = i + 1;
            }
        }
    }

    vfs_close(f);
    klog_info("Config: Loaded from %s (%d entries)", path, g_config_count);
    return 0;
}

/* 保存配置到文件 */
int config_save(const char *path)
{
    vfs_creat(path, 0x1FF);
    file_t *f = NULL;
    vfs_open(path, FILE_MODE_WRITE, &f);  /* 写入 */
    if (f == NULL) {
        klog_warn("Config: Cannot write to %s", path);
        return -1;
    }

    /* 写入文件头 */
    const char *header = "# FUNSOS Configuration File\n# Auto-generated\n\n";
    vfs_write(f, (void *)header, strlen(header));

    /* 写入所有配置项 */
    for (uint32_t i = 0; i < g_config_count; i++) {
        char line[320];
        int pos = 0;

        /* key */
        for (int j = 0; g_config[i].key[j] && pos < 318; j++)
            line[pos++] = g_config[i].key[j];

        /* = */
        line[pos++] = '=';

        /* value */
        for (int j = 0; g_config[i].value[j] && pos < 318; j++)
            line[pos++] = g_config[i].value[j];

        /* \n */
        line[pos++] = '\n';

        vfs_write(f, line, pos);
    }

    vfs_close(f);
    klog_info("Config: Saved to %s", path);
    return 0;
}

/* 获取字符串配置值 */
const char *config_get(const char *key, const char *default_value)
{
    for (uint32_t i = 0; i < g_config_count; i++) {
        int match = 1;
        for (int j = 0; j < 63 && (key[j] || g_config[i].key[j]); j++) {
            if (key[j] != g_config[i].key[j]) { match = 0; break; }
        }
        if (match) return g_config[i].value;
    }
    return default_value;
}

/* 设置字符串配置值 */
int config_set(const char *key, const char *value)
{
    /* 查找是否已存在 */
    for (uint32_t i = 0; i < g_config_count; i++) {
        int match = 1;
        for (int j = 0; j < 63 && (key[j] || g_config[i].key[j]); j++) {
            if (key[j] != g_config[i].key[j]) { match = 0; break; }
        }
        if (match) {
            /* 更新值 */
            for (int j = 0; j < 255 && value[j]; j++)
                g_config[i].value[j] = value[j];
            g_config[i].value[255] = '\0';
            return 0;
        }
    }

    /* 添加新条目 */
    if (g_config_count >= CONFIG_MAX_ENTRIES)
        return -1;

    config_entry_t *entry = &g_config[g_config_count];

    for (int j = 0; j < 63 && key[j]; j++)
        entry->key[j] = key[j];
    entry->key[63] = '\0';

    for (int j = 0; j < 255 && value[j]; j++)
        entry->value[j] = value[j];
    entry->value[255] = '\0';

    g_config_count++;
    return 0;
}

/* 获取整数配置值 */
int config_get_int(const char *key, int default_value)
{
    const char *val = config_get(key, NULL);
    if (val == NULL) return default_value;

    /* 解析整数 */
    int result = 0;
    int negative = 0;

    if (*val == '-') {
        negative = 1;
        val++;
    }

    while (*val >= '0' && *val <= '9') {
        result = result * 10 + (*val - '0');
        val++;
    }

    return negative ? -result : result;
}

/* 设置整数配置值 */
int config_set_int(const char *key, int value)
{
    char buf[32];
    int pos = 0;

    if (value < 0) {
        buf[pos++] = '-';
        value = -value;
    }

    /* 转换为字符串 */
    char digits[12];
    int d = 0;

    if (value == 0) {
        digits[d++] = '0';
    } else {
        while (value > 0) {
            digits[d++] = '0' + (value % 10);
            value /= 10;
        }
    }

    for (int i = d - 1; i >= 0; i--)
        buf[pos++] = digits[i];

    buf[pos] = '\0';

    return config_set(key, buf);
}

/* 列出所有配置项 */
void config_list(void)
{
    klog_info("=== System Configuration ===");
    for (uint32_t i = 0; i < g_config_count; i++) {
        klog_info("  %s = %s", g_config[i].key, g_config[i].value);
    }
    klog_info("Total: %d entries", g_config_count);
}

/* 获取配置项数量 */
uint32_t config_count(void)
{
    return g_config_count;
}

/* 删除配置项 */
int config_remove(const char *key)
{
    for (uint32_t i = 0; i < g_config_count; i++) {
        int match = 1;
        for (int j = 0; j < 63 && (key[j] || g_config[i].key[j]); j++) {
            if (key[j] != g_config[i].key[j]) { match = 0; break; }
        }
        if (match) {
            /* 移动后续条目 */
            for (uint32_t k = i; k < g_config_count - 1; k++)
                g_config[k] = g_config[k + 1];

            memset(&g_config[g_config_count - 1], 0, sizeof(config_entry_t));
            g_config_count--;
            return 0;
        }
    }
    return -1;
}

/* 检查配置项是否存在 */
int config_has(const char *key)
{
    for (uint32_t i = 0; i < g_config_count; i++) {
        int match = 1;
        for (int j = 0; j < 63 && (key[j] || g_config[i].key[j]); j++) {
            if (key[j] != g_config[i].key[j]) { match = 0; break; }
        }
        if (match) return 1;
    }
    return 0;
}
