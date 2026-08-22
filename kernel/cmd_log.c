/*
 * kernel/cmd_log.c
 * cmd_dmesg, cmd_loglevel, cmd_syslog
 *
 * Provides kernel log / syslog / log level inspection commands.
 */

#include "cmd_log.h"
#include "shell.h"
#include "shell_error.h"
#include "klog.h"
#include "stdio.h"
#include "string.h"

/* 将日志级别数字转换为名称字符�?*/
static const char *level_to_name(uint32_t level) {
    static const char *names[] = {
        "EMERG", "ALERT", "CRIT", "ERR", "WARN", "NOTICE", "INFO", "DEBUG"
    };
    if (level > 7) {
        return "UNKNOWN";
    }
    return names[level];
}

/* 将名称字符串或数字字符串转换为级别值；成功返回 0-7，失败返�?-1 */
static int parse_level_arg(const char *arg) {
    /* 先尝试解析数字参�?0-7 */
    if (arg[0] >= '0' && arg[0] <= '7' && arg[1] == '\0') {
        return arg[0] - '0';
    }
    /* 解析名称参数 */
    if (strcmp(arg, "emerg") == 0)   return KLOG_EMERG;
    if (strcmp(arg, "alert") == 0)  return KLOG_ALERT;
    if (strcmp(arg, "crit") == 0)   return KLOG_CRIT;
    if (strcmp(arg, "err") == 0)    return KLOG_ERR;
    if (strcmp(arg, "warn") == 0)   return KLOG_WARNING;
    if (strcmp(arg, "notice") == 0) return KLOG_NOTICE;
    if (strcmp(arg, "info") == 0)   return KLOG_INFO;
    if (strcmp(arg, "debug") == 0)  return KLOG_DEBUG;
    return -1;
}

/*
 * cmd_dmesg - 读取并显示内核环形缓冲区的内�?
 *
 * 选项�?
 *   -l LEVEL  按级别过�?(emerg/alert/crit/err/warn/notice/info/debug �?0-7)
 *   -n COUNT 只显示最�?N �?
 *   -c       读取后清空缓冲区
 *   -h       显示帮助
 *   无参�?  显示全部内容
 */
void cmd_dmesg(const char *args) {
    int filter_level = -1;   /* -1 表示不过�?*/
    int last_n = -1;         /* -1 表示显示全部 */
    int clear_after = 0;
    const char *p = args;
    char buf[16];

    /* 解析选项字符�?(空格分隔的选项列表) */
    if (p && *p) {
        /* 处理前导空格 */
        while (*p == ' ') p++;

        while (*p) {
            if (p[0] == '-' && p[1] == 'l' && (p[2] == ' ' || p[2] == '\0')) {
                p += 2;
                /* 跳过选项后的空格 */
                while (*p == ' ') p++;
                /* 读取级别参数 */
                {
                    const char *tok = p;
                    int tlen = 0;
                    while (p[tlen] && p[tlen] != ' ') tlen++;
                    if (tlen == 0) {
                        shell_print("dmesg: -l requires a level argument\n");
                        shell_last_exit_code = 1;
                        return;
                    }
                    if (tlen >= (int)sizeof(buf)) {
                        shell_print("dmesg: level argument too long\n");
                        shell_last_exit_code = 1;
                        return;
                    }
                    strncpy(buf, tok, tlen);
                    buf[tlen] = '\0';
                    filter_level = parse_level_arg(buf);
                    if (filter_level < 0) {
                        shell_print("dmesg: invalid level '");
                        shell_print(buf);
                        shell_print("'\n");
                        shell_last_exit_code = 1;
                        return;
                    }
                    p += tlen;
                }
            } else if (p[0] == '-' && p[1] == 'n' && (p[2] == ' ' || p[2] == '\0')) {
                p += 2;
                while (*p == ' ') p++;
                {
                    const char *tok = p;
                    int tlen = 0;
                    while (p[tlen] && p[tlen] != ' ') tlen++;
                    if (tlen == 0) {
                        shell_print("dmesg: -n requires a count argument\n");
                        shell_last_exit_code = 1;
                        return;
                    }
                    if (tlen >= (int)sizeof(buf)) {
                        shell_print("dmesg: count argument too long\n");
                        shell_last_exit_code = 1;
                        return;
                    }
                    strncpy(buf, tok, tlen);
                    buf[tlen] = '\0';
                    /* 解析数字 */
                    last_n = 0;
                    for (int i = 0; i < tlen; i++) {
                        if (buf[i] < '0' || buf[i] > '9') {
                            shell_print("dmesg: -n requires a numeric count\n");
                            shell_last_exit_code = 1;
                            return;
                        }
                        last_n = last_n * 10 + (buf[i] - '0');
                    }
                    p += tlen;
                }
            } else if (p[0] == '-' && p[1] == 'c' && (p[2] == ' ' || p[2] == '\0')) {
                clear_after = 1;
                p += 2;
            } else if ((p[0] == '-' && p[1] == 'h') ||
                       (p[0] == '-' && p[1] == '-' && p[2] == 'h')) {
                shell_print("Usage: dmesg [OPTION]\n");
                shell_print("  -l LEVEL    Filter by level (emerg/alert/crit/err/warn/notice/info/debug or 0-7)\n");
                shell_print("  -n COUNT    Show last N lines\n");
                shell_print("  -c          Clear ring buffer after reading\n");
                shell_print("  -h          Show this help\n");
                shell_last_exit_code = 0;
                return;
            } else {
                /* 未知选项 */
                shell_print("dmesg: unknown option. Use 'dmesg -h' for usage.\n");
                shell_last_exit_code = 1;
                return;
            }
            /* 跳过选项间的空格 */
            while (*p == ' ') p++;
        }
    }

    /* �?klog 读取环形缓冲区内�?*/
    char log_buf[4096];
    uint32_t total_lines = klog_get_line_count();
    uint32_t read_len;

    /* 确定读取策略�?
     * - 如果需要最�?N 行且有足够的行，从适当位置开始读�?
     * - 否则读取全部 */
    if (last_n > 0 && (uint32_t)last_n < total_lines) {
        uint32_t start_line = total_lines - last_n;
        read_len = klog_read_from(start_line, log_buf, sizeof(log_buf) - 1);
    } else {
        read_len = klog_read(log_buf, sizeof(log_buf) - 1);
    }
    log_buf[read_len] = '\0';

    /* 按级别过滤并输出 */
    int has_output = 0;
    char *line = log_buf;
    char *line_end;

    while (*line) {
        /* 找到当前行结�?*/
        line_end = line;
        while (*line_end && *line_end != '\n' && *line_end != '\r') {
            line_end++;
        }

        if (line_end > line) {
            char save = *line_end;
            char tmp[16];
            int printed = 0;

            /* 提取行首的级别标签，格式�?"[KERN EMERG] " �?"<0> " */
            if (line[0] == '[') {
                /* 格式: [KERN XXXX] �?[LEVEL] */
                const char *bracket_end = strchr(line, ']');
                if (bracket_end && bracket_end < line_end) {
                    int blen = bracket_end - line - 1; /* 不含方括�?*/
                    if (blen < (int)sizeof(tmp) - 1) {
                        strncpy(tmp, line + 1, blen);
                        tmp[blen] = '\0';
                        /* 从标签中提取级别数字 */
                        {
                            int lvl = -1;
                            const char *t = tmp;
                            /* 跳过 "KERN " 前缀 */
                            if (strncmp(t, "KERN ", 5) == 0) t += 5;
                            /* 解析数字 */
                            if (*t >= '0' && *t <= '9') {
                                lvl = 0;
                                while (*t >= '0' && *t <= '9') {
                                    lvl = lvl * 10 + (*t - '0');
                                    t++;
                                }
                            }
                            if (lvl >= 0 && lvl <= 7) {
                                /* 检查过滤器 */
                                if (filter_level < 0 || (uint32_t)lvl <= filter_level) {
                                    /* 行尾恢复并打印整�?*/
                                    *line_end = save;
                                    shell_print(line);
                                    *line_end = '\0';
                                    shell_print("\n");
                                    has_output = 1;
                                    printed = 1;
                                }
                            }
                        }
                    }
                }
            } else if (line[0] == '<' && line[1] >= '0' && line[1] <= '9') {
                /* 格式: <N> */
                int lvl = line[1] - '0';
                if (line[2] == '>') {
                    if (filter_level < 0 || (uint32_t)lvl <= filter_level) {
                        *line_end = save;
                        shell_print(line);
                        *line_end = '\0';
                        shell_print("\n");
                        has_output = 1;
                        printed = 1;
                    } else {
                        printed = 1;
                    }
                }
            }

            /* 如果没有按级别解析，仍输出该行（非结构化日志�?*/
            if (!printed) {
                *line_end = save;
                shell_print(line);
                *line_end = '\0';
                shell_print("\n");
                has_output = 1;
            }
        }

        /* 前进到下一�?*/
        line = line_end;
        if (*line == '\n') line++;
        if (*line == '\r') line++;
    }

    if (!has_output && read_len == 0) {
        shell_print("dmesg: ring buffer empty\n");
    }

    /* 读取后清空缓冲区 */
    if (clear_after && read_len > 0) {
        klog_clear();
        shell_print("[dmesg: ring buffer cleared]\n");
    }

    shell_last_exit_code = 0;
}

/*
 * cmd_loglevel - 获取或设置内核日志级�?
 *
 * 无参�?  显示当前级别
 * 数字 0-7 设置级别
 * 名称     设置级别 (emerg/alert/crit/err/warn/notice/info/debug)
 */
void cmd_loglevel(const char *args) {
    if (!args || !*args) {
        /* 无参数：显示当前级别 */
        uint32_t cur = klog_get_level();
        char buf[64];
        snprintf(buf, sizeof(buf), "Current log level: %u (%s)\n", cur, level_to_name(cur));
        shell_print(buf);
        shell_last_exit_code = 0;
        return;
    }

    /* 解析并设置级�?*/
    int level = parse_level_arg(args);
    if (level < 0) {
        shell_print("loglevel: invalid level '");
        shell_print(args);
        shell_print("'\nValid levels: 0-7 or emerg/alert/crit/err/warn/notice/info/debug\n");
        shell_last_exit_code = 1;
        return;
    }

    klog_set_level((uint32_t)level);
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "loglevel: set to %u (%s)\n", level, level_to_name((uint32_t)level));
        shell_print(buf);
    }
    shell_last_exit_code = 0;
}

/*
 * cmd_syslog - 控制日志系统的配置和缓冲�?
 *
 * show     显示当前配置（级别、行数）
 * reset    恢复默认级别 KLOG_INFO
 * rotate   将缓冲区 dump 到串口（归档旧日志）
 * flush    清空环形缓冲�?
 */
void cmd_syslog(const char *args) {
    if (!args || !*args) {
        shell_print("Usage: syslog <action>\n");
        shell_print("  show     Print current configuration\n");
        shell_print("  reset    Restore default log level (INFO)\n");
        shell_print("  rotate   Dump ring buffer to serial\n");
        shell_print("  flush    Clear ring buffer\n");
        shell_last_exit_code = 0;
        return;
    }

    if (strcmp(args, "show") == 0) {
        uint32_t lvl = klog_get_level();
        uint32_t lines = klog_get_line_count();
        char buf[96];
        snprintf(buf, sizeof(buf),
                 "Kernel syslog configuration:\n"
                 "  current level:  %u (%s)\n"
                 "  buffered lines:  %u\n",
                 lvl, level_to_name(lvl), lines);
        shell_print(buf);
        shell_last_exit_code = 0;
        return;
    }

    if (strcmp(args, "reset") == 0) {
        klog_set_level(KLOG_INFO);
        shell_print("syslog: log level reset to INFO (6)\n");
        shell_last_exit_code = 0;
        return;
    }

    if (strcmp(args, "rotate") == 0) {
        shell_print("syslog: rotating log (dumping ring buffer to serial)...\n");
        klog_dump_to_serial();
        shell_print("syslog: rotation complete\n");
        shell_last_exit_code = 0;
        return;
    }

    if (strcmp(args, "flush") == 0) {
        shell_print("syslog: flushing ring buffer...\n");
        klog_clear();
        shell_print("syslog: ring buffer flushed\n");
        shell_last_exit_code = 0;
        return;
    }

    shell_print("syslog: unknown action '");
    shell_print(args);
    shell_print("'\nRun 'syslog' with no argument for usage.\n");
    shell_last_exit_code = 1;
}
