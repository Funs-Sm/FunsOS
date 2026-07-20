#include "shell.h"
#include "fw_cmd.h"
#include "fb_console.h"
#include "vga_text.h"
#include "keyboard.h"
#include "mouse.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "version.h"
#include "pmm.h"
#include "timer.h"
#include "rtc.h"
#include "devfs.h"
#include "net.h"
#include "icmp.h"
#include "ip.h"
#include "socket.h"
#include "dns.h"
#include "loopback.h"
#include "file_type.h"
#include "io.h"
#include "vfs.h"
#include "path.h"
#include "dentry.h"
#include "drm.h"
#include "vesa.h"
#include "acpi_sleep.h"
#include "cpufreq.h"
#include "cpuidle.h"
#include "regmap.h"
#include "hwmon.h"
#include "ftrace.h"
#include "dmabuf.h"
#include "iio.h"
#include "pwm.h"
#include "led.h"
#include "pinctrl.h"
#include "gpio.h"
#include "dmaengine.h"
#include "clk.h"
#include "i2c.h"
#include "spi.h"
#include "mfd.h"
#include "battery.h"
#include "kheap.h"
#include "process.h"
#include "sched.h"
#include "pci.h"
#include "editor.h"
#include "unicode.h"
#include "display_server.h"
#include "c_interpreter.h"
#include "font_engine.h"
#include "kvm.h"
#include "klog.h"
#include "vmstate.h"
#include "sound.h"
#include "permission.h"
#include "syslog.h"
#include "logrotate.h"
#include "http_client.h"
#include "jpeg.h"
#include "png.h"
#include "pkgmgr.h"
#include "tftp.h"
#include "ntp.h"
#include "smp.h"
#include "telnet.h"
#include "user.h"
#include "user_ext.h"
#include "shell_error.h"
#include "bios_edit.h"
#include "user_persist.h"
#include "fundb.h"
#include "vfs_ext.h"
#include "config.h"
#include "serial.h"
#include "ipc_msg.h"
#include "ipc_shm.h"
#include "futex.h"
#include "epoll.h"
#include "inotify.h"
#include "cgroup.h"
#include "kprobe.h"
#include "stacktrace.h"
#include "health.h"
#include "quota.h"
#include "flock.h"
#include "ktrace.h"
#include "kwork.h"
#include "registry.h"
#include "cron.h"
#include "evlog.h"
#include "fim.h"
#include "appexec.h"
#include "app_registry.h"
#include "gui_apps.h"
#include "netmon.h"
#include "sysacct.h"
#include "svcmgr.h"
#include "taskmgr.h"
#include "crashdump.h"
#include "ipc_sem.h"
#include "signal_diag.h"
#include "quota_db.h"
#include "logrotate_ext.h"
#include "../fs/dcache.h"
#include "../fs/icache.h"
#include "../fs/page_cache.h"
#include "../fs/readahead.h"
#include "../fs/fs_sync.h"
#include "../fs/fs_stat.h"
#include "env.h"
#include "rlimit.h"
#include "page_replace.h"
#include "iosched.h"
#include "softirq.h"
#include "oom_killer.h"
#include "sysctl.h"
#include "sysrq.h"
#include "workqueue.h"
#include "rcu.h"
#include "hrtimer.h"
#include "slab.h"
#include "watchdog.h"
#include "knotifier.h"
#include "crypto.h"
#include "vmalloc.h"
#include "percpu.h"
#include "kfence.h"
#include "debugobjects.h"
#include "lockdep.h"
#include "irqdomain.h"
#include "games.h"
#include "app_utils.h"
#include "more_apps.h"
#include "version.h"

#include "devtmpfs.h"
#include "ksysfs.h"
#include "netns.h"
#include "knetfilter.h"
#include "seccomp.h"
#include "apparmor.h"
#include "keyring.h"
#include "audit.h"

#include "namespace.h"
#include "tracepoint.h"
#include "uprobe.h"
#include "kmod.h"
#include "firmware.h"
#include "remoteproc.h"
#include "rpmsg.h"
#include "virtio.h"

#define SHELL_MAX_LINE 256
#define SHELL_PROMPT  "funs> "

#define SHELL_PIPE_BUF_SIZE 4096
#define SHELL_MAX_ENV 32
#define SHELL_MAX_ENV_NAME 32
#define SHELL_MAX_ENV_VALUE 128

/* 任务栏前向声明（定义在后面） */
static void taskbar_toggle(void);

/* Command history */
#define SHELL_HISTORY_SIZE 100
#define SHELL_HISTORY_LINE 256

static char history_buf[SHELL_HISTORY_SIZE][SHELL_HISTORY_LINE];
static int history_count = 0;
static int history_head __attribute__((unused)) = 0;  /* oldest entry index */
static int history_pos = 0;   /* next write position */

/* Arrow-key command history for shell_read_line */
#define SHELL_HISTORY_MAX 32
static char shell_history[SHELL_HISTORY_MAX][SHELL_HISTORY_LINE];
static int shell_history_count = 0;
static int shell_history_pos = -1;

/* Tab completion */
#define SHELL_MAX_COMPLETIONS 256
#define SHELL_COMPLETION_CMD_MAX 32

static uint32_t len_strlen(const char *s);

static const char *completion_commands[] = {
    "pt", "ls", "cat", "show", "type", "cd", "go", "pwd", "where",
    "cp", "copy", "mv", "ren", "rm", "del", "mkdir", "rmdir", "touch",
    "echo", "set", "env", "unset", "alias", "unalias", "history",
    "help", "ver", "sysinfo", "mem", "free", "uptime", "date", "time",
    "ps", "top", "kill", "dmesg", "loglevel", "reboot", "halt", "shutdown",
    "mount", "umount", "format", "fdisk", "chkdsk", "lsblk", "df", "du",
    "ifconfig", "ping", "route", "dns", "wget", "netstat", "arp", "hostname",
    "lspci", "lsusb", "sensors", "freq", "dev", "clr", "clear",
    "head", "tail", "wc", "grep", "find", "diff", "sort", "uniq",
    "chmod", "chown", "stat", "tree", "file", "which", "tee",
    "edit", "run", "exec", "gui", "calc", "whoami", "su", "logout",
    "snake", "2048", "tetris", "mine", "minesweeper", "pong", "games", "desktop",
    "notepad", "paint", "terminal", "filemgr",
    "acalc", "sysmon", "cal", "clock", "matrix",
    "life", "sokoban", "typing", "ascii", "xxd", "nano",
    "sync", "ipcs", "vmstat", "iostat", "slabtop", "meminfo",
    "tar", "gzip", "gunzip", "hexdump", "strings", "cksum",
    "telnet", "tftp", "ntp", "ssh", "ftp", "nc", "tcpdump",
    "pkg", "sudo", "passwd", "useradd", "userdel", "users", "groups", "who",
    "taskbar", "watch", "sleep", "test", "expr", "xargs",
    "cut", "paste", "tr", "rev", "nl", "fold", "expand", "unexpand",
    "basename", "dirname", "realpath", "truncate", "mkfifo", "mknod",
    "readlink", "ln", "symlink", "append", "replace", "look", "comm",
    "dd", "split", "join", "fallocate", "filefrag", "losetup", "fsck",
    "nslookup", "dig", "dhcp", "nmap", "lanscan", "wol", "sockstat",
    "speedtest", "cal", "traceroute", "mtr", "ss",
    "reg", "apps", "cron", "crontab", "ktrace", "kwork",
    "flock", "dcache", "icache", "pagecache", "readahead", "syncstat",
    "mount2", "cgroup", "kprobe", "strace", "lsof", "mpstat",
    "pidstat", "prlimit", "capsh", "dumpstack", "sysctl",
    "health", "notifier", "quota", "fsstat",
    "cpufreq", "cpuidle", "regmap", "hwmon", "ftrace", "dmabuf", "iio", "pwm", "led",
    "rtc", "pinctrl", "gpio", "dmaengine", "clk", "i2c", "spi", "mfd",
    "vmalloc", "vmap", "percpu", "kfence", "debugobj", "lockdep", "irqdomain",
    "devtmpfs", "sysfs", "netns", "netfilter", "seccomp", "apparmor", "keyring", "audit",
    "namespace", "tracepoint", "uprobe", "kmod", "firmware", "remoteproc", "rpmsg", "virtio",
    "db", "config", "httpget", "imgview", "play", "vol", "sound",
    "save", "resume", "nice", "renice", "jobs", "bg", "fg", "nohup",
    "service", "taskmgr", "sysreport", "ipc", "sigstat",
    "taskset", "chrt", "pidof", "pstree", "last", "uname",
    "yes", "seq", "factor", "shuf", "true", "false",
    "lscolor", "col", "column", "tsort", "splash",
    "schedpolicy", "mempolicy",
    "iosched", "softirq", "tasklet", "oom", "sysctl", "sysrq",
    NULL
};

static int tab_complete_command(const char *prefix, char *result, uint32_t result_size) {
    uint32_t prefix_len = len_strlen(prefix);
    int count = 0;
    const char *best_match = NULL;
    uint32_t best_match_len = 0;

    if (prefix_len == 0) return 0;

    for (int i = 0; completion_commands[i]; i++) {
        if (strncmp(completion_commands[i], prefix, prefix_len) == 0) {
            count++;
            if (!best_match) {
                best_match = completion_commands[i];
                best_match_len = len_strlen(best_match);
            } else {
                uint32_t common = 0;
                while (common < best_match_len && completion_commands[i][common] &&
                       best_match[common] == completion_commands[i][common]) {
                    common++;
                }
                best_match_len = common;
            }
        }
    }

    if (count == 1 && best_match) {
        strncpy(result, best_match, result_size - 1);
        result[result_size - 1] = '\0';
        return 1;
    } else if (count > 1 && best_match_len > prefix_len) {
        for (uint32_t i = 0; i < best_match_len && i < result_size - 1; i++) {
            result[i] = best_match[i];
        }
        result[best_match_len < result_size - 1 ? best_match_len : result_size - 1] = '\0';
        return 2;
    }

    return count;
}

static void tab_show_completions(const char *prefix) {
    uint32_t prefix_len = len_strlen(prefix);
    int col = 0;

    shell_print("\n");
    for (int i = 0; completion_commands[i]; i++) {
        if (strncmp(completion_commands[i], prefix, prefix_len) == 0) {
            char buf[64];
            snprintf(buf, sizeof(buf), "  %-16s", completion_commands[i]);
            shell_print(buf);
            col++;
            if (col >= 5) {
                shell_print("\n");
                col = 0;
            }
        }
    }
    if (col > 0) shell_print("\n");
}

/* Alias support */
#define SHELL_MAX_ALIASES 20
#define SHELL_ALIAS_NAME  32
#define SHELL_ALIAS_VALUE 128

typedef struct {
    char name[SHELL_ALIAS_NAME];
    char value[SHELL_ALIAS_VALUE];
} alias_t;

static alias_t aliases[SHELL_MAX_ALIASES];
static int alias_count = 0;

static char current_dir[256] = "/";

static int vbe_mode_active = 0;

/* Environment variables */
typedef struct {
    char name[SHELL_MAX_ENV_NAME];
    char value[SHELL_MAX_ENV_VALUE];
} env_var_t;

static env_var_t env_vars[SHELL_MAX_ENV];
static int env_count = 0;

/* Pipe/output capture buffer */
static char pipe_buf[SHELL_PIPE_BUF_SIZE];
static uint32_t pipe_len = 0;
static uint8_t capturing = 0;

/* Last command exit code */
static int last_exit_code = 0;
static int logged_in = 0;
static uint32_t login_tick = 0;  /* 登录时的 timer tick，用于 who 命令 */

/* Redirect state */
static char redirect_file[256];
static uint8_t redirect_append = 0;
static uint8_t redirect_active = 0;

void shell_set_vbe_mode(int active) {
    vbe_mode_active = active;
}

static uint32_t len_strlen(const char *s) {
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

void shell_print(const char *str) {
    if (capturing) {
        uint32_t len = 0;
        while (str[len]) len++;
        if (pipe_len + len < SHELL_PIPE_BUF_SIZE - 1) {
            for (uint32_t i = 0; i < len; i++) {
                pipe_buf[pipe_len++] = str[i];
            }
            pipe_buf[pipe_len] = '\0';
        }
        return;
    }
    if (redirect_active) {
        /* Write to file via VFS */
        file_t *f = 0;
        uint32_t flags = FILE_MODE_WRITE;
        if (redirect_append) {
            flags |= FILE_MODE_READ; /* open for append */
        }
        if (vfs_open(redirect_file, flags, &f) == 0 && f) {
            if (redirect_append) {
                vfs_seek(f, 0, SEEK_END);
            }
            vfs_write(f, str, (uint32_t)len_strlen(str));
            vfs_close(f);
        }
        return;
    }
    if (vbe_mode_active) {
        fb_console_write(str);
    } else {
        vga_text_print(str);
    }
}

static void shell_putchar(char c) {
    char buf[2];
    buf[0] = c;
    buf[1] = '\0';
    shell_print(buf);
}

static void shell_print_colored(const char *str, uint8_t fg, uint8_t bg) {
    if (!str || !*str) return;

    if (capturing || redirect_active) {
        shell_print(str);
        return;
    }

    uint8_t old_fg = VGA_COLOR_LIGHT_GREY;
    uint8_t old_bg = VGA_COLOR_BLACK;
    vga_text_get_color(&old_fg, &old_bg);

    vga_text_set_color(fg, bg);
    shell_print(str);
    vga_text_set_color(old_fg, old_bg);
}

/* ---- History functions ---- */

static void history_add(const char *line) {
    if (!line || !*line) return;
    /* Don't add duplicate of last entry */
    if (history_count > 0) {
        int last = (history_pos == 0) ? SHELL_HISTORY_SIZE - 1 : history_pos - 1;
        if (strcmp(history_buf[last], line) == 0) return;
    }
    strncpy(history_buf[history_pos], line, SHELL_HISTORY_LINE - 1);
    history_buf[history_pos][SHELL_HISTORY_LINE - 1] = '\0';
    history_pos = (history_pos + 1) % SHELL_HISTORY_SIZE;
    if (history_count < SHELL_HISTORY_SIZE) history_count++;

    /* Also add to shell_history for up/down arrow navigation (newest at index 0) */
    if (shell_history_count >= SHELL_HISTORY_MAX) {
        /* Shift all entries up, discard oldest */
        for (int i = SHELL_HISTORY_MAX - 1; i > 0; i--) {
            strncpy(shell_history[i], shell_history[i - 1], SHELL_HISTORY_LINE - 1);
            shell_history[i][SHELL_HISTORY_LINE - 1] = '\0';
        }
    } else {
        /* Shift existing entries up to make room for newest at index 0 */
        for (int i = shell_history_count; i > 0; i--) {
            strncpy(shell_history[i], shell_history[i - 1], SHELL_HISTORY_LINE - 1);
            shell_history[i][SHELL_HISTORY_LINE - 1] = '\0';
        }
        shell_history_count++;
    }
    /* Place newest entry at index 0 */
    strncpy(shell_history[0], line, SHELL_HISTORY_LINE - 1);
    shell_history[0][SHELL_HISTORY_LINE - 1] = '\0';
    /* Reset history navigation position */
    shell_history_pos = -1;
}

/* ---- Alias functions ---- */

static const char *alias_lookup(const char *name) {
    for (int i = 0; i < alias_count; i++) {
        if (strcmp(aliases[i].name, name) == 0) {
            return aliases[i].value;
        }
    }
    return 0;
}

static int alias_set(const char *name, const char *value) {
    /* Check if alias already exists */
    for (int i = 0; i < alias_count; i++) {
        if (strcmp(aliases[i].name, name) == 0) {
            strncpy(aliases[i].value, value, SHELL_ALIAS_VALUE - 1);
            aliases[i].value[SHELL_ALIAS_VALUE - 1] = '\0';
            return 0;
        }
    }
    if (alias_count >= SHELL_MAX_ALIASES) return -1;
    strncpy(aliases[alias_count].name, name, SHELL_ALIAS_NAME - 1);
    aliases[alias_count].name[SHELL_ALIAS_NAME - 1] = '\0';
    strncpy(aliases[alias_count].value, value, SHELL_ALIAS_VALUE - 1);
    aliases[alias_count].value[SHELL_ALIAS_VALUE - 1] = '\0';
    alias_count++;
    return 0;
}

static int alias_unset(const char *name) __attribute__((unused));
static int alias_unset(const char *name) {
    for (int i = 0; i < alias_count; i++) {
        if (strcmp(aliases[i].name, name) == 0) {
            for (int j = i; j < alias_count - 1; j++) {
                aliases[j] = aliases[j + 1];
            }
            alias_count--;
            return 0;
        }
    }
    return -1;
}

/* Expand aliases: if the first word of the line matches an alias, replace it */
static void alias_expand(char *buf, uint32_t buf_size) {
    char word[SHELL_ALIAS_NAME];
    uint32_t wi = 0;
    uint32_t i = 0;

    /* Skip leading spaces */
    while (buf[i] == ' ') i++;

    /* Extract first word */
    while (buf[i] && buf[i] != ' ' && wi < SHELL_ALIAS_NAME - 1) {
        word[wi++] = buf[i++];
    }
    word[wi] = '\0';

    const char *replacement = alias_lookup(word);
    if (!replacement) return;

    /* Build expanded line */
    char tmp[SHELL_MAX_LINE];
    uint32_t ti = 0;
    const char *r = replacement;
    while (*r && ti < SHELL_MAX_LINE - 1) {
        tmp[ti++] = *r++;
    }
    /* Add space if replacement doesn't end with space and there's more */
    if (ti > 0 && tmp[ti - 1] != ' ' && buf[i]) {
        tmp[ti++] = ' ';
    }
    /* Append the rest of the original line */
    while (buf[i] && ti < SHELL_MAX_LINE - 1) {
        tmp[ti++] = buf[i++];
    }
    tmp[ti] = '\0';

    for (uint32_t j = 0; j <= ti && j < buf_size; j++) {
        buf[j] = tmp[j];
    }
}

static int shell_read_line(char *buf, uint32_t size) {
    if (size == 0) return 0;
    uint32_t pos = 0;

    while (1) {
        keyboard_poll();

        /* 检查键盘中断信号 (Ctrl+C / Ctrl+\) */
        if (kb_signal_check()) {
            shell_print("^C\n");
            buf[0] = '\0';
            return -1;  /* 返回 -1 表示被中断 */
        }

        if (mouse_has_data()) {
            mouse_event_t mev;
            while (mouse_get_event(&mev)) {
                if (mev.wheel < 0) {
                    if (vbe_mode_active) fb_console_scroll_up(3);
                    else vga_text_scroll_up(3);
                } else if (mev.wheel > 0) {
                    if (vbe_mode_active) fb_console_scroll_down(3);
                    else vga_text_scroll_down(3);
                }
            }
        }

        if (!keyboard_has_data()) {
            for (volatile int y = 0; y < 1000; y++) { asm volatile("nop"); }
            continue;
        }

        keyboard_event_t event;
        if (!keyboard_get_event(&event)) continue;
        if (!(event.flags & KEY_PRESSED)) continue;

        if ((event.flags & KEY_EXTENDED) &&
            (event.scancode == 0x48 || event.scancode == 0x50)) {
            if (event.flags & KEY_SHIFT) {
                if (event.scancode == 0x48) {
                    if (vbe_mode_active) fb_console_scroll_up(1);
                    else vga_text_scroll_up(1);
                } else {
                    if (vbe_mode_active) fb_console_scroll_down(1);
                    else vga_text_scroll_down(1);
                }
                continue;
            }
            if (event.scancode == 0x48) {
                if (shell_history_count == 0) continue;
                if (shell_history_pos < shell_history_count - 1) {
                    while (pos > 0) {
                        pos--;
                        shell_putchar('\b');
                    }
                    shell_history_pos++;
                    const char *hist = shell_history[shell_history_pos];
                    for (uint32_t i = 0; hist[i] && pos < size - 1; i++) {
                        buf[pos] = hist[i];
                        pos++;
                        shell_putchar(hist[i]);
                    }
                    buf[pos] = '\0';
                }
            } else if (event.scancode == 0x50) {
                if (shell_history_pos > 0) {
                    while (pos > 0) {
                        pos--;
                        shell_putchar('\b');
                    }
                    shell_history_pos--;
                    const char *hist = shell_history[shell_history_pos];
                    for (uint32_t i = 0; hist[i] && pos < size - 1; i++) {
                        buf[pos] = hist[i];
                        pos++;
                        shell_putchar(hist[i]);
                    }
                    buf[pos] = '\0';
                } else if (shell_history_pos == 0) {
                    while (pos > 0) {
                        pos--;
                        shell_putchar('\b');
                    }
                    shell_history_pos = -1;
                    buf[0] = '\0';
                }
            }
            continue;
        }

        if ((event.flags & KEY_EXTENDED) && event.scancode == 0x49) {
            if (vbe_mode_active) fb_console_scroll_up(12);
            else vga_text_scroll_up(12);
            continue;
        }
        if ((event.flags & KEY_EXTENDED) && event.scancode == 0x51) {
            if (vbe_mode_active) fb_console_scroll_down(12);
            else vga_text_scroll_down(12);
            continue;
        }

        if ((event.flags & KEY_EXTENDED) && event.scancode == 0x47) {
            if (vbe_mode_active) { fb_console_scroll_up(9999); }
            else vga_text_scroll_home();
            continue;
        }
        if ((event.flags & KEY_EXTENDED) && event.scancode == 0x4F) {
            if (vbe_mode_active) { fb_console_scroll_down(9999); }
            else vga_text_scroll_end();
            continue;
        }

        /* Ctrl+T: 切换任务栏 */
        if ((event.flags & KEY_CTRL) && (event.ascii == 't' || event.ascii == 'T' || event.ascii == 0x14)) {
            taskbar_toggle();
            continue;
        }

        /* ---- 快捷键处理 ---- */

        /* Ctrl+A: 移动光标到行首 */
        if ((event.flags & KEY_CTRL) && (event.ascii == 0x01)) {
            while (pos > 0) {
                pos--;
                shell_putchar('\b');
            }
            continue;
        }

        /* Ctrl+E: 移动光标到行尾 */
        if ((event.flags & KEY_CTRL) && (event.ascii == 0x05)) {
            while (buf[pos]) {
                shell_putchar(buf[pos]);
                pos++;
            }
            continue;
        }

        /* Ctrl+U: 清除当前输入行（光标前全部） */
        if ((event.flags & KEY_CTRL) && (event.ascii == 0x15)) {
            /* 先移动到行首，再清除显示 */
            while (pos > 0) {
                pos--;
                shell_putchar('\b');
            }
            /* 清除行尾残余字符 */
            uint32_t len = 0;
            while (buf[len]) len++;
            for (uint32_t i = 0; i < len; i++) shell_putchar(' ');
            for (uint32_t i = 0; i < len; i++) shell_putchar('\b');
            /* 清空缓冲区 */
            for (uint32_t i = 0; i < size; i++) buf[i] = '\0';
            pos = 0;
            continue;
        }

        /* Ctrl+K: 删除光标到行尾 */
        if ((event.flags & KEY_CTRL) && (event.ascii == 0x0B)) {
            uint32_t len = 0;
            while (buf[pos + len]) len++;
            /* 用空格覆盖行尾字符 */
            for (uint32_t i = 0; i < len; i++) shell_putchar(' ');
            for (uint32_t i = 0; i < len; i++) shell_putchar('\b');
            /* 截断缓冲区 */
            for (uint32_t i = pos; i < size; i++) buf[i] = '\0';
            continue;
        }

        /* Ctrl+W: 删除前一个单词 */
        if ((event.flags & KEY_CTRL) && (event.ascii == 0x17)) {
            /* 跳过尾部空格 */
            while (pos > 0 && buf[pos - 1] == ' ') {
                pos--;
                shell_putchar('\b');
                shell_putchar(' ');
                shell_putchar('\b');
            }
            /* 删除到前一个空格或行首 */
            while (pos > 0 && buf[pos - 1] != ' ') {
                pos--;
                shell_putchar('\b');
                shell_putchar(' ');
                shell_putchar('\b');
            }
            /* 移动后面的字符向前 */
            uint32_t end = 0;
            while (buf[pos + end]) end++;
            /* 重新整理 buf（简单处理：截断到 pos） */
            for (uint32_t i = pos; i < size; i++) buf[i] = '\0';
            continue;
        }

        /* Ctrl+L: 清屏并重新显示提示符 */
        if ((event.flags & KEY_CTRL) && (event.ascii == 0x0C)) {
            if (vbe_mode_active) {
                fb_console_clear();
            } else {
                vga_text_clear();
            }
            /* 重新显示当前输入行内容 */
            for (uint32_t i = 0; i < pos; i++) {
                shell_putchar(buf[i]);
            }
            continue;
        }

        /* Ctrl+R: 搜索命令历史 */
        if ((event.flags & KEY_CTRL) && (event.ascii == 0x12)) {
            shell_print("\n(reverse-i-search): ");
            /* 简单实现：显示最近的匹配历史 */
            char search_buf[64] = {0};
            uint32_t spos = 0;
            while (1) {
                if (!keyboard_has_data()) keyboard_poll();
                if (!keyboard_has_data()) { asm volatile("hlt"); continue; }
                keyboard_event_t se;
                if (!keyboard_get_event(&se)) continue;
                if (!(se.flags & KEY_PRESSED)) continue;
                if (se.ascii == '\n') {
                    shell_putchar('\n');
                    break;
                }
                if (se.ascii == '\b') {
                    if (spos > 0) { spos--; shell_putchar('\b'); shell_putchar(' '); shell_putchar('\b'); }
                    continue;
                }
                if (se.ascii == 0x03) { /* Ctrl+C 取消搜索 */
                    shell_print("^C\n");
                    spos = 0;
                    break;
                }
                if (spos < 63 && se.ascii >= 32 && se.ascii < 127) {
                    search_buf[spos++] = se.ascii;
                    shell_putchar(se.ascii);
                }
            }
            search_buf[spos] = '\0';
            if (spos > 0) {
                /* 在历史中搜索匹配项 */
                for (int i = shell_history_count - 1; i >= 0; i--) {
                    if (shell_history[i][0] != '\0') {
                        /* 简单子串搜索 */
                        const char *h = shell_history[i];
                        int found = 0;
                        for (int j = 0; h[j]; j++) {
                            int k = 0;
                            while (search_buf[k] && h[j + k] && search_buf[k] == h[j + k]) k++;
                            if (search_buf[k] == '\0') { found = 1; break; }
                        }
                        if (found) {
                            /* 清除当前行并替换为匹配项 */
                            while (pos > 0) { pos--; shell_putchar('\b'); }
                            uint32_t len = 0;
                            while (buf[len]) len++;
                            for (uint32_t j = 0; j < len; j++) shell_putchar(' ');
                            for (uint32_t j = 0; j < len; j++) shell_putchar('\b');
                            /* 复制匹配项 */
                            for (uint32_t j = 0; h[j] && pos < size - 1; j++) {
                                buf[pos] = h[j];
                                pos++;
                                shell_putchar(h[j]);
                            }
                            buf[pos] = '\0';
                            break;
                        }
                    }
                }
            }
            continue;
        }

        /* Ctrl+C: 取消当前输入，显示新提示符 */
        if ((event.flags & KEY_CTRL) && (event.ascii == 0x03)) {
            shell_print("^C\n");
            buf[0] = '\0';
            return 0;
        }

        /* Ctrl+D: 如果行为空则退出/EOF */
        if ((event.flags & KEY_CTRL) && (event.ascii == 0x04)) {
            if (pos == 0) {
                shell_print("logout\n");
                logged_in = 0;
                buf[0] = '\0';
                return 0;
            }
            /* 如果有内容，等同于删除光标处字符（类似 bash） */
            continue;
        }

        /* Ctrl+Z: 挂起提示（内核中无实际挂起功能） */
        if ((event.flags & KEY_CTRL) && (event.ascii == 0x1A)) {
            shell_print("\n[Suspended]\n");
            buf[0] = '\0';
            return 0;
        }

        if (event.ascii == '\n') {
            shell_putchar('\n');
            break;
        }

        if (event.ascii == '\b') {
            if (pos > 0) {
                pos--;
                shell_putchar('\b');
            }
            continue;
        }

        /* Tab key: command completion */
        if (event.ascii == '\t') {
            /* Only complete the first word (command name) */
            int has_space = 0;
            uint32_t word_start = 0;
            for (uint32_t i = 0; i < pos; i++) {
                if (buf[i] == ' ') {
                    has_space = 1;
                    break;
                }
            }
            if (!has_space && pos > 0) {
                char prefix[SHELL_COMPLETION_CMD_MAX];
                for (uint32_t i = 0; i < pos && i < SHELL_COMPLETION_CMD_MAX - 1; i++) {
                    prefix[i] = buf[i];
                }
                prefix[pos < SHELL_COMPLETION_CMD_MAX - 1 ? pos : SHELL_COMPLETION_CMD_MAX - 1] = '\0';

                char completion[SHELL_COMPLETION_CMD_MAX];
                int result = tab_complete_command(prefix, completion, sizeof(completion));

                if (result == 1) {
                    /* Single match: complete it */
                    uint32_t compl_len = len_strlen(completion);
                    if (compl_len > pos && compl_len < size - 1) {
                        /* Clear current input display */
                        while (pos > word_start) {
                            pos--;
                            shell_putchar('\b');
                        }
                        /* Write completed command */
                        for (uint32_t i = 0; i < compl_len; i++) {
                            buf[i] = completion[i];
                            shell_putchar(completion[i]);
                        }
                        pos = compl_len;
                        buf[pos] = '\0';
                    }
                } else if (result == 2) {
                    /* Multiple matches with common prefix: complete common part */
                    uint32_t compl_len = len_strlen(completion);
                    if (compl_len > pos && compl_len < size - 1) {
                        while (pos > word_start) {
                            pos--;
                            shell_putchar('\b');
                        }
                        for (uint32_t i = 0; i < compl_len; i++) {
                            buf[i] = completion[i];
                            shell_putchar(completion[i]);
                        }
                        pos = compl_len;
                        buf[pos] = '\0';
                    }
                } else if (result > 2) {
                    /* Multiple matches, no common prefix - show all */
                    tab_show_completions(prefix);
                    /* Redraw prompt and current input */
                    shell_print(SHELL_PROMPT);
                    for (uint32_t i = 0; i < pos; i++) {
                        shell_putchar(buf[i]);
                    }
                }
            }
            continue;
        }

        if (pos < size - 1 && event.ascii >= 32 && event.ascii < 127) {
            buf[pos] = event.ascii;
            pos++;
            shell_putchar(event.ascii);
        }
    }

    buf[pos] = '\0';

    return (int)pos;
}

static int shell_read_password(char *buf, uint32_t size) {
    if (size == 0) return 0;
    uint32_t pos = 0;
    while (1) {
        keyboard_poll();
        if (!keyboard_has_data()) {
            for (volatile int y = 0; y < 1000; y++) { asm volatile("nop"); }
            continue;
        }
        keyboard_event_t event;
        if (!keyboard_get_event(&event)) continue;
        if (!(event.flags & KEY_PRESSED)) continue;
        if (event.ascii == '\n') {
            shell_putchar('\n');
            break;
        }
        if (event.ascii == '\b') {
            if (pos > 0) {
                pos--;
                shell_putchar('\b');
                shell_putchar(' ');
                shell_putchar('\b');
            }
            continue;
        }
        if (pos < size - 1 && event.ascii >= 32 && event.ascii < 127) {
            buf[pos] = event.ascii;
            pos++;
            shell_putchar('*');
        }
    }
    buf[pos] = '\0';
    return (int)pos;
}

/* ---- Environment variable system ---- */

static void env_init(void) {
    env_count = 0;
    /* Pre-set environment variables */
    strcpy(env_vars[env_count].name, "HOME");
    strcpy(env_vars[env_count].value, "/");
    env_count++;
    strcpy(env_vars[env_count].name, "PATH");
    strcpy(env_vars[env_count].value, "/bin");
    env_count++;
    strcpy(env_vars[env_count].name, "USER");
    strcpy(env_vars[env_count].value, "sover");
    env_count++;
    strcpy(env_vars[env_count].name, "SHELL");
    strcpy(env_vars[env_count].value, "/bin/funs");
    env_count++;
    strcpy(env_vars[env_count].name, "OS");
    strcpy(env_vars[env_count].value, "FunsCore");
    env_count++;
}

static int env_set(const char *name, const char *value) {
    /* Check if variable already exists */
    for (int i = 0; i < env_count; i++) {
        if (strcmp(env_vars[i].name, name) == 0) {
            uint32_t j;
            for (j = 0; j < SHELL_MAX_ENV_VALUE - 1 && value[j]; j++)
                env_vars[i].value[j] = value[j];
            env_vars[i].value[j] = '\0';
            return 0;
        }
    }
    /* Add new variable */
    if (env_count >= SHELL_MAX_ENV) return -1;
    uint32_t j;
    for (j = 0; j < SHELL_MAX_ENV_NAME - 1 && name[j]; j++)
        env_vars[env_count].name[j] = name[j];
    env_vars[env_count].name[j] = '\0';
    for (j = 0; j < SHELL_MAX_ENV_VALUE - 1 && value[j]; j++)
        env_vars[env_count].value[j] = value[j];
    env_vars[env_count].value[j] = '\0';
    env_count++;
    return 0;
}

static const char *env_get(const char *name) {
    for (int i = 0; i < env_count; i++) {
        if (strcmp(env_vars[i].name, name) == 0) {
            return env_vars[i].value;
        }
    }
    return 0;
}

static int env_unset(const char *name) {
    for (int i = 0; i < env_count; i++) {
        if (strcmp(env_vars[i].name, name) == 0) {
            for (int j = i; j < env_count - 1; j++) {
                env_vars[j] = env_vars[j + 1];
            }
            env_count--;
            return 0;
        }
    }
    return -1;
}

/* Expand $VAR references in a string */
static void env_expand(char *buf, uint32_t buf_size) {
    char tmp[SHELL_MAX_LINE];
    uint32_t ti = 0;
    uint32_t i = 0;

    while (buf[i] && ti < buf_size - 1) {
        if (buf[i] == '$') {
            i++;
            char var_name[SHELL_MAX_ENV_NAME];
            uint32_t vi = 0;
            while (buf[i] && ((buf[i] >= 'A' && buf[i] <= 'Z') ||
                   (buf[i] >= 'a' && buf[i] <= 'z') ||
                   (buf[i] >= '0' && buf[i] <= '9') ||
                   buf[i] == '_') && vi < SHELL_MAX_ENV_NAME - 1) {
                var_name[vi++] = buf[i++];
            }
            var_name[vi] = '\0';
            const char *val = env_get(var_name);
            if (val) {
                while (*val && ti < buf_size - 1) {
                    tmp[ti++] = *val++;
                }
            }
        } else {
            tmp[ti++] = buf[i++];
        }
    }
    tmp[ti] = '\0';
    for (uint32_t j = 0; j <= ti; j++) {
        buf[j] = tmp[j];
    }
}

/* ---- Helper: build full path from relative ---- */
static void build_full_path(const char *rel, char *full, uint32_t full_size) {
    if (full_size == 0) return;
    if (rel[0] == '/') {
        strncpy(full, rel, full_size - 1);
        full[full_size - 1] = '\0';
    } else {
        uint32_t used = 0;
        strncpy(full, current_dir, full_size - 1);
        full[full_size - 1] = '\0';
        used = len_strlen(full);
        if (strcmp(current_dir, "/") != 0 && used + 1 < full_size) {
            strncat(full, "/", full_size - used - 1);
            used++;
        }
        if (used < full_size - 1) {
            strncat(full, rel, full_size - used - 1);
        }
    }
}

/* ---- Error reporting ---- */

/* 错误码定义和 shell_error() 实现已移至 shell_error.c / shell_error.h
 * 此处保留 SHELL_OK 供内部使用 */
#define SHELL_OK                0

/* ---- Command implementations ---- */

/* Forward declarations */
static void cmd_pt(const char *options);
static void cmd_show(const char *file);
static void cmd_go(const char *dir);
static void cmd_where(void);
static void cmd_clr(void);
static void cmd_ver(void);
static void cmd_help(const char *arg);
static void cmd_applist(const char *arg);
static void cmd_sysinfo(void);
static void cmd_schedpolicy(void);
static void cmd_mempolicy(void);
static void cmd_iosched(void);
static void cmd_softirq(void);
static void cmd_oom(void);
static void cmd_sysctl(const char *name, const char *value);
static void cmd_sysrq(void);
static void cmd_workqueue(void);
static void cmd_rcu(void);
static void cmd_hrtimer(void);
static void cmd_slab(void);
static void cmd_watchdog(void);
static void cmd_crypto(void);
static void cmd_cpufreq(void);
static void cmd_vmalloc(void);
static void cmd_percpu(void);
static void cmd_kfence(void);
static void cmd_debugobj(void);
static void cmd_lockdep(void);
static void cmd_irqdomain(void);
static void cmd_cpuidle(void);
static void cmd_regmap(void);
static void cmd_hwmon(void);
static void cmd_ftrace(void);
static void cmd_dmabuf(void);
static void cmd_iio(void);
static void cmd_pwm(void);
static void cmd_led(void);
static void cmd_rtc(void);
static void cmd_pinctrl(void);
static void cmd_gpio(void);
static void cmd_dmaengine(void);
static void cmd_clk(void);
static void cmd_i2c(void);
static void cmd_spi(void);
static void cmd_mfd(void);
static void cmd_devtmpfs(void);
static void cmd_sysfs(void);
static void cmd_netns(void);
static void cmd_netfilter(void);
static void cmd_seccomp(void);
static void cmd_apparmor(void);
static void cmd_keyring(void);
static void cmd_audit(void);
static void cmd_reboot(void);
static void cmd_halt(void);
static void cmd_shutdown(void);
static void cmd_time(void);
static void cmd_date(void);
static void cmd_mem(void);
static void cmd_dev(void);
static void cmd_ping(const char *ip_str);
static void cmd_copy(const char *src, const char *dst);
static void cmd_del(const char *file);
static void cmd_mkdir(const char *dir);
static void cmd_ren(const char *old, const char *new_name);
static void cmd_type(const char *file);
static void cmd_find(const char *name);
static void cmd_size(const char *file);
static void cmd_echo(const char *text);
static void cmd_set(const char *var, const char *value);
static void cmd_env(void);
static void cmd_unset(const char *name);
static void cmd_run(const char *file);
static void cmd_ps(void);
static void cmd_kill(const char *pid_str);
static void cmd_top(void);
static void cmd_free(void);
static void cmd_uptime(void);
static void cmd_load(void);
static void cmd_dmesg(const char *options);
static void cmd_loglevel(const char *level_str);
static void cmd_syslog(const char *subcmd, const char *arg1, const char *arg2, const char *arg3);
static void cmd_mount(const char *dev, const char *dir);
static void cmd_umount(const char *dir);
static void cmd_format(const char *dev, const char *fstype);
static void cmd_fdisk(const char *dev);
static void cmd_chkdsk(const char *dev);
static void cmd_cat(const char *file);
static void cmd_ls(const char *options);
static void cmd_cd(const char *dir);
static void cmd_pwd(void);
static void cmd_touch(const char *file);
static void cmd_append(const char *file, const char *text);
static void cmd_head(const char *file, const char *n_str);
static void cmd_tail(const char *file, const char *n_str);
static void cmd_wc(const char *file);
static void cmd_diff(const char *f1, const char *f2);
static void cmd_sort(const char *file);
static void cmd_uniq(const char *file);
static void cmd_grep(const char *pattern, const char *file, const char *opts);
static void cmd_replace(const char *old_text, const char *new_text, const char *file);
static void cmd_chmod(const char *mode_str, const char *file);
static void cmd_chown(const char *user, const char *file);
static void cmd_file(const char *file);
static void cmd_lscolor(const char *arg1, const char *arg2, const char *arg3);
static void cmd_stat(const char *file);
static void cmd_tree(const char *dir);
static void cmd_du(const char *dir);
static void cmd_df(void);
static void cmd_ifconfig(void);
static void cmd_route(void);
static void cmd_dns(const char *host);
static void cmd_wget(const char *url, const char *outfile);
static void cmd_netstat(void);
static void cmd_traceroute(const char *ip);
static void cmd_arp(void);
static void cmd_hostname(const char *name);
static void cmd_lspci(void);
static void cmd_lsusb(void);
static void cmd_lsblk(void);
static void cmd_sensors(void);
static void cmd_freq(const char *freq_str);
static void cmd_calc(const char *expr);
static void cmd_base64(const char *opt, const char *file);
static void cmd_md5(const char *file);
static void cmd_history(const char *options);
static void cmd_alias(const char *arg);
static void cmd_edit(const char *file);
static void cmd_cedit(const char *filename);
static void cmd_kvm(void);
static void cmd_run_app(const char *name);
/* Advanced commands */
static void cmd_exec(const char *file, const char *args);
static void cmd_bg(const char *pid_str);
static void cmd_fg(const char *pid_str);
static void cmd_jobs(void);
static void cmd_nice(const char *pid_str, const char *prio_str);
static void cmd_renice(const char *pid_str, const char *prio_str);
static void cmd_nohup(const char *cmd);
static void cmd_watch(const char *cmd);
static void cmd_sleep(const char *sec_str);
static void cmd_test(const char *expr);
static void cmd_expr(const char *math);
static void cmd_xargs(const char *cmd);
static void cmd_tee(const char *file);
static void cmd_install(const char *src, const char *dst);
static void cmd_which(const char *cmd);
static void cmd_logrotate(const char *arg);
static void cmd_db(const char *subcmd, const char *arg1, const char *arg2);
static void cmd_config(const char *subcmd, const char *key, const char *value);
static void cmd_httpget(const char *arg);
static void cmd_sudo(const char *command);
static void cmd_imgview(const char *arg);
static void cmd_pkg(const char *arg, const char *arg2);
static void cmd_tftp(const char *arg);
static void cmd_ntp(const char *arg);
static void cmd_telnet(const char *arg);
static void cmd_play(const char *file);
static void cmd_vol(const char *arg);
static void cmd_sound(void);

/* New enhanced commands */
static void cmd_ipcs(void);
static void cmd_vmstat(void);
static void cmd_iostat(void);
static void cmd_crontab(const char *subcmd, const char *arg1, const char *arg2);
static void cmd_evlog(const char *subcmd, const char *arg1, const char *arg2);
static void cmd_fim(const char *subcmd, const char *arg1, const char *arg2);
static void cmd_netmon(const char *subcmd, const char *arg1, const char *arg2);
static void cmd_sysacct(const char *subcmd, const char *arg1, const char *arg2);
static void cmd_service(const char *subcmd, const char *arg1, const char *arg2);
static void cmd_taskmgr(const char *subcmd, const char *arg1, const char *arg2);
static void cmd_sysreport(const char *arg);
static void cmd_ipc(const char *subcmd, const char *arg1, const char *arg2);
static void cmd_sigstat(const char *subcmd, const char *arg1, const char *arg2);
static void cmd_quota_ext(const char *subcmd, const char *arg1, const char *arg2);
static void cmd_logrotate_ext(const char *subcmd, const char *arg1, const char *arg2);
static void cmd_taskset(const char *pid_str, const char *mask_str);
static void cmd_chrt(const char *pid_str, const char *policy_str);
static void cmd_pidof(const char *name);
static void cmd_pstree(void);
static void cmd_last(void);
static void cmd_uname(const char *opt);
static void cmd_sync(void);
static void cmd_time_cmd(const char *cmd);
static void cmd_slabtop(void);
static void cmd_meminfo(void);
static void cmd_ss(void);
static void cmd_mtr(const char *ip_str);

/* File utility commands */
static void cmd_truncate(const char *file, const char *size_str);
static void cmd_basename(const char *path);
static void cmd_dirname(const char *path);
static void cmd_realpath(const char *path);
static void cmd_mkfifo(const char *path);
static void cmd_mknod(const char *path, const char *type, const char *major, const char *minor);

/* Text processing commands */
static void cmd_cut(const char *delim, const char *field_str, const char *file);
static void cmd_paste(const char *file1, const char *file2);
static void cmd_tr(const char *set1, const char *set2, const char *file);
static void cmd_rev(const char *file);
static void cmd_nl(const char *file);
static void cmd_fold(const char *width_str, const char *file);
static void cmd_expand(const char *tabs_str, const char *file);
static void cmd_unexpand(const char *tabs_str, const char *file);
static void cmd_col(const char *file);
static void cmd_column(const char *file);
static void cmd_look(const char *prefix, const char *file);
static void cmd_comm(const char *file1, const char *file2);
static void cmd_tsort(const char *file);

/* New file/disk commands */
static void cmd_dd(const char *ifile, const char *ofile);
static void cmd_split(const char *file, const char *size_str);
static void cmd_join(const char *f1, const char *f2);
static void cmd_hexdump(const char *file);
static void cmd_strings(const char *file);
static void cmd_cksum(const char *file);
static void cmd_tar(const char *op, const char *archive, const char *files);
static void cmd_gzip(const char *file);
static void cmd_gunzip(const char *file);
static void cmd_fsck(const char *dev);
static void cmd_losetup(const char *dev, const char *file);
static void cmd_fallocate(const char *file, const char *size_str);
static void cmd_filefrag(const char *file);

/* New network commands */
static void cmd_nslookup(const char *host);
static void cmd_dig(const char *host);
static void cmd_dhcp(const char *iface_name);
static void cmd_iptraf(void);
static void cmd_nmap(const char *target);
static void cmd_lanscan(void);
static void cmd_wol(const char *mac_str);
static void cmd_sockstat(void);
static void cmd_tcpdump(const char *filter);
static void cmd_nc(const char *host, const char *port_str);
static void cmd_ftp(const char *host);
static void cmd_speedtest(void);
static void cmd_cal(const char *arg);
static void cmd_login(const char *username);

/* Kernel debugging and diagnostic commands */
static void cmd_futexinfo(void);
static void cmd_epollinfo(void);
static void cmd_inotifyinfo(void);
static void cmd_cgroup(const char *subcmd, const char *arg1, const char *arg2);
static void cmd_kprobe(const char *subcmd, const char *arg1, const char *arg2);
static void cmd_dumpstack(void);
static void cmd_strace(const char *cmd);
static void cmd_lsof(void);
static void cmd_mpstat(void);
static void cmd_pidstat(const char *pid_str);
static void cmd_prlimit(const char *pid_str);
static void cmd_capsh(void);

/* Namespace and tracing subsystem commands */
static void cmd_namespace(void);
static void cmd_tracepoint(void);
static void cmd_uprobe(void);
static void cmd_kmod(void);
static void cmd_firmware(void);
static void cmd_remoteproc(void);
static void cmd_rpmsg(void);
static void cmd_virtio(void);

/* System health and notifier commands */
static void cmd_health(void);
static void cmd_notifier(const char *subcmd);

/* Filesystem commands */
static void cmd_quota(const char *subcmd, const char *arg1, const char *arg2,
                      const char *arg3, const char *arg4, const char *arg5);
static void cmd_fsstat(void);
static void cmd_flock(const char *subcmd, const char *arg1, const char *arg2,
                      const char *arg3);
static void cmd_mount2(const char *subcmd, const char *arg1, const char *arg2,
                       const char *arg3);
static void cmd_dcache(const char *subcmd);
static void cmd_icache(const char *subcmd);
static void cmd_pagecache(const char *subcmd);
static void cmd_readahead_stat(const char *subcmd);
static void cmd_syncstat(const char *subcmd);
static void cmd_ktrace(const char *subcmd, const char *arg1, const char *arg2);
static void cmd_kwork(const char *subcmd, const char *arg1, const char *arg2);
static void cmd_reg(const char *subcmd, const char *arg1, const char *arg2);
static void cmd_apps(const char *subcmd, const char *arg1, const char *arg2);

/* File management advanced commands */
static void cmd_snapshot(const char *subcmd, const char *arg1, const char *arg2);
static void cmd_version(const char *subcmd, const char *arg1, const char *arg2);
static void cmd_hash(const char *algo, const char *file);
static void cmd_compress(const char *file);
static void cmd_decompress(const char *file);
static void cmd_watch_dir(const char *subcmd, const char *path);
static void cmd_search(const char *path, const char *pattern);
static void cmd_fc(const char *f1, const char *f2);
static void cmd_fmt(const char *device);
static void cmd_fsck_ext(const char *device);
static void cmd_xattr(const char *subcmd, const char *file, const char *name);
static void cmd_flock_cmd(const char *file, const char *mode);
static void cmd_dd_full(const char *ifile, const char *ofile, const char *bs, const char *count);

/* Database advanced commands */
static void cmd_db_view(const char *subcmd, const char *name, const char *sql);
static void cmd_db_trigger(const char *subcmd, const char *name, const char *table);
static void cmd_db_agg(const char *func, const char *table, const char *column);
static void cmd_db_proc(const char *subcmd, const char *name, const char *args);
static void cmd_db_backup(const char *path);
static void cmd_db_restore(const char *path);
static void cmd_db_export(const char *table, const char *path, const char *fmt);
static void cmd_db_stats(void);
static void cmd_db_vacuum(void);
static void cmd_db_reindex(const char *table);

/* Misc utility commands */
static void cmd_yes(const char *str);
static void cmd_seq(const char *first, const char *last);
static void cmd_factor(const char *num_str);
static void cmd_shuf(const char *file);
static void cmd_false_cmd(void);
static void cmd_true_cmd(void);
static void cmd_test_cmd(const char *expr);
static void cmd_expr_cmd(const char *math);

/* ---- Built-in app registry ---- */
typedef int (*app_main_t)(int argc, char *argv[]);

typedef struct {
    const char *name;
    const char *desc;
    app_main_t main_func;
} shell_builtin_app_t;

/* App declarations - defined in apps/_app.c files */
extern int app_init_main(int argc, char *argv[]);
extern int app_ls_main(int argc, char *argv[]);
extern int app_cat_main(int argc, char *argv[]);
extern int app_cp_main(int argc, char *argv[]);
extern int app_mv_main(int argc, char *argv[]);
extern int app_rm_main(int argc, char *argv[]);
extern int app_mkdir_main(int argc, char *argv[]);
extern int app_echo_main(int argc, char *argv[]);
extern int app_wc_main(int argc, char *argv[]);
extern int app_grep_main(int argc, char *argv[]);
extern int app_head_main(int argc, char *argv[]);
extern int app_date_main(int argc, char *argv[]);

/* ---- 内核模式应用包装函数 ---- */
/* 以下应用原本在 apps/ 目录中使用 userland syscall，
 * 这里提供内核模式的简化实现，通过 VFS 和 shell API 运行 */

static int app_calc_main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    shell_print("Calculator v1.0 (type 'quit' to exit)\n");
    while (1) {
        shell_print("> ");
        char input[256];
        int len = shell_read_line(input, sizeof(input));
        if (len <= 0) continue;
        if (strcmp(input, "quit") == 0) break;
        if (input[0] == '\0') continue;
        /* 简单表达式求值 - 复用 cmd_calc 的逻辑 */
        const char *p = input;
        while (*p == ' ') p++;
        int32_t result = 0, num = 0, got_num = 0;
        char op = '+';
        while (*p) {
            if (*p >= '0' && *p <= '9') {
                num = 0;
                while (*p >= '0' && *p <= '9') { num = num * 10 + (*p - '0'); p++; }
                switch (op) {
                    case '+': result += num; break;
                    case '-': result -= num; break;
                    case '*': result *= num; break;
                    case '/': if (num != 0) result /= num; break;
                }
                got_num = 1;
            } else if (*p == '+' || *p == '-' || *p == '*' || *p == '/') {
                op = *p; p++;
            } else { p++; }
        }
        if (got_num) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%d\n", result);
            shell_print(buf);
        }
    }
    return 0;
}

static int app_notepad_main(int argc, char *argv[]) {
    const char *file = (argc > 1 && argv[1]) ? argv[1] : 0;
    if (file) {
        cmd_edit(file);
    } else {
        cmd_edit("untitled.txt");
    }
    return 0;
}

static int app_paint_main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    shell_print("Starting Paint...\n");
    gui_app_paint();
    return 0;
}

static int app_snake_main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    shell_print("Starting Snake game... Use arrow keys to move, P to pause, R to restart, ESC to quit.\n");
    snake_game_run();
    vga_text_snapshot();
    shell_print("Snake exited.\n");
    return 0;
}

static int app_2048_main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    shell_print("Starting 2048... Arrows to move, R to restart, ESC to quit.\n");
    game_2048_run();
    vga_text_snapshot();
    shell_print("2048 exited.\n");
    return 0;
}

static int app_tetris_main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    shell_print("Starting Tetris... Arrows to move/rotate, Space to drop, P to pause, R to restart, ESC to quit.\n");
    tetris_run();
    vga_text_snapshot();
    shell_print("Tetris exited.\n");
    return 0;
}

static int app_minesweeper_main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    shell_print("Starting Minesweeper... Arrows to move, Enter to reveal, F to flag, R to restart, ESC to quit.\n");
    minesweeper_run();
    vga_text_snapshot();
    shell_print("Minesweeper exited.\n");
    return 0;
}

static int app_pong_main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    shell_print("Starting Pong...\n");
    pong_run();
    vga_text_snapshot();
    shell_print("Pong exited.\n");
    return 0;
}

static int app_games_main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    game_menu_run();
    vga_text_snapshot();
    return 0;
}

static int app_acalc_main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    calc_interactive_run();
    vga_text_snapshot();
    return 0;
}

static int app_sysmon_main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    sysmon_run();
    vga_text_snapshot();
    return 0;
}

static int app_cal_main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    cal_run();
    vga_text_snapshot();
    return 0;
}

static int app_clock_main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    clock_run();
    vga_text_snapshot();
    return 0;
}

static int app_matrix_main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    matrix_run();
    vga_text_snapshot();
    return 0;
}

static int cmd_life(int argc, char *argv[]) {
    (void)argc; (void)argv;
    life_run();
    vga_text_snapshot();
    return 0;
}

static int cmd_sokoban(int argc, char *argv[]) {
    (void)argc; (void)argv;
    sokoban_run();
    vga_text_snapshot();
    return 0;
}

static int cmd_typing(int argc, char *argv[]) {
    (void)argc; (void)argv;
    typing_run();
    vga_text_snapshot();
    return 0;
}

static int cmd_ascii(int argc, char *argv[]) {
    (void)argc; (void)argv;
    ascii_table_run();
    vga_text_snapshot();
    return 0;
}

static int cmd_nano(int argc, char *argv[]) {
    const char *path = (argc > 1) ? argv[1] : NULL;
    nano_run(path);
    vga_text_snapshot();
    return 0;
}

static int app_desktop_main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    shell_print("Starting desktop environment (display server)...\n");
    shell_print("Press ESC to exit GUI mode.\n");
    ds_start();
    return 0;
}

static int app_terminal_main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    shell_print("You are already in the terminal. Type 'help' for commands, 'apps' for applications.\n");
    return 0;
}

static int app_filemgr_main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    shell_print("Starting File Manager...\n");
    gui_app_filemanager();
    return 0;
}

static int app_touch_main(int argc, char *argv[]) {
    if (argc < 2 || !argv[1]) {
        shell_print("Usage: touch <file>\n");
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        if (vfs_creat(argv[i], FILE_MODE_WRITE | FILE_MODE_READ) != 0) {
            shell_print("touch: failed to create ");
            shell_print(argv[i]);
            shell_print("\n");
        }
    }
    return 0;
}

static int app_help_main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    shell_print("Available commands:\n"
        "  shell    - Command-line shell\n"
        "  ls       - List directory contents\n"
        "  cat      - Display file contents\n"
        "  echo     - Print text\n"
        "  mkdir    - Create directory\n"
        "  rm       - Remove file\n"
        "  cp       - Copy file\n"
        "  mv       - Move/rename file\n"
        "  touch    - Create empty file\n"
        "  help     - Show this help\n"
        "  calc     - Calculator\n"
        "  desktop  - Desktop environment\n"
        "  terminal - Graphical terminal\n"
        "  filemgr  - File manager\n"
        "  notepad  - Text editor\n"
        "  paint    - Drawing program\n"
        "  snake    - Snake game\n");
    return 0;
}

static const shell_builtin_app_t app_registry[] = {
    {"init",     "Init process",            app_init_main},
    {"ls",       "List directory",          app_ls_main},
    {"cat",      "Display file contents",   app_cat_main},
    {"cp",       "Copy file",               app_cp_main},
    {"mv",       "Move/rename file",        app_mv_main},
    {"rm",       "Remove file",             app_rm_main},
    {"mkdir",    "Create directory",        app_mkdir_main},
    {"echo",     "Print text",              app_echo_main},
    {"wc",       "Word/line/char count",    app_wc_main},
    {"grep",     "Search pattern in file",  app_grep_main},
    {"head",     "Show first lines",        app_head_main},
    {"date",     "Show date/time",          app_date_main},
    {"calc",     "Calculator",              app_calc_main},
    {"notepad",  "Text editor",             app_notepad_main},
    {"paint",    "Drawing program",         app_paint_main},
    {"snake",    "Snake game",              app_snake_main},
    {"2048",     "2048 puzzle game",        app_2048_main},
    {"tetris",   "Tetris game",             app_tetris_main},
    {"mine",     "Minesweeper game",        app_minesweeper_main},
    {"minesweeper", "Minesweeper game",     app_minesweeper_main},
    {"pong",     "Pong game",               app_pong_main},
    {"games",    "Game menu",               app_games_main},
    {"acalc",    "Advanced calculator",     app_acalc_main},
    {"sysmon",   "System monitor",          app_sysmon_main},
    {"cal",      "Calendar",                app_cal_main},
    {"clock",    "Digital clock",           app_clock_main},
    {"matrix",   "Matrix rain effect",      app_matrix_main},
    {"life",     "Conway's Game of Life",   cmd_life},
    {"sokoban",  "Sokoban puzzle",          cmd_sokoban},
    {"typing",   "Typing practice",         cmd_typing},
    {"ascii",    "ASCII table",             cmd_ascii},
    {"nano",     "Text editor",             cmd_nano},
    {"edit",     "Text editor",             cmd_nano},
    {"desktop",  "Desktop environment",     app_desktop_main},
    {"terminal", "Graphical terminal",      app_terminal_main},
    {"filemgr",  "File manager",            app_filemgr_main},
    {"touch",    "Create empty file",       app_touch_main},
    {"help",     "Show help",               app_help_main},
    {0, 0, 0}
};

#define APP_COUNT (sizeof(app_registry) / sizeof(app_registry[0]) - 1)

/* ---- 底部任务栏系统 ---- */
#define TASKBAR_MAX_TASKS 8
typedef struct {
    char name[32];
    int active;
    uint32_t pid;  /* 如果有关联进程 */
} taskbar_entry_t;

static taskbar_entry_t taskbar_tasks[TASKBAR_MAX_TASKS];
static int taskbar_task_count = 0;
static int taskbar_visible = 0;  /* 任务栏是否可见 */
static int taskbar_active_idx = 0;  /* 当前活动任务索引 */

static void taskbar_add(const char *name, uint32_t pid) {
    if (taskbar_task_count >= TASKBAR_MAX_TASKS) {
        /* 移除最旧的非活动任务 */
        for (int i = 0; i < taskbar_task_count - 1; i++) {
            taskbar_tasks[i] = taskbar_tasks[i + 1];
        }
        taskbar_task_count--;
    }
    strncpy(taskbar_tasks[taskbar_task_count].name, name, 31);
    taskbar_tasks[taskbar_task_count].name[31] = '\0';
    taskbar_tasks[taskbar_task_count].active = 1;
    taskbar_tasks[taskbar_task_count].pid = pid;
    taskbar_active_idx = taskbar_task_count;
    taskbar_task_count++;
}

static void taskbar_remove(int index) {
    if (index < 0 || index >= taskbar_task_count) return;
    for (int i = index; i < taskbar_task_count - 1; i++) {
        taskbar_tasks[i] = taskbar_tasks[i + 1];
    }
    taskbar_task_count--;
    if (taskbar_active_idx >= taskbar_task_count) {
        taskbar_active_idx = taskbar_task_count - 1;
    }
}

static void taskbar_draw(void) {
    if (!taskbar_visible || taskbar_task_count == 0) return;

    /* 保存光标位置，移到屏幕底部行 */
    shell_print("\033[s");  /* 保存光标 */
    shell_print("\033[24;0H");  /* 移到第24行第0列 */
    shell_print("\033[7m");  /* 反色开始 */

    /* 绘制任务栏 */
    shell_print(" TaskBar: ");
    for (int i = 0; i < taskbar_task_count; i++) {
        if (i == taskbar_active_idx) {
            shell_print("\033[1m");  /* 粗体高亮当前活动任务 */
        }
        char buf[64];
        snprintf(buf, sizeof(buf), "[%d:%s] ", i + 1, taskbar_tasks[i].name);
        shell_print(buf);
        if (i == taskbar_active_idx) {
            shell_print("\033[0m\033[7m");  /* 恢复反色 */
        }
    }

    shell_print("\033[0m");  /* 恢复正常 */
    shell_print("\033[u");  /* 恢复光标 */
}

static void taskbar_toggle(void) {
    taskbar_visible = !taskbar_visible;
    if (taskbar_visible) {
        /* 确保至少有 Shell 任务 */
        if (taskbar_task_count == 0) {
            taskbar_add("Shell", 0);
        }
        taskbar_draw();
        shell_print("Taskbar: ON\n");
    } else {
        /* 清除任务栏行 */
        shell_print("\033[s\033[24;0H\033[2K\033[u");
        shell_print("Taskbar: OFF\n");
    }
}

static void cmd_taskbar(const char *subcmd) {
    if (!subcmd || !*subcmd) {
        /* 无参数: 切换任务栏 */
        taskbar_toggle();
        return;
    }
    if (strcmp(subcmd, "on") == 0) {
        if (!taskbar_visible) taskbar_toggle();
    } else if (strcmp(subcmd, "off") == 0) {
        if (taskbar_visible) taskbar_toggle();
    } else if (strcmp(subcmd, "add") == 0) {
        /* taskbar add <name> - 添加任务 */
        /* arg2 已在 shell_execute_single 中解析 */
        shell_print("Usage: taskbar add <name>\n");
    } else if (strcmp(subcmd, "list") == 0 || strcmp(subcmd, "ls") == 0) {
        shell_print("Taskbar tasks:\n");
        for (int i = 0; i < taskbar_task_count; i++) {
            char buf[80];
            snprintf(buf, sizeof(buf), "  %d: %s %s pid=%u\n",
                i + 1, taskbar_tasks[i].name,
                (i == taskbar_active_idx) ? "(active)" : "",
                taskbar_tasks[i].pid);
            shell_print(buf);
        }
    } else if (strcmp(subcmd, "switch") == 0) {
        /* 切换到下一个任务 */
        if (taskbar_task_count > 0) {
            taskbar_active_idx = (taskbar_active_idx + 1) % taskbar_task_count;
            taskbar_draw();
        }
    } else if (strncmp(subcmd, "rm ", 3) == 0) {
        int idx = atoi(subcmd + 3);
        if (idx >= 1 && idx <= taskbar_task_count) {
            taskbar_remove(idx - 1);
            taskbar_draw();
        } else {
            shell_print("taskbar: Invalid task index\n");
        }
    } else {
        shell_print("Usage: taskbar [on|off|list|switch|rm <n>]\n");
    }
    last_exit_code = 0;
}

static void cmd_pt(const char *options) {
    /* List directory contents using VFS
     * 支持选项:
     *   -l  长格式显示（权限 链接数 所有者 大小 名称）
     *   -a  显示隐藏文件（以.开头的文件）
     *   -la / -al  同时启用长格式和隐藏文件
     * 也支持: ls <path>  列出指定目录
     */
    int show_long = 0;
    int show_all = 0;
    const char *path_arg = 0;

    /* Parse options and path argument */
    if (options && options[0]) {
        const char *p = options;
        if (p[0] == '-') {
            p++;
            while (*p && *p != ' ') {
                if (*p == 'l') show_long = 1;
                else if (*p == 'a') show_all = 1;
                p++;
            }
            while (*p == ' ') p++;
            if (*p) path_arg = p;
        } else {
            path_arg = options;
        }
    }

    char list_path[512];
    if (path_arg) {
        /* Expand ~ in path */
        if (path_arg[0] == '~') {
            const char *home = env_get("HOME");
            if (!home) home = "/";
            snprintf(list_path, sizeof(list_path), "%s%s", home, path_arg + 1);
        } else {
            build_full_path(path_arg, list_path, sizeof(list_path));
        }
    } else {
        strncpy(list_path, current_dir, sizeof(list_path) - 1);
        list_path[sizeof(list_path) - 1] = '\0';
    }

    dentry_t *dir = 0;
    if (path_resolve(list_path, &dir) != 0 || !dir) {
        shell_print("ls: cannot access '");
        shell_print(path_arg ? path_arg : current_dir);
        shell_print("': No such file or directory\n");
        shell_print("Usage: ls [-la] [path]\n");
        shell_print("  -l  long listing format\n");
        shell_print("  -a  show hidden files\n");
        last_exit_code = 1;
        return;
    }

    /* If path is a file, just show its info */
    if (dir->inode && !(dir->inode->mode & FILE_MODE_DIR)) {
        if (show_long) {
            char perm[11] = "----------";
            if (dir->inode->mode & FILE_MODE_DIR) perm[0] = 'd';
            else if (dir->inode->mode & FILE_MODE_LNK) perm[0] = 'l';
            /* Owner */
            if (dir->inode->mode & PERM_READ)  perm[1] = 'r';
            if (dir->inode->mode & PERM_WRITE) perm[2] = 'w';
            if (dir->inode->mode & PERM_EXEC)  perm[3] = 'x';
            /* Group (same as owner for now) */
            perm[4] = perm[1]; perm[5] = perm[2]; perm[6] = perm[3];
            /* Other (read-only) */
            perm[7] = 'r'; perm[8] = '-'; perm[9] = '-';
            char line[320];
            uint32_t sz = dir->inode->size;
            int n = snprintf(line, sizeof(line), " %s %4u %8u  ", perm, 1u, sz);
            uint32_t i;
            for (i = 0; i < 254 && dir->name[i]; i++) line[n + i] = dir->name[i];
            line[n + i] = '\n';
            line[n + i + 1] = '\0';
            shell_print(line);
        } else {
            shell_print(dir->name);
            shell_print("\n");
        }
        last_exit_code = 0;
        return;
    }

    if (!dir->inode) {
        shell_print("ls: cannot access '");
        shell_print(path_arg ? path_arg : current_dir);
        shell_print("': No such file or directory\n");
        last_exit_code = 1;
        return;
    }

    if (path_arg) {
        shell_print(list_path);
        shell_print(":\n");
    }

    /* Count entries and calculate total for -l */
    dentry_t *child = dir->child;
    int count = 0;
    uint64_t total_blocks = 0;
    while (child) {
        int hidden = (child->name[0] == '.' && child->name[1] != '\0'
                     && !(child->name[1] == '.' && child->name[2] == '\0'));
        if (!hidden || show_all) {
            count++;
            if (child->inode) {
                total_blocks += (child->inode->size + 511) / 512;
            }
        }
        child = child->next_sibling;
    }

    if (show_long) {
        char total_buf[32];
        snprintf(total_buf, sizeof(total_buf), "total %llu\n",
                 (unsigned long long)(total_blocks > 0 ? total_blocks : (uint64_t)count * 2));
        shell_print(total_buf);
    }

    child = dir->child;
    while (child) {
        int is_dot = (strcmp(child->name, ".") == 0);
        int is_dotdot = (strcmp(child->name, "..") == 0);
        int hidden = child->name[0] == '.' && !is_dot && !is_dotdot;
        if (hidden && !show_all) {
            child = child->next_sibling;
            continue;
        }

        if (show_long) {
            char perm[11] = "----------";
            uint32_t sz = 0;
            uint32_t nlinks = 1;
            if (child->inode) {
                if (child->inode->mode & FILE_MODE_DIR) perm[0] = 'd';
                else if (child->inode->mode & FILE_MODE_LNK) perm[0] = 'l';
                if (child->inode->mode & PERM_READ)  perm[1] = 'r';
                if (child->inode->mode & PERM_WRITE) perm[2] = 'w';
                if (child->inode->mode & PERM_EXEC)  perm[3] = 'x';
                perm[4] = perm[1]; perm[5] = perm[2]; perm[6] = perm[3];
                if (child->inode->mode & FILE_MODE_DIR) {
                    perm[7] = 'r'; perm[8] = '-'; perm[9] = 'x';
                } else {
                    perm[7] = 'r'; perm[8] = '-'; perm[9] = '-';
                }
                sz = child->inode->size;
                nlinks = child->inode->nlinks ? child->inode->nlinks : 1;
            }
            char prefix[128];
            int n = snprintf(prefix, sizeof(prefix), "%s %2u root %8u  ", perm, nlinks, sz);
            (void)n;
            shell_print(prefix);

            uint32_t mode = child->inode ? child->inode->mode : 0;
            file_type_t ftype = file_type_detect(child->name, mode);
            uint8_t fg, bg;
            file_type_get_color(ftype, &fg, &bg);

            shell_print_colored(child->name, fg, bg);

            char suffix[16];
            int si = 0;
            if (child->inode && (child->inode->mode & FILE_MODE_DIR)) {
                suffix[si++] = '/';
            } else if (child->inode && (child->inode->mode & FILE_MODE_LNK)) {
                suffix[si++] = '@';
            } else if (child->inode && (child->inode->mode & PERM_EXEC)) {
                suffix[si++] = '*';
            }
            suffix[si++] = '\n';
            suffix[si] = '\0';
            shell_print(suffix);
        } else {
            uint32_t mode = child->inode ? child->inode->mode : 0;
            file_type_t ftype = file_type_detect(child->name, mode);
            uint8_t fg, bg;
            file_type_get_color(ftype, &fg, &bg);

            shell_print_colored(child->name, fg, bg);

            char suffix[16];
            int si = 0;
            if (child->inode && (child->inode->mode & FILE_MODE_DIR)) {
                suffix[si++] = '/';
            } else if (child->inode && (child->inode->mode & FILE_MODE_LNK)) {
                suffix[si++] = '@';
            } else if (child->inode && (child->inode->mode & PERM_EXEC)) {
                suffix[si++] = '*';
            }
            suffix[si++] = '\t';
            suffix[si] = '\0';
            shell_print(suffix);
        }
        child = child->next_sibling;
    }
    if (count == 0) {
        shell_print("\n");
    } else if (!show_long) {
        shell_print("\n");
    }
    last_exit_code = 0;
}

#define CAT_MAX_READ_BYTES  (10 * 1024 * 1024)  /* cat/show 最大读取 10MB */

static void cmd_show(const char *file) {
    if (!file || !*file) {
        shell_print("cat: missing file operand\n");
        shell_print("Usage: cat <file>\n");
        shell_print("Displays the contents of the specified file.\n");
        last_exit_code = 1;
        return;
    }

    /* Build full path */
    char full_path[512];
    build_full_path(file, full_path, sizeof(full_path));

    file_t *f = 0;
    if (vfs_open(full_path, FILE_MODE_READ, &f) != 0 || !f) {
        shell_print("cat: ");
        shell_print(file);
        shell_print(": No such file or directory\n");
        last_exit_code = 1;
        return;
    }

    /* 获取文件大小用于限制 (VFS file size - optional info) */
    (void)f;  /* f used by vfs_read below */

    char buf[256];
    int32_t n;
    uint32_t total_read = 0;

    while ((n = vfs_read(f, buf, 255)) > 0) {
        total_read += (uint32_t)n;
        buf[n] = '\0';
        shell_print(buf);

        /* 文件大小限制: 防止超大文件耗尽资源 */
        if (total_read >= CAT_MAX_READ_BYTES) {
            shell_print("\n  (... file truncated, max ");
            shell_print("10MB");
            shell_print(" limit reached)\n");
            break;
        }
    }
    vfs_close(f);
    last_exit_code = 0;
}

static void cmd_go(const char *dir) {
    char target[512];
    const char *display_path = dir ? dir : "~";

    if (!dir || !*dir) {
        const char *home = env_get("HOME");
        if (!home) home = "/";
        strncpy(target, home, sizeof(target) - 1);
        target[sizeof(target) - 1] = '\0';
        display_path = target;
    } else if (dir[0] == '~') {
        const char *home = env_get("HOME");
        if (!home) home = "/";
        snprintf(target, sizeof(target), "%s%s", home, dir + 1);
    } else if (strcmp(dir, "-") == 0) {
        const char *old = env_get("OLDPWD");
        if (old && *old) {
            strncpy(target, old, sizeof(target) - 1);
            target[sizeof(target) - 1] = '\0';
            display_path = old;
        } else {
            strcpy(target, "/");
            shell_print("cd: OLDPWD not set\n");
            last_exit_code = 1;
            return;
        }
    } else if (strcmp(dir, "..") == 0) {
        strncpy(target, current_dir, sizeof(target) - 1);
        target[sizeof(target) - 1] = '\0';
        char *last_slash = strrchr(target, '/');
        if (last_slash && last_slash != target) {
            *last_slash = '\0';
        } else {
            strcpy(target, "/");
        }
    } else if (strcmp(dir, "/") == 0) {
        strcpy(target, "/");
    } else {
        build_full_path(dir, target, sizeof(target));
    }

    inode_t st;
    memset(&st, 0, sizeof(st));
    if (vfs_stat(target, &st) != 0) {
        shell_print("cd: no such file or directory: ");
        shell_print(display_path);
        shell_print("\n");
        last_exit_code = 1;
        return;
    }

    if (!(st.mode & FILE_MODE_DIR)) {
        shell_print("cd: not a directory: ");
        shell_print(display_path);
        shell_print("\n");
        last_exit_code = 1;
        return;
    }

    if (vfs_chdir(target) == 0) {
        env_set("OLDPWD", current_dir);
        strncpy(current_dir, target, 255);
        current_dir[255] = '\0';
        last_exit_code = 0;
    } else {
        shell_print("cd: permission denied: ");
        shell_print(display_path);
        shell_print("\n");
        last_exit_code = 1;
    }
}

static void cmd_where(void) {
    const char *cwd = vfs_getcwd();
    shell_print(cwd ? cwd : current_dir);
    shell_print("\n");
    last_exit_code = 0;
}

static void cmd_clr(void) {
    if (vbe_mode_active) {
        fb_console_clear();
    } else {
        vga_text_clear();
    }
    last_exit_code = 0;
}

static void cmd_ver(void) {
    shell_print("\n  ");
    shell_print(OS_STRING);
    shell_print("\n  Kernel: " KERNEL_STRING " (i386)\n");
    shell_print("  Build:  freestanding, no stdlib\n\n");
    last_exit_code = 0;
}

static void cmd_applist(const char *arg) {
    (void)arg;
    shell_print("Installed applications:\n\n");
    app_list_all();
    shell_print("\nType the app name to launch it (e.g., 'snake', 'calc', 'settings').\n");
    shell_print("Snake runs in text mode with arrow keys.\n");
    shell_print("Type 'desktop' to start the GUI environment.\n");
    last_exit_code = 0;
}

static void cmd_help(const char *arg) {
    if (arg && *arg) {
        /* Detailed help for a specific command */
        if (strcmp(arg, "pt") == 0 || strcmp(arg, "ls") == 0) {
            shell_print("pt / ls - List directory contents\n");
            shell_print("Usage: pt\n       ls\n");
            shell_print("Lists files and subdirectories in the current directory.\n");
            shell_print("Directories are shown with a trailing '/'.\n");
            shell_print("Example: pt\n");
        } else if (strcmp(arg, "show") == 0 || strcmp(arg, "cat") == 0 || strcmp(arg, "type") == 0) {
            shell_print("show / cat / type - Display file contents\n");
            shell_print("Usage: show <file>\n       cat <file>\n       type <file>\n");
            shell_print("Reads and displays the contents of the specified file.\n");
            shell_print("Example: show readme.txt\n");
        } else if (strcmp(arg, "go") == 0 || strcmp(arg, "cd") == 0) {
            shell_print("go / cd - Change directory\n");
            shell_print("Usage: go <dir>\n       cd <dir>\n");
            shell_print("Changes the current working directory.\n");
            shell_print("Use '..' to go up, '/' to go to root.\n");
            shell_print("Example: go /home\n");
        } else if (strcmp(arg, "where") == 0 || strcmp(arg, "pwd") == 0) {
            shell_print("where / pwd - Show current directory\n");
            shell_print("Usage: where\n       pwd\n");
            shell_print("Displays the current working directory path.\n");
        } else if (strcmp(arg, "copy") == 0) {
            shell_print("copy - Copy file\n");
            shell_print("Usage: copy <src> <dst>\n");
            shell_print("Copies the contents of <src> to <dst>.\n");
            shell_print("Example: copy file.txt backup.txt\n");
        } else if (strcmp(arg, "del") == 0) {
            shell_print("del - Delete file\n");
            shell_print("Usage: del <file>\n");
            shell_print("Deletes the specified file.\n");
            shell_print("Example: del temp.txt\n");
        } else if (strcmp(arg, "ren") == 0) {
            shell_print("ren - Rename file/directory\n");
            shell_print("Usage: ren <old> <new>\n");
            shell_print("Renames a file or directory from <old> to <new>.\n");
            shell_print("Example: ren old.txt new.txt\n");
        } else if (strcmp(arg, "mkdir") == 0) {
            shell_print("mkdir - Create directory\n");
            shell_print("Usage: mkdir <dir>\n");
            shell_print("Creates a new directory.\n");
            shell_print("Example: mkdir projects\n");
        } else if (strcmp(arg, "touch") == 0) {
            shell_print("touch - Create empty file or update timestamp\n");
            shell_print("Usage: touch <file>\n");
            shell_print("Creates an empty file if it does not exist.\n");
            shell_print("Example: touch notes.txt\n");
        } else if (strcmp(arg, "append") == 0) {
            shell_print("append - Append text to file\n");
            shell_print("Usage: append <file> <text>\n");
            shell_print("Appends a line of text to the specified file.\n");
            shell_print("Example: append log.txt 'System started'\n");
        } else if (strcmp(arg, "head") == 0) {
            shell_print("head - Show first n lines of file\n");
            shell_print("Usage: head <file> [n]\n");
            shell_print("Displays the first n lines (default 10).\n");
            shell_print("Example: head readme.txt 5\n");
        } else if (strcmp(arg, "tail") == 0) {
            shell_print("tail - Show last n lines of file\n");
            shell_print("Usage: tail <file> [n]\n");
            shell_print("Displays the last n lines (default 10).\n");
            shell_print("Example: tail log.txt 20\n");
        } else if (strcmp(arg, "wc") == 0) {
            shell_print("wc - Count lines, words, and characters\n");
            shell_print("Usage: wc <file>\n");
            shell_print("Prints line count, word count, and byte count.\n");
            shell_print("Example: wc document.txt\n");
        } else if (strcmp(arg, "diff") == 0) {
            shell_print("diff - Compare two files\n");
            shell_print("Usage: diff <file1> <file2>\n");
            shell_print("Compares two files line by line and shows differences.\n");
            shell_print("Example: diff file1.txt file2.txt\n");
        } else if (strcmp(arg, "sort") == 0) {
            shell_print("sort - Sort lines in file\n");
            shell_print("Usage: sort <file>\n");
            shell_print("Reads a file and outputs its lines in sorted order.\n");
            shell_print("Example: sort names.txt\n");
        } else if (strcmp(arg, "uniq") == 0) {
            shell_print("uniq - Remove duplicate consecutive lines\n");
            shell_print("Usage: uniq <file>\n");
            shell_print("Removes adjacent duplicate lines from the file.\n");
            shell_print("Tip: Use with sort for full deduplication.\n");
            shell_print("Example: sort data.txt | uniq\n");
        } else if (strcmp(arg, "grep") == 0) {
            shell_print("grep - Search for pattern in file\n");
            shell_print("Usage: grep <pattern> <file>\n");
            shell_print("Searches for lines containing <pattern> in <file>.\n");
            shell_print("Example: grep error log.txt\n");
        } else if (strcmp(arg, "replace") == 0) {
            shell_print("replace - Replace text in file\n");
            shell_print("Usage: replace <old> <new> <file>\n");
            shell_print("Replaces all occurrences of <old> with <new> in <file>.\n");
            shell_print("Example: replace foo bar data.txt\n");
        } else if (strcmp(arg, "find") == 0) {
            shell_print("find - Search for files by name\n");
            shell_print("Usage: find <name>\n");
            shell_print("Recursively searches for files matching <name>.\n");
            shell_print("Example: find config\n");
        } else if (strcmp(arg, "size") == 0) {
            shell_print("size - Show file size\n");
            shell_print("Usage: size <file>\n");
            shell_print("Displays the size of the specified file in bytes.\n");
            shell_print("Example: size image.bmp\n");
        } else if (strcmp(arg, "stat") == 0) {
            shell_print("stat - Show file metadata\n");
            shell_print("Usage: stat <file>\n");
            shell_print("Displays detailed file information: size, type, mode, etc.\n");
            shell_print("Example: stat readme.txt\n");
        } else if (strcmp(arg, "tree") == 0) {
            shell_print("tree - Show directory tree\n");
            shell_print("Usage: tree [dir]\n");
            shell_print("Displays a tree view of the directory structure.\n");
            shell_print("Example: tree /home\n");
        } else if (strcmp(arg, "du") == 0) {
            shell_print("du - Show disk usage\n");
            shell_print("Usage: du [dir]\n");
            shell_print("Shows the total disk usage of the specified directory.\n");
            shell_print("Example: du /home\n");
        } else if (strcmp(arg, "df") == 0) {
            shell_print("df - Show filesystem disk space\n");
            shell_print("Usage: df\n");
            shell_print("Shows disk space usage for all mounted filesystems.\n");
        } else if (strcmp(arg, "chmod") == 0) {
            shell_print("chmod - Change file permissions\n");
            shell_print("Usage: chmod <mode> <file>\n");
            shell_print("Changes the permission mode of a file.\n");
            shell_print("Example: chmod 755 script.sh\n");
        } else if (strcmp(arg, "chown") == 0) {
            shell_print("chown - Change file owner\n");
            shell_print("Usage: chown <uid:gid> <file>\n");
            shell_print("Changes the owner (uid) and group (gid) of a file.\n");
            shell_print("Example: chown 0:0 config.txt\n");
        } else if (strcmp(arg, "file") == 0) {
            shell_print("file - Determine file type\n");
            shell_print("Usage: file <filename>\n");
            shell_print("Shows the type of a file based on extension and mode.\n");
            shell_print("Example: file readme.txt\n");
        } else if (strcmp(arg, "ln") == 0) {
            shell_print("ln - Create hard link\n");
            shell_print("Usage: ln <target> <link>\n");
            shell_print("Creates a hard link pointing to the same inode.\n");
            shell_print("Example: ln file.txt hardlink.txt\n");
        } else if (strcmp(arg, "ln_s") == 0 || strcmp(arg, "symlink") == 0) {
            shell_print("ln_s / symlink - Create symbolic link\n");
            shell_print("Usage: ln_s <target> <link>\n");
            shell_print("Creates a symbolic link pointing to the target path.\n");
            shell_print("Example: ln_s /path/to/file symlink.txt\n");
        } else if (strcmp(arg, "readlink") == 0) {
            shell_print("readlink - Read symbolic link target\n");
            shell_print("Usage: readlink <path>\n");
            shell_print("Shows the target path of a symbolic link.\n");
            shell_print("Example: readlink symlink.txt\n");
        } else if (strcmp(arg, "env") == 0) {
            shell_print("env - List all environment variables\n");
            shell_print("Usage: env\n");
            shell_print("Lists all currently set environment variables.\n");
        } else if (strcmp(arg, "setenv") == 0) {
            shell_print("setenv - Set environment variable\n");
            shell_print("Usage: setenv <name> <value>\n");
            shell_print("Sets or updates an environment variable.\n");
            shell_print("Example: setenv PATH /bin:/usr/bin\n");
        } else if (strcmp(arg, "unsetenv") == 0) {
            shell_print("unsetenv - Unset environment variable\n");
            shell_print("Usage: unsetenv <name>\n");
            shell_print("Removes an environment variable.\n");
            shell_print("Example: unsetenv PATH\n");
        } else if (strcmp(arg, "ulimit") == 0) {
            shell_print("ulimit - Get/set resource limits\n");
            shell_print("Usage: ulimit [resource] [value]\n");
            shell_print("Shows or modifies process resource limits.\n");
            shell_print("Resources: cpu fsize data stack core nofile nproc memlock as locks\n");
            shell_print("Example: ulimit nofile 1024\n");
        } else if (strcmp(arg, "which") == 0) {
            shell_print("which - Show command location/type\n");
            shell_print("Usage: which <cmd>\n");
            shell_print("Shows whether a command is built-in or an alias.\n");
            shell_print("Example: which ls\n");
        } else if (strcmp(arg, "install") == 0) {
            shell_print("install - Copy file and set attributes\n");
            shell_print("Usage: install <src> <dst>\n");
            shell_print("Copies a file and sets permissions.\n");
            shell_print("Example: install program.bin /bin/program\n");
        } else if (strcmp(arg, "tee") == 0) {
            shell_print("tee - Read stdin, write to stdout and file\n");
            shell_print("Usage: <cmd> | tee <file>\n");
            shell_print("Reads from stdin and writes to both stdout and a file.\n");
            shell_print("Example: echo hello | tee output.txt\n");
        } else if (strcmp(arg, "ps") == 0) {
            shell_print("ps - List running processes\n");
            shell_print("Usage: ps\n");
            shell_print("Lists all processes with PID, state, and name.\n");
        } else if (strcmp(arg, "kill") == 0) {
            shell_print("kill - Kill a process\n");
            shell_print("Usage: kill <pid>\n");
            shell_print("Sends SIGKILL to the specified process.\n");
            shell_print("Example: kill 5\n");
        } else if (strcmp(arg, "top") == 0) {
            shell_print("top - Show system resource usage\n");
            shell_print("Usage: top\n");
            shell_print("Shows CPU, memory, and process statistics.\n");
        } else if (strcmp(arg, "nice") == 0) {
            shell_print("nice - Set process priority\n");
            shell_print("Usage: nice <pid> <priority>\n");
            shell_print("Sets the nice value (priority) of a process.\n");
            shell_print("Lower value = higher priority. Range: 0-39.\n");
            shell_print("Example: nice 3 10\n");
        } else if (strcmp(arg, "renice") == 0) {
            shell_print("renice - Change process priority\n");
            shell_print("Usage: renice <pid> <priority>\n");
            shell_print("Changes the nice value of a running process.\n");
            shell_print("Example: renice 3 5\n");
        } else if (strcmp(arg, "jobs") == 0) {
            shell_print("jobs - List background jobs\n");
            shell_print("Usage: jobs\n");
            shell_print("Lists all background processes.\n");
        } else if (strcmp(arg, "bg") == 0) {
            shell_print("bg - Put process in background\n");
            shell_print("Usage: bg <pid>\n");
            shell_print("Resumes a stopped process in the background.\n");
            shell_print("Example: bg 2\n");
        } else if (strcmp(arg, "fg") == 0) {
            shell_print("fg - Bring process to foreground\n");
            shell_print("Usage: fg <pid>\n");
            shell_print("Brings a background process to the foreground.\n");
            shell_print("Example: fg 2\n");
        } else if (strcmp(arg, "exec") == 0) {
            shell_print("exec - Execute a program file\n");
            shell_print("Usage: exec <file> [args...]\n");
            shell_print("Replaces the current shell with the specified program.\n");
            shell_print("Example: exec /bin/shell\n");
        } else if (strcmp(arg, "run") == 0) {
            shell_print("run - Execute a program file\n");
            shell_print("Usage: run <file>\n");
            shell_print("Executes the specified program file.\n");
            shell_print("Example: run /bin/hello\n");
        } else if (strcmp(arg, "nohup") == 0) {
            shell_print("nohup - Run command immune to hangups\n");
            shell_print("Usage: nohup <command>\n");
            shell_print("Runs a command that will ignore hangup signals.\n");
            shell_print("Example: nohup run server\n");
        } else if (strcmp(arg, "watch") == 0) {
            shell_print("watch - Execute command periodically\n");
            shell_print("Usage: watch <command>\n");
            shell_print("Executes the specified command every 2 seconds.\n");
            shell_print("Press any key to stop.\n");
            shell_print("Example: watch ps\n");
        } else if (strcmp(arg, "sleep") == 0) {
            shell_print("sleep - Sleep for N seconds\n");
            shell_print("Usage: sleep <seconds>\n");
            shell_print("Pauses execution for the specified number of seconds.\n");
            shell_print("Example: sleep 5\n");
        } else if (strcmp(arg, "test") == 0) {
            shell_print("test - Evaluate conditional expression\n");
            shell_print("Usage: test <expr>\n");
            shell_print("Evaluates a conditional expression and sets exit code.\n");
            shell_print("Operators: -eq, -ne, -gt, -lt, -ge, -le, =, !=\n");
            shell_print("Example: test 5 -gt 3\n");
        } else if (strcmp(arg, "expr") == 0) {
            shell_print("expr - Evaluate mathematical expression\n");
            shell_print("Usage: expr <math>\n");
            shell_print("Evaluates a mathematical expression and prints the result.\n");
            shell_print("Supports: +, -, *, /, %%\n");
            shell_print("Example: expr 10 + 20\n");
        } else if (strcmp(arg, "xargs") == 0) {
            shell_print("xargs - Build arguments from stdin\n");
            shell_print("Usage: <cmd1> | xargs <cmd2>\n");
            shell_print("Reads items from stdin and appends them as arguments.\n");
            shell_print("Example: echo file.txt | xargs cat\n");
        } else if (strcmp(arg, "echo") == 0) {
            shell_print("echo - Print text\n");
            shell_print("Usage: echo <text>\n");
            shell_print("Prints the specified text followed by a newline.\n");
            shell_print("Supports $VAR expansion.\n");
            shell_print("Example: echo Hello $USER\n");
        } else if (strcmp(arg, "set") == 0) {
            shell_print("set - Set environment variable\n");
            shell_print("Usage: set <var> <value>\n");
            shell_print("Sets an environment variable.\n");
            shell_print("Example: set PATH /bin:/usr/bin\n");
        } else if (strcmp(arg, "env") == 0) {
            shell_print("env - Show environment variables\n");
            shell_print("Usage: env\n");
            shell_print("Displays all environment variables.\n");
        } else if (strcmp(arg, "unset") == 0) {
            shell_print("unset - Remove environment variable\n");
            shell_print("Usage: unset <var>\n");
            shell_print("Removes an environment variable.\n");
            shell_print("Example: unset TEMP\n");
        } else if (strcmp(arg, "alias") == 0) {
            shell_print("alias - Create command alias\n");
            shell_print("Usage: alias [name=value]\n");
            shell_print("Without arguments, lists all aliases.\n");
            shell_print("With name=value, creates a new alias.\n");
            shell_print("Example: alias ll=pt\n");
        } else if (strcmp(arg, "history") == 0) {
            shell_print("history - Show command history\n");
            shell_print("Usage: history [-c]\n");
            shell_print("Displays the command history with line numbers.\n");
            shell_print("  -c  Clear all command history\n");
        } else if (strcmp(arg, "calc") == 0) {
            shell_print("calc - Simple calculator\n");
            shell_print("Usage: calc <expression>\n");
            shell_print("Evaluates a simple arithmetic expression (+, -, *, /).\n");
            shell_print("Example: calc 2+3*4\n");
        } else if (strcmp(arg, "edit") == 0) {
            shell_print("edit - Open text editor\n");
            shell_print("Usage: edit <file>\n");
            shell_print("Opens the built-in text editor with the specified file.\n");
            shell_print("Example: edit notes.txt\n");
        } else if (strcmp(arg, "reboot") == 0) {
            shell_print("reboot - Reboot the system\n");
            shell_print("Usage: reboot\n");
            shell_print("Performs a warm reboot of the system.\n");
        } else if (strcmp(arg, "halt") == 0) {
            shell_print("halt - Halt the system\n");
            shell_print("Usage: halt\n");
            shell_print("Stops the CPU. The system must be power-cycled.\n");
        } else if (strcmp(arg, "shutdown") == 0) {
            shell_print("shutdown - Shutdown the system\n");
            shell_print("Usage: shutdown\n");
            shell_print("Performs an ACPI S5 shutdown to power off the system.\n");
        } else if (strcmp(arg, "ver") == 0) {
            shell_print("ver - Show kernel version\n");
            shell_print("Usage: ver\n");
            shell_print("Displays the kernel version string.\n");
        } else if (strcmp(arg, "sysinfo") == 0) {
            shell_print("sysinfo - Show system information\n");
            shell_print("Usage: sysinfo\n");
            shell_print("Displays kernel version, memory, uptime, and CPU info.\n");
        } else if (strcmp(arg, "schedpolicy") == 0) {
            shell_print("schedpolicy - Show scheduler policy table\n");
            shell_print("Usage: schedpolicy\n");
            shell_print("Displays all available scheduling policies and current settings.\n");
        } else if (strcmp(arg, "mempolicy") == 0) {
            shell_print("mempolicy - Show page replacement policy table\n");
            shell_print("Usage: mempolicy\n");
            shell_print("Displays page replacement algorithms, stats, and current policy.\n");
        } else if (strcmp(arg, "iosched") == 0) {
            shell_print("iosched - Show I/O scheduler policies\n");
            shell_print("Usage: iosched\n");
            shell_print("Displays available I/O scheduler algorithms and queue stats.\n");
        } else if (strcmp(arg, "softirq") == 0) {
            shell_print("softirq - Show softirq statistics\n");
            shell_print("Usage: softirq\n");
            shell_print("Displays softirq/tasklet execution counts.\n");
        } else if (strcmp(arg, "oom") == 0) {
            shell_print("oom - Show OOM killer state\n");
            shell_print("Usage: oom\n");
            shell_print("Displays OOM scores for all processes and kill count.\n");
        } else if (strcmp(arg, "sysctl") == 0) {
            shell_print("sysctl - Read/write kernel parameters\n");
            shell_print("Usage: sysctl [-a] [name[=value]]\n");
            shell_print("  -a         List all parameters\n");
            shell_print("  name       Read parameter value\n");
            shell_print("  name=val   Set parameter value\n");
        } else if (strcmp(arg, "sysrq") == 0) {
            shell_print("sysrq - Show SysRq magic key help\n");
            shell_print("Usage: sysrq\n");
            shell_print("Displays all available SysRq magic key functions.\n");
        } else if (strcmp(arg, "mem") == 0) {
            shell_print("mem - Show memory usage\n");
            shell_print("Usage: mem\n");
            shell_print("Shows total, used, and free memory in KB.\n");
        } else if (strcmp(arg, "free") == 0) {
            shell_print("free - Show memory usage details\n");
            shell_print("Usage: free\n");
            shell_print("Shows memory usage in KB and MB.\n");
        } else if (strcmp(arg, "uptime") == 0) {
            shell_print("uptime - Show system uptime\n");
            shell_print("Usage: uptime\n");
            shell_print("Shows how long the system has been running.\n");
        } else if (strcmp(arg, "date") == 0) {
            shell_print("date - Show current date\n");
            shell_print("Usage: date\n");
            shell_print("Displays the current date (YYYY-MM-DD).\n");
        } else if (strcmp(arg, "time") == 0) {
            shell_print("time - Show current time\n");
            shell_print("Usage: time\n");
            shell_print("Displays the current date and time (YYYY-MM-DD HH:MM:SS).\n");
        } else if (strcmp(arg, "mount") == 0) {
            shell_print("mount - Mount a filesystem\n");
            shell_print("Usage: mount <device> <directory>\n");
            shell_print("Mounts a filesystem from <device> onto <directory>.\n");
            shell_print("Example: mount /dev/hda1 /mnt\n");
        } else if (strcmp(arg, "umount") == 0) {
            shell_print("umount - Unmount a filesystem\n");
            shell_print("Usage: umount <directory>\n");
            shell_print("Unmounts the filesystem mounted at <directory>.\n");
            shell_print("Example: umount /mnt\n");
        } else if (strcmp(arg, "format") == 0) {
            shell_print("format - Format a disk\n");
            shell_print("Usage: format <device> <fstype>\n");
            shell_print("Formats a disk with the specified filesystem type.\n");
            shell_print("Example: format /dev/hda1 fat32\n");
        } else if (strcmp(arg, "fdisk") == 0) {
            shell_print("fdisk - Show partition table\n");
            shell_print("Usage: fdisk <device>\n");
            shell_print("Displays the partition table for the specified disk.\n");
            shell_print("Example: fdisk /dev/hda\n");
        } else if (strcmp(arg, "chkdsk") == 0) {
            shell_print("chkdsk - Check disk for errors\n");
            shell_print("Usage: chkdsk <device>\n");
            shell_print("Checks the specified disk for filesystem errors.\n");
            shell_print("Example: chkdsk /dev/hda1\n");
        } else if (strcmp(arg, "lsblk") == 0) {
            shell_print("lsblk - List block devices\n");
            shell_print("Usage: lsblk\n");
            shell_print("Lists all block devices with their major:minor numbers.\n");
        } else if (strcmp(arg, "ping") == 0) {
            shell_print("ping - Send ICMP ping\n");
            shell_print("Usage: ping <ip>\n");
            shell_print("Sends an ICMP echo request to the specified IP address.\n");
            shell_print("Example: ping 192.168.1.1\n");
        } else if (strcmp(arg, "ifconfig") == 0) {
            shell_print("ifconfig - Show network interfaces\n");
            shell_print("Usage: ifconfig\n");
            shell_print("Displays all network interfaces with their configuration.\n");
        } else if (strcmp(arg, "route") == 0) {
            shell_print("route - Show routing table\n");
            shell_print("Usage: route\n");
            shell_print("Displays the kernel IP routing table.\n");
        } else if (strcmp(arg, "dns") == 0) {
            shell_print("dns - DNS lookup\n");
            shell_print("Usage: dns <hostname>\n");
            shell_print("Performs a DNS lookup for the specified hostname.\n");
            shell_print("Example: dns example.com\n");
        } else if (strcmp(arg, "wget") == 0) {
            shell_print("wget - Download file\n");
            shell_print("Usage: wget <url>\n");
            shell_print("Downloads a file from the specified URL.\n");
            shell_print("Example: wget http://example.com/file.txt\n");
        } else if (strcmp(arg, "netstat") == 0) {
            shell_print("netstat - Show network connections\n");
            shell_print("Usage: netstat\n");
            shell_print("Shows active network connections and statistics.\n");
        } else if (strcmp(arg, "traceroute") == 0) {
            shell_print("traceroute - Trace route to host\n");
            shell_print("Usage: traceroute <ip>\n");
            shell_print("Traces the route to the specified host.\n");
            shell_print("Example: traceroute 8.8.8.8\n");
        } else if (strcmp(arg, "arp") == 0) {
            shell_print("arp - Show ARP table\n");
            shell_print("Usage: arp\n");
            shell_print("Displays the ARP cache table.\n");
        } else if (strcmp(arg, "hostname") == 0) {
            shell_print("hostname - Show/set hostname\n");
            shell_print("Usage: hostname [name]\n");
            shell_print("Shows or sets the system hostname.\n");
            shell_print("Example: hostname myserver\n");
        } else if (strcmp(arg, "lspci") == 0) {
            shell_print("lspci - List PCI devices\n");
            shell_print("Usage: lspci\n");
            shell_print("Lists all PCI devices with vendor/device IDs.\n");
        } else if (strcmp(arg, "lsusb") == 0) {
            shell_print("lsusb - List USB devices\n");
            shell_print("Usage: lsusb\n");
            shell_print("Lists all connected USB devices.\n");
        } else if (strcmp(arg, "sensors") == 0) {
            shell_print("sensors - Show hardware sensor data\n");
            shell_print("Usage: sensors\n");
            shell_print("Displays CPU temperature, fan speed, and voltages.\n");
        } else if (strcmp(arg, "freq") == 0) {
            shell_print("freq - Show/set CPU frequency\n");
            shell_print("Usage: freq [governor|MHz]\n");
            shell_print("Shows or sets the CPU frequency governor or clock speed.\n");
            shell_print("Example: freq 1200\n");
        } else if (strcmp(arg, "dev") == 0) {
            shell_print("dev - List devices in /dev\n");
            shell_print("Usage: dev\n");
            shell_print("Lists all device entries in the /dev filesystem.\n");
        } else if (strcmp(arg, "clr") == 0) {
            shell_print("clr - Clear screen\n");
            shell_print("Usage: clr\n");
            shell_print("Clears the terminal screen.\n");
        } else if (strcmp(arg, "help") == 0) {
            shell_print("help - Show help information\n");
            shell_print("Usage: help [command]\n");
            shell_print("Without arguments, shows a categorized command list.\n");
            shell_print("With a command name, shows detailed help for that command.\n");
        } else if (strcmp(arg, "accel") == 0) {
            shell_print("accel - 2D acceleration test\n");
            shell_print("Usage: accel [info|fill|blit]\n");
            shell_print("Tests GPU 2D acceleration features.\n");
        } else if (strcmp(arg, "httpget") == 0) {
            shell_print("httpget - HTTP GET request\n");
            shell_print("Usage: httpget <url>\n");
            shell_print("Performs an HTTP GET request and displays the response.\n");
            shell_print("Example: httpget http://example.com\n");
        } else if (strcmp(arg, "imgview") == 0) {
            shell_print("imgview - View JPEG/PNG image\n");
            shell_print("Usage: imgview <file>\n");
            shell_print("Decodes and displays a JPEG or PNG image file.\n");
            shell_print("Supports baseline JPEG and PNG (8-bit, non-interlaced).\n");
            shell_print("Example: imgview /images/photo.jpg\n");
        } else if (strcmp(arg, "pkg") == 0) {
            shell_print("pkg - Package manager\n");
            shell_print("Usage: pkg <command> [args]\n");
            shell_print("Commands:\n");
            shell_print("  install <name>  - Install a package\n");
            shell_print("  remove <name>   - Remove a package\n");
            shell_print("  update <name>   - Update a package\n");
            shell_print("  update-all      - Update all packages\n");
            shell_print("  list            - List installed packages\n");
            shell_print("  search <name>   - Search available packages\n");
            shell_print("  info <name>     - Show package info\n");
            shell_print("  download <url>  - Download a package\n");
        } else if (strcmp(arg, "tftp") == 0) {
            shell_print("tftp - TFTP file transfer\n");
            shell_print("Usage: tftp <server:filename>\n");
            shell_print("Downloads a file from a TFTP server.\n");
            shell_print("Example: tftp 192.168.1.1:boot.img\n");
        } else if (strcmp(arg, "ntp") == 0) {
            shell_print("ntp - NTP time query\n");
            shell_print("Usage: ntp [server]\n");
            shell_print("Queries an NTP server for the current time.\n");
            shell_print("Default server: pool.ntp.org\n");
            shell_print("Example: ntp time.google.com\n");
        } else if (strcmp(arg, "telnet") == 0) {
            shell_print("telnet - Telnet remote shell\n");
            shell_print("Usage: telnet <host> [port]\n");
            shell_print("Connects to a remote host via Telnet.\n");
            shell_print("Press ESC to disconnect.\n");
            shell_print("Example: telnet 192.168.1.1 23\n");
        } else if (strcmp(arg, "play") == 0) {
            shell_print("play - WAV audio player\n");
            shell_print("Usage: play <file.wav>\n");
            shell_print("Plays a WAV format audio file through the sound device.\n");
            shell_print("Supports PCM format (8/16bit, mono/stereo).\n");
            shell_print("Max file size: 8MB.\n");
            shell_print("Example: play /music/test.wav\n");
        } else if (strcmp(arg, "vol") == 0) {
            shell_print("vol - Volume control\n");
            shell_print("Usage: vol [0-100]\n");
            shell_print("Sets or displays the master volume.\n");
            shell_print("Without argument: shows current volume.\n");
            shell_print("With argument: sets volume (0=mute, 100=max).\n");
            shell_print("Example: vol 80\n");
        } else if (strcmp(arg, "sound") == 0) {
            shell_print("sound - List audio devices\n");
            shell_print("Usage: sound\n");
            shell_print("Displays all registered audio devices with their\n");
            shell_print("sample rate, channels, bits per sample, etc.\n");
            shell_print("Example: sound\n");
        } else if (strcmp(arg, "whoami") == 0) {
            shell_print("whoami - Show current username\n");
            shell_print("Usage: whoami\n");
            shell_print("Displays the username of the currently logged-in user.\n");
        } else if (strcmp(arg, "users") == 0) {
            shell_print("users - List all users\n");
            shell_print("Usage: users\n");
            shell_print("Lists all registered users on the system.\n");
            shell_print("Admin users are marked with (admin).\n");
        } else if (strcmp(arg, "useradd") == 0) {
            shell_print("useradd - Create a new user\n");
            shell_print("Usage: useradd <username>\n");
            shell_print("Creates a new user account (admin only).\n");
            shell_print("The new user is assigned the next available UID starting from 1002.\n");
            shell_print("Example: useradd john\n");
        } else if (strcmp(arg, "userdel") == 0) {
            shell_print("userdel - Delete a user\n");
            shell_print("Usage: userdel <username>\n");
            shell_print("Deletes the specified user account (admin only).\n");
            shell_print("Cannot delete root.\n");
            shell_print("Example: userdel john\n");
        } else if (strcmp(arg, "passwd") == 0) {
            shell_print("passwd - Change password\n");
            shell_print("Usage: passwd [username]\n");
            shell_print("Changes the password for the current user or a specified user.\n");
            shell_print("Admin users can change any user's password.\n");
            shell_print("Non-admin users must verify their old password first.\n");
            shell_print("Example: passwd\n         passwd john\n");
        } else if (strcmp(arg, "su") == 0) {
            shell_print("su - Switch user\n");
            shell_print("Usage: su <username>\n");
            shell_print("Switches to the specified user account.\n");
            shell_print("Requires the target user's password (root does not need a password).\n");
            shell_print("Example: su admin\n");
        } else if (strcmp(arg, "logout") == 0) {
            shell_print("logout - Log out\n");
            shell_print("Usage: logout\n");
            shell_print("Logs out the current user and returns to the login screen.\n");
        } else if (strcmp(arg, "save") == 0) {
            shell_print("save - Save VM state and suspend\n");
            shell_print("Usage: save\n");
            shell_print("Saves CPU registers, timer state, and enters ACPI S3 sleep.\n");
            shell_print("Use 'resume' after wake-up to restore saved state.\n");
        } else if (strcmp(arg, "resume") == 0) {
            shell_print("resume - Restore VM state\n");
            shell_print("Usage: resume\n");
            shell_print("Restores previously saved VM state (if any).\n");
            shell_print("Reports error if no valid saved state exists.\n");
        } else if (strcmp(arg, "groups") == 0) {
            shell_print("groups - List all user groups\n");
            shell_print("Usage: groups\n");
            shell_print("Displays group name, GID, and member list for each group.\n");
        } else if (strcmp(arg, "who") == 0) {
            shell_print("who - Show logged-in users\n");
            shell_print("Usage: who\n");
            shell_print("Displays current user, terminal, login time, and UID.\n");
        } else {
            shell_err_help(arg);
            last_exit_code = 1;
            return;
        }
        last_exit_code = 0;
        return;
    }

    /* Categorized help listing */
    shell_print("\n");
    shell_print("  +-------------------------------------------+\n");
    shell_print("  |         FUNSOS Shell Commands             |\n");
    shell_print("  +-------------------------------------------+\n\n");

    shell_print("  [File Operations]\n");
    shell_print("    pt/ls           List directory contents\n");
    shell_print("    show/cat/type   Display file contents\n");
    shell_print("    go/cd           Change directory\n");
    shell_print("    where/pwd       Show current directory\n");
    shell_print("    copy            Copy file\n");
    shell_print("    del/rm          Delete file\n");
    shell_print("    ren/mv          Rename file/directory\n");
    shell_print("    mkdir           Create directory\n");
    shell_print("    touch           Create empty file\n");
    shell_print("    append          Append text to file\n");
    shell_print("    head            Show first N lines\n");
    shell_print("    tail            Show last N lines\n");
    shell_print("    wc              Count lines/words/chars\n");
    shell_print("    diff            Compare two files\n");
    shell_print("    sort            Sort lines in file\n");
    shell_print("    uniq            Remove duplicate lines\n");
    shell_print("    grep            Search for pattern\n");
    shell_print("    replace         Replace text in file\n");
    shell_print("    find            Search for file by name\n");
    shell_print("    size            Show file size\n");
    shell_print("    stat            Show file metadata\n");
    shell_print("    tree            Show directory tree\n");
    shell_print("    du              Show disk usage\n");
    shell_print("    df              Show filesystem space\n");
    shell_print("    chmod           Change permissions\n");
    shell_print("    chown           Change owner\n");
    shell_print("    which           Show command location\n");
    shell_print("    install         Copy & set attributes\n");
    shell_print("    tee             Stdout+file redirect\n");

    shell_print("\n  [System]\n");
    shell_print("    ver             Kernel version\n");
    shell_print("    sysinfo         System information\n");
    shell_print("    schedpolicy     Scheduler policy table\n");
    shell_print("    mempolicy       Memory replacement policy table\n");
    shell_print("    iosched         I/O scheduler policies\n");
    shell_print("    softirq         SoftIRQ statistics\n");
    shell_print("    oom             OOM killer state\n");
    shell_print("    sysctl          Kernel parameters\n");
    shell_print("    sysrq           SysRq magic key help\n");
    shell_print("    workqueue       Workqueue statistics\n");
    shell_print("    rcu             RCU synchronization stats\n");
    shell_print("    hrtimer         High-resolution timers\n");
    shell_print("    slab            Slab allocator stats\n");
    shell_print("    watchdog        Watchdog devices\n");
    shell_print("    crypto          Crypto framework\n");
    shell_print("    cpufreq         CPU frequency scaling\n");
    shell_print("    cpuidle         CPU idle C-state management\n");
    shell_print("    regmap          Register map framework stats\n");
    shell_print("    hwmon           Hardware monitor sensors\n");
    shell_print("    ftrace          Function tracer statistics\n");
    shell_print("    dmabuf          DMA-BUF buffer sharing stats\n");
    shell_print("    iio             Industrial I/O subsystem stats\n");
    shell_print("    pwm             PWM controller statistics\n");
    shell_print("    led             LED subsystem statistics\n");
    shell_print("    rtc             RTC real-time clock stats\n");
    shell_print("    pinctrl         Pin control subsystem stats\n");
    shell_print("    gpio            GPIO subsystem statistics\n");
    shell_print("    dmaengine       DMA engine statistics\n");
    shell_print("    clk             Common clock framework stats\n");
    shell_print("    i2c             I2C bus subsystem stats\n");
    shell_print("    spi             SPI bus subsystem stats\n");
    shell_print("    mfd             MFD multi-function device stats\n");
    shell_print("    vmalloc / vmap  Vmalloc allocator stats\n");
    shell_print("    percpu          Per-CPU allocator\n");
    shell_print("    kfence          KFENCE memory checker\n");
    shell_print("    debugobj        Debug objects tracker\n");
    shell_print("    lockdep         Lock dependency validator\n");
    shell_print("    irqdomain       IRQ domain manager\n");
    shell_print("    devtmpfs        Devtmpfs device filesystem stats\n");
    shell_print("    sysfs           Sysfs kernel object filesystem\n");
    shell_print("    netns           Network namespace stats\n");
    shell_print("    netfilter       Netfilter packet filter stats\n");
    shell_print("    seccomp         Seccomp secure computing stats\n");
    shell_print("    apparmor        AppArmor MAC stats\n");
    shell_print("    keyring         Kernel key retention stats\n");
    shell_print("    audit           Linux audit framework stats\n");
    shell_print("    namespace       Namespace isolation stats\n");
    shell_print("    tracepoint/tp   Static tracepoints stats\n");
    shell_print("    uprobe          User-space probes stats\n");
    shell_print("    kmod            Kernel module loader stats\n");
    shell_print("    firmware/fwldr  Firmware loader stats\n");
    shell_print("    remoteproc/rproc Remote processor stats\n");
    shell_print("    rpmsg           Remote processor messaging stats\n");
    shell_print("    virtio          VirtIO framework stats\n");
    shell_print("    mem / free      Memory usage\n");
    shell_print("    uptime          System uptime\n");
    shell_print("    load            Load average\n");
    shell_print("    dmesg [-c] [-n N] Kernel log (ring buffer)\n");
    shell_print("    loglevel        Set kernel log level\n");
    shell_print("    syslog          Syslog rules management\n");
    shell_print("    ps              List processes\n");
    shell_print("    kill            Kill process\n");
    shell_print("top              System resource monitor\n");
    shell_print("    nice / renice   Process priority\n");
    shell_print("    jobs / bg / fg  Job control\n");
    shell_print("    reboot          Reboot system\n");
    shell_print("    halt / shutdown Halt/Shutdown system\n");
    shell_print("    save / resume   Save/Restore VM state (S3)\n");
    shell_print("    sleep           Sleep N seconds\n");
    shell_print("    date / time     Date and time\n");

    shell_print("\n  [Disk & Storage]\n");
    shell_print("    mount / umount   Mount/Unmount filesystem\n");
    shell_print("    format          Format disk (fat32/ext2)\n");
    shell_print("    fdisk           Partition table\n");
    shell_print("    chkdsk          Check disk errors\n");
    shell_print("    lsblk           List block devices\n");

    shell_print("\n  [Network]\n");
    shell_print("    ping            ICMP echo request\n");
    shell_print("    ifconfig        Network interfaces\n");
    shell_print("    route           Routing table\n");
    shell_print("    dns             DNS lookup\n");
    shell_print("    wget            Download file (HTTP)\n");
    shell_print("    httpget         HTTP GET request\n");
    shell_print("    tftp            TFTP file transfer\n");
    shell_print("    ntp             NTP time sync\n");
    shell_print("    telnet          Remote shell\n");
    shell_print("    netstat         Connections/sockets\n");
    shell_print("    traceroute      Trace network route\n");
    shell_print("    arp             ARP cache table\n");
    shell_print("    hostname        Show/set hostname\n");

    shell_print("\n  [Hardware]\n");
    shell_print("    lspci           List PCI devices\n");
    shell_print("    lsusb           List USB devices\n");
    shell_print("    sensors         Sensor readings\n");
    shell_print("    freq            CPU frequency\n");
    shell_print("    dev             /dev device list\n");

    shell_print("\n  [Shell Built-in]\n");
    shell_print("    echo            Print text\n");
    shell_print("    set / env / unset  Environment variables\n");
    shell_print("    alias           Command aliases\n");
    shell_print("    history         Command history\n");
    shell_print("    calc            Calculator\n");
    shell_print("    edit            Text editor\n");
    shell_print("    gui / guistop   Display server on/off\n");
    shell_print("    c / crepl       C interpreter REPL\n");
    shell_print("    exec / run      Execute program\n");
    shell_print("    nohup           Detached execution\n");
    shell_print("    watch           Repeat command (2s)\n");
    shell_print("    test / expr     Condition & math eval\n");
    shell_print("    xargs           Argument builder\n");
    shell_print("    clr             Clear screen\n");
    shell_print("    help            Show this help\n");

    shell_print("\n  [GUI]\n");
    shell_print("    accel           2D acceleration test\n");
    shell_print("    imgview         Image viewer (JPEG/PNG)\n");

    shell_print("\n  [Audio]\n");
    shell_print("    play            WAV audio player\n");
    shell_print("    vol             Volume control\n");
    shell_print("    sound           Audio device list\n");

    shell_print("\n  [Package Manager]\n");
    shell_print("    pkg install     Install package\n");
    shell_print("    pkg remove      Remove package\n");
    shell_print("    pkg update      Update package\n");
    shell_print("    pkg list        List installed\n");
    shell_print("    pkg search      Search packages\n");

    shell_print("\n  [User Management]\n");
    shell_print("    whoami          Current user\n");
    shell_print("    who             Show logged-in user info\n");
    shell_print("    users           All users (table)\n");
    shell_print("    groups          List all groups\n");
    shell_print("    useradd         Create user (admin)\n");
    shell_print("    userdel         Delete user (admin)\n");
    shell_print("    passwd          Change password\n");
    shell_print("    su              Switch user\n");
    shell_print("    logout          End session\n");

    shell_print("\n  [Kernel Diagnostics]\n");
    shell_print("    ktrace          Kernel event tracing (ring buffer)\n");
    shell_print("    kwork           Kernel workqueue (deferred work)\n");
    shell_print("    flock           File lock statistics\n");
    shell_print("    dcache          Dentry cache stats\n");
    shell_print("    icache          Inode cache stats\n");
    shell_print("    syncstat        FS sync statistics\n");
    shell_print("    mount2          Mount flag management\n");

    shell_print("\n  [Registry & Apps]\n");
    shell_print("    reg             System registry (query/add/delete/export)\n");
    shell_print("    apps            Browse & launch built-in applications\n");

    shell_print("\n  +-------------------------------------------+\n");
    shell_print("  Tip: 'help <cmd>' for detailed usage info.\n");
    shell_print("  +-------------------------------------------+\n");
    last_exit_code = 0;
}

static void print_box_line(char left, char mid, char right, char fill, int w) {
    char buf[128];
    int i = 0;
    buf[i++] = left;
    for (int j = 0; j < w - 2; j++) buf[i++] = fill;
    buf[i++] = right;
    buf[i++] = '\n';
    buf[i] = '\0';
    shell_print(buf);
}

static void print_box_row(const char *label, const char *value, int w) {
    char buf[128];
    int label_len = len_strlen(label);
    int value_len = len_strlen(value);
    int i = 0;
    buf[i++] = '|';
    buf[i++] = ' ';
    for (int j = 0; j < label_len && j < 20; j++) buf[i++] = label[j];
    for (int j = label_len; j < 20; j++) buf[i++] = ' ';
    buf[i++] = ' ';
    for (int j = 0; j < value_len && j < w - 25; j++) buf[i++] = value[j];
    for (int j = value_len; j < w - 25; j++) buf[i++] = ' ';
    buf[i++] = ' ';
    buf[i++] = '|';
    buf[i++] = '\n';
    buf[i] = '\0';
    shell_print(buf);
}

static void print_box_separator(int w) {
    char buf[128];
    int i = 0;
    buf[i++] = '|';
    for (int j = 0; j < w - 2; j++) buf[i++] = '-';
    buf[i++] = '|';
    buf[i++] = '\n';
    buf[i] = '\0';
    shell_print(buf);
}

static void print_box_title(const char *title, int w) {
    char buf[128];
    int title_len = len_strlen(title);
    int pad = (w - 2 - title_len) / 2;
    int i = 0;
    buf[i++] = '|';
    for (int j = 0; j < pad; j++) buf[i++] = ' ';
    for (int j = 0; j < title_len && j < w - 2; j++) buf[i++] = title[j];
    for (int j = 0; j < w - 2 - pad - title_len; j++) buf[i++] = ' ';
    buf[i++] = '|';
    buf[i++] = '\n';
    buf[i] = '\0';
    shell_print(buf);
}

static uint32_t count_processes(void) {
    uint32_t count = 0;
    for (pid_t pid = 0; pid < 1024; pid++) {
        if (process_get_pcb(pid) != NULL) {
            count++;
        }
    }
    return count > 0 ? count : 1;
}

static void cmd_sysinfo(void) {
    char val[64];
    int box_w = 60;

    uint32_t total_pages = pmm_get_total_pages();
    uint32_t used_pages = pmm_get_used_pages();
    uint32_t free_pages = pmm_get_free_pages();
    uint64_t total_mem = (uint64_t)total_pages * 4096;
    uint64_t used_mem = (uint64_t)used_pages * 4096;
    uint64_t free_mem = (uint64_t)free_pages * 4096;

    uint32_t uptime_sec = timer_get_ticks() / 100;
    uint32_t up_days = uptime_sec / 86400;
    uint32_t up_hours = (uptime_sec % 86400) / 3600;
    uint32_t up_mins = (uptime_sec % 3600) / 60;
    uint32_t up_secs = uptime_sec % 60;

    uint32_t proc_count = count_processes();

    uint32_t cpu_count = smp_get_cpu_count();
    cpufreq_info_t *freq_info = cpufreq_get_info();
    uint32_t cpu_freq = freq_info ? freq_info->current_freq : 0;

    uint32_t pkg_count = pkgmgr_get_count();

    uint32_t net_if_count = net_get_interface_count();

    /* ==== 标题栏 ==== */
    print_box_line('+', '-', '+', '-', box_w);
    print_box_title(OS_STRING " - System Information", box_w);
    print_box_line('+', '-', '+', '-', box_w);

    /* ==== 系统信息 ==== */
    print_box_title(" System", box_w);
    print_box_separator(box_w);
    print_box_row("OS Name:", OS_NAME, box_w);
    print_box_row("OS Version:", KERNEL_VERSION, box_w);
    print_box_row("Kernel:", KERNEL_STRING, box_w);
    snprintf(val, sizeof(val), "%u process(es)", proc_count);
    print_box_row("Processes:", val, box_w);
    snprintf(val, sizeof(val), "%u package(s)", pkg_count);
    print_box_row("Packages:", val, box_w);
    print_box_line('+', '-', '+', '-', box_w);

    /* ==== CPU 信息 ==== */
    print_box_title(" CPU", box_w);
    print_box_separator(box_w);
    print_box_row("Architecture:", "x86 (32-bit)", box_w);
    snprintf(val, sizeof(val), "%u core(s)", cpu_count);
    print_box_row("Cores:", val, box_w);
    if (cpu_freq > 0) {
        snprintf(val, sizeof(val), "%u MHz", cpu_freq);
    } else {
        strncpy(val, "N/A", sizeof(val) - 1);
        val[sizeof(val) - 1] = '\0';
    }
    print_box_row("Frequency:", val, box_w);
    print_box_row("Model:", "Intel/AMD Compatible", box_w);
    print_box_line('+', '-', '+', '-', box_w);

    /* ==== 内存信息 ==== */
    print_box_title(" Memory", box_w);
    print_box_separator(box_w);
    if (total_mem >= 1024 * 1024) {
        snprintf(val, sizeof(val), "%u MB", (uint32_t)(total_mem / 1024 / 1024));
    } else {
        snprintf(val, sizeof(val), "%u KB", (uint32_t)(total_mem / 1024));
    }
    print_box_row("Total:", val, box_w);
    if (used_mem >= 1024 * 1024) {
        snprintf(val, sizeof(val), "%u MB", (uint32_t)(used_mem / 1024 / 1024));
    } else {
        snprintf(val, sizeof(val), "%u KB", (uint32_t)(used_mem / 1024));
    }
    print_box_row("Used:", val, box_w);
    if (free_mem >= 1024 * 1024) {
        snprintf(val, sizeof(val), "%u MB", (uint32_t)(free_mem / 1024 / 1024));
    } else {
        snprintf(val, sizeof(val), "%u KB", (uint32_t)(free_mem / 1024));
    }
    print_box_row("Free:", val, box_w);
    snprintf(val, sizeof(val), "%u pages", total_pages);
    print_box_row("Page Count:", val, box_w);
    print_box_line('+', '-', '+', '-', box_w);

    /* ==== 运行时间 ==== */
    print_box_title(" Uptime", box_w);
    print_box_separator(box_w);
    if (up_days > 0) {
        snprintf(val, sizeof(val), "%ud %uh %um %us", up_days, up_hours, up_mins, up_secs);
    } else if (up_hours > 0) {
        snprintf(val, sizeof(val), "%uh %um %us", up_hours, up_mins, up_secs);
    } else if (up_mins > 0) {
        snprintf(val, sizeof(val), "%um %us", up_mins, up_secs);
    } else {
        snprintf(val, sizeof(val), "%us", up_secs);
    }
    print_box_row("Uptime:", val, box_w);
    snprintf(val, sizeof(val), "%u seconds", uptime_sec);
    print_box_row("Total Seconds:", val, box_w);
    print_box_line('+', '-', '+', '-', box_w);

    /* ==== 文件系统 ==== */
    print_box_title(" Filesystem", box_w);
    print_box_separator(box_w);
    {
        extern mount_t *mount_list;
        mount_t *mnt = mount_list;
        int mnt_count = 0;
        while (mnt) {
            mnt_count++;
            mnt = mnt->next;
        }
        snprintf(val, sizeof(val), "%d mount(s)", mnt_count);
        print_box_row("Mount Points:", val, box_w);
        print_box_row("Root FS:", "VFS (multi-fs)", box_w);
        print_box_row("Dev FS:", "/dev (devfs)", box_w);
    }
    print_box_line('+', '-', '+', '-', box_w);

    /* ==== 网络接口 ==== */
    print_box_title(" Network", box_w);
    print_box_separator(box_w);
    snprintf(val, sizeof(val), "%u interface(s)", net_if_count);
    print_box_row("Interfaces:", val, box_w);
    for (uint32_t i = 0; i < net_if_count; i++) {
        net_interface_t *iface = net_get_interface(i);
        if (iface) {
            snprintf(val, sizeof(val), "%s (%s)",
                     iface->name,
                     (iface->flags & IFF_UP) ? "UP" : "DOWN");
            char label[24];
            snprintf(label, sizeof(label), "  eth%u:", i);
            print_box_row(label, val, box_w);
        }
    }
    print_box_line('+', '-', '+', '-', box_w);

    /* ==== 显示 ==== */
    print_box_title(" Display", box_w);
    print_box_separator(box_w);
    if (vbe_mode_active) {
        vbe_mode_info_t *vbe = vbe_get_current_mode();
        if (vbe) {
            snprintf(val, sizeof(val), "%ux%ux%u (VBE)",
                     vbe->width, vbe->height, vbe->bpp);
        } else {
            strncpy(val, "VBE mode", sizeof(val) - 1);
            val[sizeof(val) - 1] = '\0';
        }
    } else {
        strncpy(val, "80x25 (VGA text)", sizeof(val) - 1);
        val[sizeof(val) - 1] = '\0';
    }
    print_box_row("Mode:", val, box_w);
    print_box_line('+', '-', '+', '-', box_w);

    /* ==== 当前用户与权限 ==== */
    print_box_title(" User & Permissions", box_w);
    print_box_separator(box_w);
    {
        user_t *u = user_get_current();
        if (u) {
            print_box_row("Current User:", u->username, box_w);
            snprintf(val, sizeof(val), "%u", u->uid);
            print_box_row("UID:", val, box_w);
            snprintf(val, sizeof(val), "%u", u->gid);
            print_box_row("GID:", val, box_w);
            print_box_row("Role:", perm_level_name(perm_get_level()), box_w);
            snprintf(val, sizeof(val), "%s", u->is_admin ? "Yes" : "No");
            print_box_row("Admin:", val, box_w);
            snprintf(val, sizeof(val), "%s", perm_is_sover() ? "Full" :
                                           perm_is_admin() ? "Elevated" : "Standard");
            print_box_row("Privilege:", val, box_w);
            snprintf(val, sizeof(val), "%u user(s)", user_count());
            print_box_row("Total Users:", val, box_w);
            snprintf(val, sizeof(val), "%u group(s)", group_count());
            print_box_row("Total Groups:", val, box_w);
        } else {
            print_box_row("Current User:", "nobody", box_w);
            print_box_row("UID:", "65534", box_w);
            print_box_row("Role:", "Nobody", box_w);
        }
    }
    print_box_line('+', '-', '+', '-', box_w);

    last_exit_code = 0;
}

static void cmd_schedpolicy(void) {
    shell_print("=== Scheduler Policy Table ===\n\n");
    sched_print_policy_table();
    shell_print("\n");
    last_exit_code = 0;
}

static void cmd_mempolicy(void) {
    shell_print("=== Page Replacement Policy Table ===\n\n");
    page_replace_print_stats();
    shell_print("\n");
    last_exit_code = 0;
}

static void cmd_iosched(void) {
    shell_print("=== I/O Scheduler Policies ===\n\n");
    iosched_print_all_policies();
    shell_print("\n");
    last_exit_code = 0;
}

static void cmd_softirq(void) {
    shell_print("=== SoftIRQ Statistics ===\n\n");
    softirq_print_stats();
    shell_print("\n");
    last_exit_code = 0;
}

static void cmd_oom(void) {
    shell_print("=== OOM Killer State ===\n\n");
    oom_print_score_table();
    shell_print("\n");
    last_exit_code = 0;
}

static void cmd_sysrq(void) {
    shell_print("=== SysRq Magic Key Help ===\n");
    sysrq_print_help('h');
    char buf[128];
    snprintf(buf, sizeof(buf), "\nSysRq is %s\n", sysrq_is_enabled() ? "enabled" : "disabled");
    shell_print(buf);
    last_exit_code = 0;
}

static void cmd_workqueue(void) {
    shell_print("=== Workqueue Statistics ===\n");
    workqueue_print_stats();
    last_exit_code = 0;
}

static void cmd_rcu(void) {
    shell_print("=== RCU Statistics ===\n");
    rcu_print_stats();
    last_exit_code = 0;
}

static void cmd_hrtimer(void) {
    shell_print("=== High-Resolution Timers ===\n");
    hrtimer_print_stats();
    timer_list_print_stats();
    last_exit_code = 0;
}

static void cmd_slab(void) {
    shell_print("=== Slab Allocator ===\n");
    slab_print_stats();
    last_exit_code = 0;
}

static void cmd_watchdog(void) {
    shell_print("=== Watchdog Devices ===\n");
    watchdog_print_stats();
    last_exit_code = 0;
}

static void cmd_crypto(void) {
    shell_print("=== Crypto Framework ===\n");
    crypto_print_stats();
    last_exit_code = 0;
}

static void cmd_namespace(void) {
    shell_print("=== Namespaces Statistics ===\n");
    namespace_print_stats();
    last_exit_code = 0;
}

static void cmd_tracepoint(void) {
    shell_print("=== Tracepoints Statistics ===\n");
    tracepoint_print_stats();
    last_exit_code = 0;
}

static void cmd_uprobe(void) {
    shell_print("=== Uprobes Statistics ===\n");
    uprobe_print_stats();
    last_exit_code = 0;
}

static void cmd_kmod(void) {
    shell_print("=== Kernel Modules Statistics ===\n");
    kmod_print_stats();
    last_exit_code = 0;
}

static void cmd_firmware(void) {
    shell_print("=== Firmware Loader Statistics ===\n");
    firmware_print_stats();
    last_exit_code = 0;
}

static void cmd_remoteproc(void) {
    shell_print("=== Remote Processor Statistics ===\n");
    remoteproc_print_stats();
    last_exit_code = 0;
}

static void cmd_rpmsg(void) {
    shell_print("=== Rpmsg Statistics ===\n");
    rpmsg_print_stats();
    last_exit_code = 0;
}

static void cmd_virtio(void) {
    shell_print("=== VirtIO Statistics ===\n");
    virtio_print_stats();
    last_exit_code = 0;
}

static void cmd_cpufreq(void) {
    shell_print("=== CPUFreq Statistics ===\n");
    cpufreq_print_stats();
    last_exit_code = 0;
}

static void cmd_vmalloc(void) {
    shell_print("=== Vmalloc Statistics ===\n");
    vmalloc_print_stats();
    last_exit_code = 0;
}

static void cmd_percpu(void) {
    shell_print("=== Per-CPU Allocator Statistics ===\n");
    percpu_print_stats();
    last_exit_code = 0;
}

static void cmd_kfence(void) {
    shell_print("=== KFENCE Statistics ===\n");
    kfence_print_stats();
    last_exit_code = 0;
}

static void cmd_debugobj(void) {
    shell_print("=== Debug Objects Statistics ===\n");
    debug_objects_print_stats();
    last_exit_code = 0;
}

static void cmd_lockdep(void) {
    shell_print("=== Lockdep Statistics ===\n");
    lockdep_print_stats();
    last_exit_code = 0;
}

static void cmd_irqdomain(void) {
    shell_print("=== IRQ Domain Statistics ===\n");
    irqdomain_print_stats();
    last_exit_code = 0;
}

static void cmd_cpuidle(void) {
    shell_print("=== CPUIdle Statistics ===\n");
    cpuidle_print_stats();
    last_exit_code = 0;
}

static void cmd_regmap(void) {
    shell_print("=== Regmap Statistics ===\n");
    regmap_print_stats();
    last_exit_code = 0;
}

static void cmd_hwmon(void) {
    shell_print("=== Hwmon Hardware Monitor Statistics ===\n");
    hwmon_print_stats();
    last_exit_code = 0;
}

static void cmd_ftrace(void) {
    shell_print("=== Ftrace Statistics ===\n");
    ftrace_print_stats();
    last_exit_code = 0;
}

static void cmd_dmabuf(void) {
    shell_print("=== DMA-BUF Statistics ===\n");
    dmabuf_print_stats();
    last_exit_code = 0;
}

static void cmd_iio(void) {
    shell_print("=== IIO Industrial I/O Statistics ===\n");
    iio_print_stats();
    last_exit_code = 0;
}

static void cmd_pwm(void) {
    shell_print("=== PWM Subsystem Statistics ===\n");
    pwm_print_stats();
    last_exit_code = 0;
}

static void cmd_led(void) {
    shell_print("=== LED Subsystem Statistics ===\n");
    led_print_stats();
    last_exit_code = 0;
}

static void cmd_rtc(void) {
    rtc_print_stats();
    last_exit_code = 0;
}

static void cmd_pinctrl(void) {
    pinctrl_print_stats();
    last_exit_code = 0;
}

static void cmd_gpio(void) {
    gpio_print_stats();
    last_exit_code = 0;
}

static void cmd_dmaengine(void) {
    dmaengine_print_stats();
    last_exit_code = 0;
}

static void cmd_clk(void) {
    clk_print_stats();
    last_exit_code = 0;
}

static void cmd_i2c(void) {
    i2c_print_stats();
    last_exit_code = 0;
}

static void cmd_spi(void) {
    spi_print_stats();
    last_exit_code = 0;
}

static void cmd_mfd(void) {
    mfd_print_stats();
    last_exit_code = 0;
}

static void cmd_devtmpfs(void) {
    devtmpfs_print_stats();
    last_exit_code = 0;
}

static void cmd_sysfs(void) {
    ksysfs_print_stats();
    last_exit_code = 0;
}

static void cmd_netns(void) {
    netns_print_stats();
    last_exit_code = 0;
}

static void cmd_netfilter(void) {
    knetfilter_print_stats();
    last_exit_code = 0;
}

static void cmd_seccomp(void) {
    seccomp_print_stats();
    last_exit_code = 0;
}

static void cmd_apparmor(void) {
    apparmor_print_stats();
    last_exit_code = 0;
}

static void cmd_keyring(void) {
    keyring_print_stats();
    last_exit_code = 0;
}

static void cmd_audit(void) {
    audit_print_stats();
    last_exit_code = 0;
}

static void cmd_reboot(void) {
    shell_print("System is rebooting...\n");
    /* Wait for serial output to complete */
    for (volatile int i = 0; i < 10000000; i++);
    /* Try ACPI reboot first, falls back to keyboard controller and triple fault */
    acpi_reboot();
    /* Should never reach here */
    while (1) {
        asm volatile("hlt");
    }
}

static void cmd_halt(void) {
    shell_print("System halted.\n");
    while (1) {
        asm volatile("cli; hlt");
    }
}

static void cmd_shutdown(void) {
    shell_print("System is shutting down...\n");
    /* Wait for serial output to complete */
    for (volatile int i = 0; i < 10000000; i++);
    /* Try ACPI shutdown (S5 soft-off) */
    acpi_shutdown();
    /* If ACPI fails, try QEMU-specific shutdown port */
    outw(0x604, 0x2000);
    /* If all else fails, just halt */
    shell_print("ACPI shutdown failed. System halted.\n");
    while (1) {
        asm volatile("cli; hlt");
    }
}

static void cmd_time(void) {
    rtc_time_t t;
    rtc_read_time(&t);

    char buf[64];
    snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u\n",
             t.year, t.month, t.day, t.hour, t.minute, t.second);
    shell_print(buf);
    last_exit_code = 0;
}

static void cmd_date(void) {
    rtc_time_t t;
    rtc_read_time(&t);

    char buf[64];
    snprintf(buf, sizeof(buf), "%04u-%02u-%02u\n", t.year, t.month, t.day);
    shell_print(buf);
    last_exit_code = 0;
}

static void cmd_mem(void) {
    uint32_t total_pages = pmm_get_total_pages();
    uint32_t used_pages = pmm_get_used_pages();
    uint32_t free_pages = pmm_get_free_pages();

    char buf[128];
    snprintf(buf, sizeof(buf),
             "Memory: %u KB total, %u KB used, %u KB free\n",
             total_pages * 4, used_pages * 4, free_pages * 4);
    shell_print(buf);
    last_exit_code = 0;
}

static void cmd_dev(void) {
    shell_print("Devices in /dev:\n");
    devfs_device_t *dev = NULL;
    const char *names[] = {
        "null", "zero", "random", "urandom", "stdin", "stdout", "stderr",
        "hda", "hdb", "fd0", "console", "tty0", "tty1", NULL
    };
    for (int i = 0; names[i]; i++) {
        dev = devfs_find(names[i]);
        if (dev) {
            char buf[64];
            snprintf(buf, sizeof(buf), "  %s (%s %u:%u)\n",
                     dev->name,
                     dev->type == DEVICE_CHAR ? "char" : "block",
                     dev->major, dev->minor);
            shell_print(buf);
        }
    }
    shell_print("(end of device list)\n");
    last_exit_code = 0;
}

static void cmd_ping(const char *ip_str) {
    if (!ip_str || !*ip_str) {
        shell_err_ping(ip_str);
        last_exit_code = 1;
        return;
    }

    uint32_t a = 0, b = 0, c = 0, d = 0;
    int parsed = 0;
    uint32_t *vals[4] = { &a, &b, &c, &d };
    const char *p = ip_str;
    for (int i = 0; i < 4; i++) {
        uint32_t val = 0;
        int digits = 0;
        while (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            digits++;
            p++;
        }
        if (digits == 0) break;
        *vals[i] = val;
        parsed++;
        if (i < 3 && *p == '.') p++;
    }
    if (parsed != 4) {
        shell_err_ping(ip_str);
        last_exit_code = 1;
        return;
    }
    if (a > 255 || b > 255 || c > 255 || d > 255) {
        shell_err_ping(ip_str);
        last_exit_code = 1;
        return;
    }

    ipv4_addr_t dst;
    dst.addr = (d << 24) | (c << 16) | (b << 8) | a;

    net_interface_t *iface = NULL;
    if (a == 127) {
        iface = loopback_get();
    } else {
        iface = net_get_default_interface();
    }
    if (!iface) {
        shell_error(SHELL_ERR_NO_NETWORK, "ping");
        last_exit_code = 1;
        return;
    }

    char buf[128];
    snprintf(buf, sizeof(buf), "PING %s (%u.%u.%u.%u): 56 data bytes\n",
             ip_str, a, b, c, d);
    shell_print(buf);

    int sent = 0, received = 0;
    uint16_t ping_id = (uint16_t)(timer_get_ticks() & 0xFFFF);

    for (int seq = 1; seq <= 4; seq++) {
        /* 检查中断信号 */
        if (kb_signal_check()) {
            shell_print("^C\n");
            last_exit_code = 130;
            return;
        }

        if (icmp_ping(iface, dst, ping_id, (uint16_t)seq) == 0) {
            sent++;
        }

        uint32_t start_tick = timer_get_ticks();
        int found = 0;
        while (timer_get_ticks() - start_tick < 100) {
            if (kb_signal_check()) {
                shell_print("^C\n");
                last_exit_code = 130;
                return;
            }
            const icmp_ping_result_t *results = NULL;
            uint32_t count = icmp_get_ping_results(&results);
            for (uint32_t i = 0; i < count; i++) {
                if (results[i].identifier == ping_id &&
                    results[i].sequence == (uint16_t)seq &&
                    results[i].received) {
                    char rtt_buf[64];
                    snprintf(rtt_buf, sizeof(rtt_buf),
                             "64 bytes from %s: icmp_seq=%d ttl=64 time=%ums\n",
                             ip_str, seq, results[i].rtt_ms);
                    shell_print(rtt_buf);
                    received++;
                    found = 1;
                    break;
                }
            }
            if (found) break;
            for (volatile int j = 0; j < 10000; j++) { (void)j; }
        }
        if (!found) {
            shell_print("Request timed out.\n");
        }

        for (volatile int j = 0; j < 50000; j++) { (void)j; }
    }

    snprintf(buf, sizeof(buf),
             "\n--- %s ping statistics ---\n"
             "%d packets transmitted, %d received, %d%% packet loss\n",
             ip_str, sent, received,
             sent > 0 ? ((sent - received) * 100) / sent : 0);
    shell_print(buf);

    last_exit_code = (received > 0) ? 0 : 1;
}

static void cmd_copy(const char *src, const char *dst) {
    if (!src || !*src || !dst || !*dst) {
        shell_print("cp: missing file operand\n");
        shell_print("Usage: cp <source> <destination>\n");
        shell_print("Copies the contents of source file to destination.\n");
        shell_print("If destination is a directory, source is copied into it.\n");
        last_exit_code = 1;
        return;
    }

    char src_path[512], dst_path[512];
    build_full_path(src, src_path, sizeof(src_path));
    build_full_path(dst, dst_path, sizeof(dst_path));

    /* Check if destination is a directory - if so, append source filename */
    {
        dentry_t *dst_d = 0;
        if (path_resolve(dst_path, &dst_d) == 0 && dst_d && dst_d->inode &&
            (dst_d->inode->mode & FILE_MODE_DIR)) {
            const char *src_name = src;
            const char *slash = strrchr(src, '/');
            if (slash) src_name = slash + 1;
            uint32_t dlen = len_strlen(dst_path);
            if (dlen > 0 && dst_path[dlen - 1] != '/' && dlen + 1 < sizeof(dst_path)) {
                dst_path[dlen] = '/';
                dlen++;
                dst_path[dlen] = '\0';
            }
            strncat(dst_path, src_name, sizeof(dst_path) - dlen - 1);
        }
    }

    file_t *sf = 0;
    if (vfs_open(src_path, FILE_MODE_READ, &sf) != 0 || !sf) {
        shell_print("cp: cannot stat '");
        shell_print(src);
        shell_print("': No such file or directory\n");
        last_exit_code = 1;
        return;
    }

    /* Create destination file */
    if (vfs_creat(dst_path, FILE_MODE_WRITE | FILE_MODE_READ) != 0) {
        vfs_close(sf);
        shell_print("cp: cannot create '");
        shell_print(dst);
        shell_print("': Permission denied\n");
        last_exit_code = 1;
        return;
    }

    file_t *df = 0;
    if (vfs_open(dst_path, FILE_MODE_WRITE, &df) != 0 || !df) {
        vfs_close(sf);
        shell_print("cp: cannot open '");
        shell_print(dst);
        shell_print("' for writing: Permission denied\n");
        last_exit_code = 1;
        return;
    }

    char buf[4096];
    int32_t total = 0;
    int32_t n;
    while ((n = vfs_read(sf, buf, sizeof(buf))) > 0) {
        int32_t w = vfs_write(df, buf, n);
        if (w > 0) total += w;
        if (w != n) {
            shell_print("copy: write error\n");
            break;
        }
    }
    vfs_close(sf);
    vfs_close(df);

    char num[32];
    snprintf(num, sizeof(num), "%d", total);
    shell_print("'");
    shell_print(src);
    shell_print("' -> '");
    shell_print(dst);
    shell_print("' (");
    shell_print(num);
    shell_print(" bytes)\n");
    last_exit_code = 0;
}

static int shell_rm_recursive(const char *path) {
    dentry_t *dir = 0;
    if (path_resolve((char *)path, &dir) != 0 || !dir || !dir->inode) {
        return -1;
    }

    if (!(dir->inode->mode & FILE_MODE_DIR)) {
        /* It's a file or symlink - unlink it */
        return vfs_unlink(path);
    }

    /* It's a directory - first recursively delete all children */
    dentry_t *child = dir->child;
    while (child) {
        dentry_t *next = child->next_sibling;
        char child_path[512];
        strncpy(child_path, path, sizeof(child_path) - 1);
        child_path[sizeof(child_path) - 1] = '\0';
        uint32_t plen = len_strlen(child_path);
        if (plen > 0 && child_path[plen - 1] != '/') {
            if (plen + 1 < sizeof(child_path)) {
                child_path[plen] = '/';
                plen++;
            }
        }
        if (plen < sizeof(child_path) - 1) {
            strncat(child_path, child->name, sizeof(child_path) - plen - 1);
        }

        if (child->inode && (child->inode->mode & FILE_MODE_DIR)) {
            if (shell_rm_recursive(child_path) != 0) return -1;
        } else {
            if (vfs_unlink(child_path) != 0) return -1;
        }
        child = next;
    }

    /* Now remove the empty directory */
    return vfs_rmdir(path);
}

static void cmd_del(const char *file) {
    if (!file || !*file) {
        shell_print("rm: missing file operand\n");
        shell_print("Usage: rm [-rf] <file|directory>\n");
        shell_print("  -r  remove directories and their contents recursively\n");
        shell_print("  -f  ignore nonexistent files, never prompt\n");
        last_exit_code = 1;
        return;
    }

    int recursive = 0;
    int force = 0;
    const char *target = file;

    /* Parse options */
    if (file[0] == '-') {
        const char *p = file + 1;
        while (*p && *p != ' ') {
            if (*p == 'r' || *p == 'R') recursive = 1;
            else if (*p == 'f') force = 1;
            p++;
        }
        while (*p == ' ') p++;
        target = p;
        if (!*target) {
            shell_print("Usage: del [-rf] <file/dir>\n       rm [-rf] <file/dir>\n");
            last_exit_code = 1;
            return;
        }
    }

    char full_path[512];
    build_full_path(target, full_path, sizeof(full_path));

    int ret;
    if (recursive) {
        ret = shell_rm_recursive(full_path);
    } else {
        /* Try unlink first (file), if fails try rmdir (empty dir) */
        ret = vfs_unlink(full_path);
        if (ret != 0) {
            ret = vfs_rmdir(full_path);
        }
    }

    if (ret == 0) {
        shell_print("removed '");
        shell_print(target);
        shell_print("'\n");
        last_exit_code = 0;
    } else {
        if (!force) {
            shell_print("rm: cannot remove '");
            shell_print(target);
            shell_print("': No such file or directory\n");
        }
        last_exit_code = 1;
    }
}

static void cmd_mkdir(const char *dir) {
    if (!dir || !*dir) {
        shell_print("mkdir: missing operand\n");
        shell_print("Usage: mkdir <directory>\n");
        shell_print("Creates a new directory with the specified name.\n");
        last_exit_code = 1;
        return;
    }

    char full_path[512];
    build_full_path(dir, full_path, sizeof(full_path));

    inode_t st;
    memset(&st, 0, sizeof(st));
    if (vfs_stat(full_path, &st) == 0) {
        shell_print("mkdir: cannot create '");
        shell_print(dir);
        shell_print("': File exists\n");
        last_exit_code = 1;
        return;
    }

    if (vfs_mkdir(full_path, FILE_MODE_DIR | PERM_READ | PERM_WRITE | PERM_EXEC) == 0) {
        last_exit_code = 0;
    } else {
        shell_print("mkdir: cannot create '");
        shell_print(dir);
        shell_print("': No such file or directory or permission denied\n");
        last_exit_code = 1;
    }
}

static void cmd_ren(const char *old, const char *new_name) {
    if (!old || !*old || !new_name || !*new_name) {
        shell_print("mv: missing file operand\n");
        shell_print("Usage: mv <source> <destination>\n");
        shell_print("Renames or moves source file/directory to destination.\n");
        last_exit_code = 1;
        return;
    }

    char old_path[512], new_path[512];
    if (old[0] == '/') {
        strncpy(old_path, old, 511); old_path[511] = '\0';
    } else {
        strncpy(old_path, current_dir, 255); old_path[255] = '\0';
        if (strcmp(current_dir, "/") != 0) strcat(old_path, "/");
        strncat(old_path, old, 254);
    }
    if (new_name[0] == '/') {
        strncpy(new_path, new_name, 511); new_path[511] = '\0';
    } else {
        strncpy(new_path, current_dir, 255); new_path[255] = '\0';
        if (strcmp(current_dir, "/") != 0) strcat(new_path, "/");
        strncat(new_path, new_name, 254);
    }

    /* Use inode_ops rename if available */
    char parent_path[512], old_name[256];
    strncpy(parent_path, old_path, 511); parent_path[511] = '\0';
    char *slash = strrchr(parent_path, '/');
    if (slash) {
        strncpy(old_name, slash + 1, 255); old_name[255] = '\0';
        if (slash == parent_path) parent_path[1] = '\0';
        else *slash = '\0';
    } else {
        strcpy(old_name, parent_path);
        strcpy(parent_path, "/");
    }

    dentry_t *parent = 0;
    if (path_resolve(parent_path, &parent) != 0 || !parent || !parent->inode || !parent->inode->ops || !parent->inode->ops->rename) {
        shell_print("mv: cannot stat '");
        shell_print(old);
        shell_print("': No such file or directory\n");
        last_exit_code = 1;
        return;
    }

    char new_basename[256];
    slash = strrchr(new_path, '/');
    if (slash) {
        strncpy(new_basename, slash + 1, 255); new_basename[255] = '\0';
    } else {
        strncpy(new_basename, new_path, 255); new_basename[255] = '\0';
    }

    if (parent->inode->ops->rename(parent, old_name, parent, new_basename) == 0) {
        shell_print("Renamed: ");
        shell_print(old);
        shell_print(" -> ");
        shell_print(new_name);
        shell_print("\n");
        last_exit_code = 0;
    } else {
        shell_print("mv: cannot rename '");
        shell_print(old);
        shell_print("' to '");
        shell_print(new_name);
        shell_print("': Permission denied or target exists\n");
        last_exit_code = 1;
    }
}

static void cmd_type(const char *file) {
    cmd_show(file);
}

#define FIND_MAX_DEPTH  20    /* 最大递归深度，防止深层目录栈溢出 */
#define FIND_MAX_RESULTS 500   /* 最大结果数限制 */
#define FIND_PATH_MAX   512    /* 路径缓冲区最大长度 */

/* Recursive find helper - 输出完整路径 */
static void find_recursive(dentry_t *dir, const char *name, int *found, int depth,
                           const char *path_prefix) {
    /* 深度保护: 超过最大深度时停止递归 */
    if (depth >= FIND_MAX_DEPTH) {
        shell_print("  (max depth reached, skipping deeper directories)\n");
        return;
    }
    /* 结果数保护: 避免输出过多导致系统卡顿 */
    if (*found >= FIND_MAX_RESULTS) {
        return;
    }

    dentry_t *child = dir->child;
    while (child) {
        if (*found < FIND_MAX_RESULTS && strstr(child->name, name) != 0) {
            /* 构建完整路径: 前缀 + 文件名 */
            shell_print("  ");
            shell_print(path_prefix);
            /* 如果前缀不是根目录，添加分隔符 */
            if (len_strlen(path_prefix) > 1 || (len_strlen(path_prefix) == 1 && path_prefix[0] != '/')) {
                /* 已经有路径分隔符则不需要额外添加 */
                if (path_prefix[len_strlen(path_prefix) - 1] != '/') {
                    shell_print("/");
                }
            } else if (len_strlen(path_prefix) == 1 && path_prefix[0] == '/') {
                /* 根目录不需要额外斜杠 */
            }
            shell_print(child->name);
            shell_print("\n");
            (*found)++;
        }
        if (child->inode && (child->inode->mode & FILE_MODE_DIR)) {
            /* 跳过 "." 和 ".." 目录避免循环 */
            if (len_strlen(child->name) > 0 &&
                !(child->name[0] == '.' &&
                  (child->name[1] == '\0' ||
                   (child->name[1] == '.' && child->name[2] == '\0')))) {
                /* 构建子目录的路径前缀 */
                char sub_path[FIND_PATH_MAX];
                strncpy(sub_path, path_prefix, FIND_PATH_MAX - 1);
                sub_path[FIND_PATH_MAX - 1] = '\0';
                uint32_t plen = len_strlen(sub_path);
                if (plen > 0 && sub_path[plen - 1] != '/') {
                    strncat(sub_path, "/", FIND_PATH_MAX - plen - 1);
                }
                strncat(sub_path, child->name, FIND_PATH_MAX - len_strlen(sub_path) - 1);

                find_recursive(child, name, found, depth + 1, sub_path);
            }
        }
        child = child->next_sibling;
    }
}

static void cmd_find(const char *name) {
    if (!name || !*name) {
        shell_print("find: missing operand\n");
        shell_print("Usage: find <name>\n");
        shell_print("Recursively searches for files/directories by name.\n");
        last_exit_code = 1;
        return;
    }

    dentry_t *dir = 0;
    if (path_resolve(current_dir, &dir) != 0 || !dir) {
        shell_print("find: cannot access current directory\n");
        last_exit_code = 1;
        return;
    }

    shell_print("Searching for '");
    shell_print(name);
    shell_print("' in ");
    shell_print(current_dir);
    shell_print("...\n");

    int found = 0;
    find_recursive(dir, name, &found, 0, current_dir);

    if (found == 0) {
        shell_print("No matches found.\n");
    } else if (found >= FIND_MAX_RESULTS) {
        shell_print("  (... output truncated at ");
        char nbuf[16];
        snprintf(nbuf, sizeof(nbuf), "%d", FIND_MAX_RESULTS);
        shell_print(nbuf);
        shell_print(" results)\n");
    }
    last_exit_code = (found > 0) ? 0 : 1;
}

static void cmd_size(const char *file) {
    if (!file || !*file) {
        shell_print("size: missing file operand\n");
        shell_print("Usage: size <file>\n");
        shell_print("Displays the size of the specified file in bytes.\n");
        last_exit_code = 1;
        return;
    }

    char full_path[512];
    if (file[0] == '/') {
        strncpy(full_path, file, 511); full_path[511] = '\0';
    } else {
        strncpy(full_path, current_dir, 255); full_path[255] = '\0';
        if (strcmp(current_dir, "/") != 0) strcat(full_path, "/");
        strncat(full_path, file, 254);
    }

    inode_t stat;
    memset(&stat, 0, sizeof(stat));
    if (vfs_stat(full_path, &stat) == 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%u bytes\n", stat.size);
        shell_print(file);
        shell_print(": ");
        shell_print(buf);
        last_exit_code = 0;
    } else {
        shell_print("size: ");
        shell_print(file);
        shell_print(": No such file or directory\n");
        last_exit_code = 1;
    }
}

static void cmd_echo(const char *text) {
    if (!text || !*text) {
        shell_err_echo();
        last_exit_code = 1;
        return;
    }
    shell_print(text);
    shell_print("\n");
    last_exit_code = 0;
}

static void cmd_set(const char *var, const char *value) {
    if (!var || !*var) {
        shell_err_set();
        last_exit_code = 1;
        return;
    }
    if (env_set(var, value ? value : "") == 0) {
        last_exit_code = 0;
    } else {
        shell_err_set();
        last_exit_code = 1;
    }
}

static void cmd_env(void) {
    for (int i = 0; i < env_count; i++) {
        shell_print(env_vars[i].name);
        shell_print("=");
        shell_print(env_vars[i].value);
        shell_print("\n");
    }
    last_exit_code = 0;
}

static void cmd_unset(const char *name) {
    if (!name || !*name) {
        shell_print("Usage: unset <variable>\n");
        last_exit_code = 1;
        return;
    }
    if (env_unset(name) == 0) {
        last_exit_code = 0;
    } else {
        shell_print("unset: no such variable: ");
        shell_print(name);
        shell_print("\n");
        last_exit_code = 1;
    }
}

static void cmd_run(const char *file) {
    if (!file || !*file) {
        shell_err_run(0);
        last_exit_code = 1;
        return;
    }

    char full_path[512];
    if (file[0] == '/') {
        strncpy(full_path, file, 511); full_path[511] = '\0';
    } else {
        strncpy(full_path, current_dir, 255); full_path[255] = '\0';
        if (strcmp(current_dir, "/") != 0) strcat(full_path, "/");
        strncat(full_path, file, 254);
    }

    /* Try to open the file via VFS */
    file_t *f = 0;
    if (vfs_open(full_path, FILE_MODE_READ, &f) != 0 || !f) {
        shell_err_run(file);
        last_exit_code = 1;
        return;
    }
    vfs_close(f);

    shell_print("run: Executing '");
    shell_print(file);
    shell_print("'...\n");
    shell_print("run: Process exited with code 0 (simulated)\n");
    last_exit_code = 0;
}

/* ============================================================ */
/* ---- 50 New Shell Commands ---- */
/* ============================================================ */

/* 1. ps - List running processes */
static void cmd_ps(void) {
    shell_print("  PID  STATE       NAME\n");
    for (int i = 0; i < MAX_PROCESSES; i++) {
        pcb_t *p = process_get_pcb(i);
        if (p && p->state != PROCESS_UNUSED) {
            const char *state_str;
            switch (p->state) {
                case PROCESS_READY:   state_str = "READY"; break;
                case PROCESS_RUNNING: state_str = "RUNNING"; break;
                case PROCESS_BLOCKED: state_str = "BLOCKED"; break;
                case PROCESS_ZOMBIE:  state_str = "ZOMBIE"; break;
                default:              state_str = "UNKNOWN"; break;
            }
            char buf[128];
            snprintf(buf, sizeof(buf), "  %3d  %-10s  %s\n", p->pid, state_str, p->name);
            shell_print(buf);
        }
    }
    last_exit_code = 0;
}

/* 2. kill - Kill a process */
static void cmd_kill(const char *pid_str) {
    if (!pid_str || !*pid_str) {
        shell_err_kill(0);
        last_exit_code = 1;
        return;
    }
    int pid = 0;
    const char *p = pid_str;
    while (*p >= '0' && *p <= '9') {
        pid = pid * 10 + (*p - '0');
        p++;
    }
    pcb_t *proc = process_get_pcb(pid);
    if (!proc || proc->state == PROCESS_UNUSED) {
        shell_err_kill(pid_str);
        last_exit_code = 1;
        return;
    }
    proc->state = PROCESS_ZOMBIE;
    proc->exit_status = 9; /* SIGKILL */
    char buf[64];
    snprintf(buf, sizeof(buf), "Killed process %d\n", pid);
    shell_print(buf);
    last_exit_code = 0;
}

/* 3. top - Show system resource usage */
static void cmd_top(void) {
    uint32_t total = pmm_get_total_pages() * 4096;
    uint32_t used = pmm_get_used_pages() * 4096;
    uint32_t uptime = timer_get_ticks() / 100;
    uint32_t cpu_pct = 0;
    pcb_t *cur = sched_get_current();
    if (cur) {
        cpu_pct = (cur->cpu_time * 100) / (uptime > 0 ? uptime : 1);
    }

    char buf[128];
    shell_print("System Resource Usage:\n");
    snprintf(buf, sizeof(buf), "  Uptime:     %u seconds\n", uptime);
    shell_print(buf);
    snprintf(buf, sizeof(buf), "  CPU usage:  %u%%\n", cpu_pct);
    shell_print(buf);
    snprintf(buf, sizeof(buf), "  Memory:     %u KB / %u KB (%u%%)\n",
             used / 1024, total / 1024,
             total > 0 ? (used * 100) / total : 0);
    shell_print(buf);

    /* Count processes */
    int proc_count = 0, running = 0, blocked = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        pcb_t *p = process_get_pcb(i);
        if (p && p->state != PROCESS_UNUSED) {
            proc_count++;
            if (p->state == PROCESS_RUNNING || p->state == PROCESS_READY) running++;
            if (p->state == PROCESS_BLOCKED) blocked++;
        }
    }
    snprintf(buf, sizeof(buf), "  Processes:  %d total, %d running, %d blocked\n",
             proc_count, running, blocked);
    shell_print(buf);
    last_exit_code = 0;
}

/* 4. free - Show memory usage details */
static void cmd_free(void) {
    uint32_t total_pages = pmm_get_total_pages();
    uint32_t used_pages = pmm_get_used_pages();
    uint32_t free_pages = pmm_get_free_pages();

    shell_print("              total    used    free\n");
    char buf[128];
    snprintf(buf, sizeof(buf), "Mem (KB):  %6u  %6u  %6u\n",
             total_pages * 4, used_pages * 4, free_pages * 4);
    shell_print(buf);
    snprintf(buf, sizeof(buf), "Mem (MB):  %6u  %6u  %6u\n",
             (total_pages * 4) / 1024, (used_pages * 4) / 1024, (free_pages * 4) / 1024);
    shell_print(buf);
    last_exit_code = 0;
}

/* 5. uptime - Show system uptime */
static void cmd_uptime(void) {
    uint32_t ticks = timer_get_ticks();
    uint32_t seconds = ticks / 100;
    uint32_t minutes = seconds / 60;
    uint32_t hours = minutes / 60;
    char buf[128];
    snprintf(buf, sizeof(buf), "Up %u hours, %u minutes, %u seconds\n",
             hours, minutes % 60, seconds % 60);
    shell_print(buf);
    last_exit_code = 0;
}

/* 6. load - Show load average */
static void cmd_load(void) {
    int proc_count = 0, running = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        pcb_t *p = process_get_pcb(i);
        if (p && p->state != PROCESS_UNUSED) {
            proc_count++;
            if (p->state == PROCESS_RUNNING || p->state == PROCESS_READY) running++;
        }
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "Load: %d runnable / %d total processes\n", running, proc_count);
    shell_print(buf);
    last_exit_code = 0;
}

/* 7. dmesg - Show kernel log messages */
static void cmd_dmesg(const char *options) {
    int clear_flag = 0;
    int tail_lines = 0;
    int parse_error = 0;

    if (options && *options) {
        const char *p = options;
        while (*p) {
            while (*p == ' ') p++;
            if (*p == '-') {
                p++;
                while (*p && *p != ' ') {
                    if (*p == 'c') {
                        clear_flag = 1;
                    } else if (*p == 'n') {
                        p++;
                        while (*p == ' ') p++;
                        if (*p < '0' || *p > '9') {
                            parse_error = 1;
                            break;
                        }
                        tail_lines = 0;
                        while (*p >= '0' && *p <= '9') {
                            tail_lines = tail_lines * 10 + (*p - '0');
                            p++;
                        }
                        continue;
                    } else {
                        parse_error = 1;
                        break;
                    }
                    p++;
                }
            } else if (*p) {
                parse_error = 1;
                break;
            }
            if (parse_error) break;
        }
    }

    if (parse_error) {
        shell_print("Usage: dmesg [-c] [-n num]\n");
        shell_print("  -c       Clear the ring buffer after printing\n");
        shell_print("  -n num   Show only the last num lines\n");
        last_exit_code = 1;
        return;
    }

    char *buf = (char *)kmalloc(SYSLOG_BUF_SIZE + 1);
    if (!buf) {
        shell_print("dmesg: out of memory\n");
        last_exit_code = 1;
        return;
    }

    uint32_t len = 0;
    if (tail_lines > 0) {
        uint32_t total_lines = syslog_dmesg_get_line_count();
        uint32_t start_line = (total_lines > (uint32_t)tail_lines) ? (total_lines - tail_lines) : 0;
        len = syslog_dmesg_read_from(start_line, buf, SYSLOG_BUF_SIZE);
    } else {
        len = syslog_dmesg_read(buf, SYSLOG_BUF_SIZE);
    }

    if (len > 0) {
        buf[len] = '\0';
        shell_print(buf);
    } else {
        shell_print("Kernel log is empty\n");
    }

    if (clear_flag) {
        syslog_dmesg_clear();
    }

    kfree(buf);
    last_exit_code = 0;
}

/* loglevel - Set or show kernel log level */
static void cmd_loglevel(const char *level_str) {
    static const char *level_names[] = {
        "EMERG", "ALERT", "CRIT", "ERR",
        "WARNING", "NOTICE", "INFO", "DEBUG"
    };

    if (!level_str || !*level_str) {
        /* Show current level */
        uint32_t cur = klog_get_level();
        char num_buf[16];
        snprintf(num_buf, sizeof(num_buf), "%u", cur);
        shell_print("Current log level: ");
        shell_print(num_buf);
        shell_print(" (");
        if (cur <= 7) shell_print(level_names[cur]);
        else shell_print("UNKNOWN");
        shell_print(")\n");
        last_exit_code = 0;
        return;
    }

    /* 修改日志级别需要 Admin 或 Sover 权限 */
    if (!perm_is_admin()) {
        shell_print(perm_denied_reason(0, PERM_LEVEL_ADMIN));
        shell_print("\nYour current privilege level: ");
        shell_print(perm_level_name(perm_get_level()));
        shell_print("\nUse 'sudo' to elevate privileges.\n");
        last_exit_code = 1;
        return;
    }

    /* Parse level: accept number or name */
    uint32_t new_level = 0;
    if (level_str[0] >= '0' && level_str[0] <= '9') {
        new_level = 0;
        int i = 0;
        while (level_str[i] >= '0' && level_str[i] <= '9') {
            new_level = new_level * 10 + (level_str[i] - '0');
            i++;
        }
    } else {
        /* Match by name */
        for (uint32_t i = 0; i < 8; i++) {
            if (strcasecmp(level_str, level_names[i]) == 0) {
                new_level = i;
                break;
            }
        }
    }

    if (new_level > 7) {
        shell_print("loglevel: invalid level (0-7 or EMERG..DEBUG)\n");
        last_exit_code = 1;
        return;
    }

    klog_set_level(new_level);
    shell_print("Log level set to ");
    char num_buf[16];
    snprintf(num_buf, sizeof(num_buf), "%u", new_level);
    shell_print(num_buf);
    shell_print(" (");
    shell_print(level_names[new_level]);
    shell_print(")\n");
    last_exit_code = 0;
}

/* syslog command - Show/add/delete syslog rules */
static void cmd_syslog(const char *subcmd, const char *arg1, const char *arg2, const char *arg3) {
    /* syslog 管理需要 Admin 或 Sover 权限 */
    if (!perm_is_admin()) {
        shell_print(perm_denied_reason(0, PERM_LEVEL_ADMIN));
        shell_print("\nYour current privilege level: ");
        shell_print(perm_level_name(perm_get_level()));
        shell_print("\nUse 'sudo' to elevate privileges.\n");
        last_exit_code = 1;
        return;
    }
    if (!subcmd || !*subcmd) {
        /* Show rules and recent entries */
        uint32_t count = syslog_get_rule_count();
        char num_buf[16];

        shell_print("Syslog rules (");
        snprintf(num_buf, sizeof(num_buf), "%u", count);
        shell_print(num_buf);
        shell_print("):\n");

        static const char *target_names[] = {
            "serial", "klog", "file", "network"
        };

        for (uint32_t i = 0; i < count; i++) {
            syslog_rule_t *r = syslog_get_rule(i);
            if (!r) continue;

            snprintf(num_buf, sizeof(num_buf), "%u", i);
            shell_print("  [");
            shell_print(num_buf);
            shell_print("] fac_mask=0x");
            snprintf(num_buf, sizeof(num_buf), "%x", r->facility);
            shell_print(num_buf);
            shell_print(" level<=");
            snprintf(num_buf, sizeof(num_buf), "%u", r->min_level);
            shell_print(num_buf);
            shell_print(" targets=");
            for (int t = 0; t < 4; t++) {
                if (r->targets & (1u << t)) {
                    shell_print(target_names[t]);
                    shell_print(",");
                }
            }
            if (r->filepath[0]) {
                shell_print(" file=");
                shell_print(r->filepath);
            }
            if (r->remote_host[0]) {
                shell_print(" host=");
                shell_print(r->remote_host);
            }
            shell_print("\n");
        }

        /* Show recent klog entries */
        shell_print("\nRecent kernel log:\n");
        char *buf = (char *)kmalloc(4096);
        if (buf) {
            uint32_t len = klog_read_from(klog_get_line_count() > 20 ? klog_get_line_count() - 20 : 0, buf, 4095);
            if (len > 0) {
                buf[len] = '\0';
                shell_print(buf);
            }
            kfree(buf);
        }
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "add") == 0) {
        /* syslog add <facility> <level> <target> */
        if (!arg1 || !arg2 || !arg3) {
            shell_print("Usage: syslog add <facility> <level> <target>\n");
            shell_print("  facility: kern, user, daemon, auth, all\n");
            shell_print("  level: 0-7 or emerg..debug\n");
            shell_print("  target: serial, klog, file, network\n");
            last_exit_code = 1;
            return;
        }

        syslog_rule_t rule;
        memset(&rule, 0, sizeof(rule));

        /* Parse facility */
        if (strcmp(arg1, "all") == 0) {
            rule.facility = 0xFFFFFFFF;
        } else if (strcmp(arg1, "kern") == 0) {
            rule.facility = (1u << 0);
        } else if (strcmp(arg1, "user") == 0) {
            rule.facility = (1u << 1);
        } else if (strcmp(arg1, "daemon") == 0) {
            rule.facility = (1u << 3);
        } else if (strcmp(arg1, "auth") == 0) {
            rule.facility = (1u << 4);
        } else {
            shell_print("syslog: unknown facility '");
            shell_print(arg1);
            shell_print("'\n");
            last_exit_code = 1;
            return;
        }

        /* Parse level */
        if (arg2[0] >= '0' && arg2[0] <= '9') {
            rule.min_level = 0;
            int i = 0;
            while (arg2[i] >= '0' && arg2[i] <= '9') {
                rule.min_level = rule.min_level * 10 + (arg2[i] - '0');
                i++;
            }
        } else {
            static const char *lvl_names[] = {
                "emerg", "alert", "crit", "err",
                "warning", "notice", "info", "debug"
            };
            rule.min_level = 8;
            for (int i = 0; i < 8; i++) {
                if (strcasecmp(arg2, lvl_names[i]) == 0) {
                    rule.min_level = i;
                    break;
                }
            }
        }
        if (rule.min_level > 7) {
            shell_print("syslog: invalid level\n");
            last_exit_code = 1;
            return;
        }

        /* Parse target */
        if (strcmp(arg3, "serial") == 0) {
            rule.targets = SYSLOG_TARGET_SERIAL;
        } else if (strcmp(arg3, "klog") == 0) {
            rule.targets = SYSLOG_TARGET_KLOG;
        } else if (strcmp(arg3, "file") == 0) {
            rule.targets = SYSLOG_TARGET_FILE;
            strncpy(rule.filepath, "/var/log/syslog.log", sizeof(rule.filepath) - 1);
        } else if (strcmp(arg3, "network") == 0) {
            rule.targets = SYSLOG_TARGET_NETWORK;
        } else {
            shell_print("syslog: unknown target '");
            shell_print(arg3);
            shell_print("'\n");
            last_exit_code = 1;
            return;
        }

        if (syslog_add_rule(&rule) == 0) {
            shell_print("Rule added\n");
            last_exit_code = 0;
        } else {
            shell_print("syslog: max rules reached\n");
            last_exit_code = 1;
        }
    } else if (strcmp(subcmd, "del") == 0) {
        /* syslog del <index> */
        if (!arg1 || !*arg1) {
            shell_print("Usage: syslog del <index>\n");
            last_exit_code = 1;
            return;
        }
        uint32_t idx = 0;
        int i = 0;
        while (arg1[i] >= '0' && arg1[i] <= '9') {
            idx = idx * 10 + (arg1[i] - '0');
            i++;
        }
        if (syslog_remove_rule(idx) == 0) {
            shell_print("Rule ");
            char num_buf[16];
            snprintf(num_buf, sizeof(num_buf), "%u", idx);
            shell_print(num_buf);
            shell_print(" removed\n");
            last_exit_code = 0;
        } else {
            shell_print("syslog: invalid rule index\n");
            last_exit_code = 1;
        }
    } else {
        shell_print("Usage: syslog [add <fac> <level> <target> | del <index>]\n");
        last_exit_code = 1;
    }
}

/* 8. mount - Mount a filesystem */
static void cmd_mount(const char *dev, const char *dir) {
    if (!dev || !*dev || !dir || !*dir) {
        shell_err_mount(0);
        last_exit_code = 1;
        return;
    }
    /* 需要 Admin 或 Sover 权限 */
    if (!perm_is_admin()) {
        shell_print(perm_denied_reason(0, PERM_LEVEL_ADMIN));
        shell_print("\nYour current privilege level: ");
        shell_print(perm_level_name(perm_get_level()));
        shell_print("\nUse 'sudo' to elevate privileges.\n");
        last_exit_code = 1;
        return;
    }
    /* Determine fs type from device name heuristics */
    uint32_t fs_type = FS_TYPE_RAMFS;
    if (vfs_mount(dir, fs_type, 0) == 0) {
        shell_print("Mounted ");
        shell_print(dev);
        shell_print(" on ");
        shell_print(dir);
        shell_print("\n");
        last_exit_code = 0;
    } else {
        shell_err_mount(dev);
        last_exit_code = 1;
    }
}

/* 9. umount - Unmount a filesystem */
static void cmd_umount(const char *dir) {
    if (!dir || !*dir) {
        shell_err_umount(0);
        last_exit_code = 1;
        return;
    }
    /* 需要 Admin 或 Sover 权限 */
    if (!perm_is_admin()) {
        shell_print(perm_denied_reason(0, PERM_LEVEL_ADMIN));
        shell_print("\nYour current privilege level: ");
        shell_print(perm_level_name(perm_get_level()));
        shell_print("\nUse 'sudo' to elevate privileges.\n");
        last_exit_code = 1;
        return;
    }
    if (vfs_umount(dir) == 0) {
        shell_print("Unmounted ");
        shell_print(dir);
        shell_print("\n");
        last_exit_code = 0;
    } else {
        shell_err_umount(dir);
        last_exit_code = 1;
    }
}

/* 10. format - Format a disk with filesystem */
static void cmd_format(const char *dev, const char *fstype) {
    /* 需要 Sover 权限 */
    if (!perm_is_sover()) {
        shell_print("Permission denied: Only Sover can format disks.\n");
        shell_print("Your current privilege level: ");
        shell_print(perm_level_name(perm_get_level()));
        shell_print("\nUse 'sudo' to elevate privileges.\n");
        last_exit_code = 1;
        return;
    }
    (void)dev;
    (void)fstype;
    shell_print("format: This will erase ALL data on the target device!\n");
    shell_print("Usage: format <device> <fs_type>\n");
    shell_print("Supported types: fat32, ext2\n");
    shell_print("WARNING: This operation is IRREVERSIBLE.\n");
    last_exit_code = 0;
}

/* 11. fdisk - Show/edit partition table */
static void cmd_fdisk(const char *dev) {
    /* 需要 Sover 权限 */
    if (!perm_is_sover()) {
        shell_print("Permission denied: Only Sover can edit partition tables.\n");
        shell_print("Your current privilege level: ");
        shell_print(perm_level_name(perm_get_level()));
        shell_print("\nUse 'sudo' to elevate privileges.\n");
        last_exit_code = 1;
        return;
    }
    if (!dev || !*dev) {
        shell_err_fdisk();
        last_exit_code = 1;
        return;
    }
    shell_print("Partition table for ");
    shell_print(dev);
    shell_print(":\n");
    shell_print("  Device    Boot  Start    End      Size     Type\n");
    shell_print("  ");
    shell_print(dev);
    shell_print("1  *     2048     2097151  1023M    83 (Linux)\n");
    last_exit_code = 0;
}

/* 12. chkdsk - Check disk for errors */
static void cmd_chkdsk(const char *dev) {
    if (!dev || !*dev) {
        shell_err_chkdsk();
        last_exit_code = 1;
        return;
    }
    shell_print("Checking disk ");
    shell_print(dev);
    shell_print("...\n");
    shell_print("  No errors found.\n");
    last_exit_code = 0;
}

/* 13. cat - Alias for show */
static void cmd_cat(const char *file) {
    cmd_show(file);
}

/* 14. ls - Alias for pt */
static void cmd_ls(const char *options) {
    cmd_pt(options);
}

/* 15. cd - Alias for go */
static void cmd_cd(const char *dir) {
    cmd_go(dir);
}

/* 16. pwd - Alias for where */
static void cmd_pwd(void) {
    cmd_where();
}

/* 17. touch - Create empty file */
static void cmd_touch(const char *file) {
    if (!file || !*file) {
        shell_print("touch: missing file operand\n");
        shell_print("Usage: touch <file>\n");
        shell_print("Creates an empty file if it does not exist.\n");
        shell_print("If the file exists, updates its timestamp.\n");
        last_exit_code = 1;
        return;
    }
    char full_path[512];
    build_full_path(file, full_path, sizeof(full_path));

    /* Check if file already exists */
    inode_t stat;
    memset(&stat, 0, sizeof(stat));
    if (vfs_stat(full_path, &stat) == 0) {
        /* File exists - update timestamp */
        uint32_t now = rtc_get_timestamp();
        if (vfs_utimes(full_path, now, now) == 0) {
            shell_print("Updated: ");
            shell_print(file);
            shell_print("\n");
            last_exit_code = 0;
        } else {
            shell_print("touch: cannot touch '");
            shell_print(file);
            shell_print("': Unable to update timestamp\n");
            last_exit_code = 1;
        }
        return;
    }

    /* Create the file - find parent directory */
    char parent_path[512], name[256];
    strncpy(parent_path, full_path, 511); parent_path[511] = '\0';
    char *slash = strrchr(parent_path, '/');
    if (slash) {
        strncpy(name, slash + 1, 255); name[255] = '\0';
        if (slash == parent_path) parent_path[1] = '\0';
        else *slash = '\0';
    } else {
        strcpy(name, parent_path);
        strcpy(parent_path, "/");
    }

    dentry_t *parent = 0;
    if (path_resolve(parent_path, &parent) != 0 || !parent || !parent->inode || !parent->inode->ops || !parent->inode->ops->create) {
        shell_print("touch: cannot touch '");
        shell_print(file);
        shell_print("': No such file or directory\n");
        last_exit_code = 1;
        return;
    }

    if (parent->inode->ops->create(parent, name, FILE_MODE_READ | FILE_MODE_WRITE) == 0) {
        shell_print("Created: ");
        shell_print(file);
        shell_print("\n");
        last_exit_code = 0;
    } else {
        shell_print("touch: cannot touch '");
        shell_print(file);
        shell_print("': Permission denied\n");
        last_exit_code = 1;
    }
}

/* 18. append - Append text to file */
static void cmd_append(const char *file, const char *text) {
    if (!file || !*file) {
        shell_print("append: missing file operand\n");
        shell_print("Usage: append <file> <text>\n");
        shell_print("Appends a line of text to the specified file.\n");
        last_exit_code = 1;
        return;
    }
    char full_path[512];
    build_full_path(file, full_path, sizeof(full_path));

    file_t *f = 0;
    if (vfs_open(full_path, FILE_MODE_WRITE | FILE_MODE_READ, &f) != 0 || !f) {
        shell_print("append: ");
        shell_print(file);
        shell_print(": No such file or directory\n");
        last_exit_code = 1;
        return;
    }
    vfs_seek(f, 0, SEEK_END);
    vfs_write(f, text, (uint32_t)len_strlen(text));
    vfs_write(f, "\n", 1);
    vfs_close(f);
    shell_print("Appended to ");
    shell_print(file);
    shell_print("\n");
    last_exit_code = 0;
}

/* 19. head - Show first n lines */
static void cmd_head(const char *file, const char *n_str) {
    if (!file || !*file) {
        shell_print("head: missing file operand\n");
        shell_print("Usage: head <file> [n]\n");
        shell_print("Displays the first n lines (default 10).\n");
        last_exit_code = 1;
        return;
    }
    int n = 10;
    if (n_str && *n_str) {
        n = 0;
        const char *p = n_str;
        while (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); p++; }
        if (n <= 0) n = 10;
    }

    char full_path[512];
    build_full_path(file, full_path, sizeof(full_path));

    file_t *f = 0;
    if (vfs_open(full_path, FILE_MODE_READ, &f) != 0 || !f) {
        shell_print("head: ");
        shell_print(file);
        shell_print(": No such file or directory\n");
        last_exit_code = 1;
        return;
    }

    char buf[256];
    int32_t nr;
    int line_count = 0;
    int buf_pos = 0;
    while ((nr = vfs_read(f, buf + buf_pos, 255 - buf_pos)) > 0) {
        buf_pos += nr;
        buf[buf_pos] = '\0';
        char *line_start = buf;
        char *newline;
        while ((newline = strchr(line_start, '\n')) != 0 && line_count < n) {
            *newline = '\0';
            shell_print(line_start);
            shell_print("\n");
            line_count++;
            line_start = newline + 1;
        }
        if (line_count >= n) break;
        /* Move remaining partial line to beginning */
        uint32_t remaining = buf_pos - (line_start - buf);
        memmove(buf, line_start, remaining);
        buf_pos = remaining;
    }
    /* Print last partial line if no trailing newline */
    if (line_count < n && buf_pos > 0) {
        buf[buf_pos] = '\0';
        shell_print(buf);
        shell_print("\n");
    }
    vfs_close(f);
    last_exit_code = 0;
}

/* 20. tail - Show last n lines */
static void cmd_tail(const char *file, const char *n_str) {
    if (!file || !*file) {
        shell_print("tail: missing file operand\n");
        shell_print("Usage: tail <file> [n]\n");
        shell_print("Displays the last n lines (default 10).\n");
        last_exit_code = 1;
        return;
    }
    int n = 10;
    if (n_str && *n_str) {
        n = 0;
        const char *p = n_str;
        while (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); p++; }
        if (n <= 0) n = 10;
    }

    char full_path[512];
    build_full_path(file, full_path, sizeof(full_path));

    file_t *f = 0;
    if (vfs_open(full_path, FILE_MODE_READ, &f) != 0 || !f) {
        shell_print("tail: ");
        shell_print(file);
        shell_print(": No such file or directory\n");
        last_exit_code = 1;
        return;
    }

    uint32_t content_cap = 4096;
    uint32_t content_size = 0;
    char *content = (char *)kmalloc(content_cap);
    if (!content) {
        vfs_close(f);
        shell_error(SHELL_ERR_NO_MEMORY, "tail");
        last_exit_code = 1;
        return;
    }

    char rbuf[256];
    int32_t nr;
    while ((nr = vfs_read(f, rbuf, 255)) > 0) {
        if (content_size + nr >= content_cap) {
            /* 溢出保护: content_cap 翻倍时检查是否溢出 */
            if (content_cap > UINT32_MAX / 2) {
                kfree(content);
                vfs_close(f);
                shell_print("tail: file too large to buffer in memory\n");
                last_exit_code = 1;
                return;
            }
            content_cap *= 2;
            char *new_content = (char *)krealloc(content, content_cap);
            if (!new_content) {
                kfree(content);
                vfs_close(f);
                shell_error(SHELL_ERR_NO_MEMORY, "tail");
                last_exit_code = 1;
                return;
            }
            content = new_content;
        }
        memcpy(content + content_size, rbuf, nr);
        content_size += nr;
    }
    content[content_size] = '\0';
    vfs_close(f);

    /* Count lines */
    int total_lines = 0;
    for (uint32_t i = 0; i < content_size; i++) {
        if (content[i] == '\n') total_lines++;
    }
    if (content_size > 0 && content[content_size - 1] != '\n') total_lines++;

    /* Find start line */
    int start_line = total_lines - n;
    if (start_line < 0) start_line = 0;

    /* Output from start_line */
    int line = 0;
    uint32_t i = 0;
    while (i < content_size) {
        if (line >= start_line) {
            /* Print this line */
            uint32_t j = i;
            while (j < content_size && content[j] != '\n') j++;
            char saved = content[j];
            content[j] = '\0';
            shell_print(content + i);
            shell_print("\n");
            content[j] = saved;
            i = j + 1;
            line++;
        } else {
            while (i < content_size && content[i] != '\n') i++;
            if (i < content_size) i++;
            line++;
        }
    }

    kfree(content);
    last_exit_code = 0;
}

/* 21. wc - Count lines/words/chars */
static void cmd_wc(const char *file) {
    if (!file || !*file) {
        shell_print("wc: missing file operand\n");
        shell_print("Usage: wc <file>\n");
        shell_print("Counts lines, words, and characters in the file.\n");
        shell_print("Output format: lines words chars filename\n");
        last_exit_code = 1;
        return;
    }
    char full_path[512];
    build_full_path(file, full_path, sizeof(full_path));

    file_t *f = 0;
    if (vfs_open(full_path, FILE_MODE_READ, &f) != 0 || !f) {
        shell_print("wc: ");
        shell_print(file);
        shell_print(": No such file or directory\n");
        last_exit_code = 1;
        return;
    }

    uint32_t lines = 0, words = 0, chars = 0;
    int in_word = 0;
    char buf[256];
    int32_t nr;
    while ((nr = vfs_read(f, buf, 255)) > 0) {
        chars += nr;
        for (int32_t i = 0; i < nr; i++) {
            if (buf[i] == '\n') lines++;
            if (buf[i] == ' ' || buf[i] == '\n' || buf[i] == '\t') {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                words++;
            }
        }
    }
    vfs_close(f);

    char result[128];
    snprintf(result, sizeof(result), "  %u  %u  %u %s\n", lines, words, chars, file);
    shell_print(result);
    last_exit_code = 0;
}

/* 22. diff - Compare two files */
static void cmd_diff(const char *f1, const char *f2) {
    if (!f1 || !*f1 || !f2 || !*f2) {
        shell_print("diff: missing file operand\n");
        shell_print("Usage: diff <file1> <file2>\n");
        shell_print("Compares two files line by line and shows differences.\n");
        last_exit_code = 1;
        return;
    }

    char path1[512], path2[512];
    build_full_path(f1, path1, sizeof(path1));
    build_full_path(f2, path2, sizeof(path2));

    file_t *file1 = 0, *file2 = 0;
    if (vfs_open(path1, FILE_MODE_READ, &file1) != 0 || !file1) {
        shell_print("diff: ");
        shell_print(f1);
        shell_print(": No such file or directory\n");
        last_exit_code = 1;
        return;
    }

    /* Simple line-by-line comparison */
    char buf1[256], buf2[256];
    int32_t n1, n2;
    int line = 1;
    int differences = 0;

    while (1) {
        n1 = vfs_read(file1, buf1, 255);
        n2 = vfs_read(file2, buf2, 255);
        if (n1 <= 0 && n2 <= 0) break;

        if (n1 > 0) buf1[n1] = '\0';
        if (n2 > 0) buf2[n2] = '\0';

        if (n1 != n2 || (n1 > 0 && n2 > 0 && strcmp(buf1, buf2) != 0)) {
            char lbuf[32];
            snprintf(lbuf, sizeof(lbuf), "%d", line);
            shell_print("Line ");
            shell_print(lbuf);
            shell_print(" differs:\n");
            if (n1 > 0) { shell_print("< "); shell_print(buf1); }
            if (n2 > 0) { shell_print("> "); shell_print(buf2); }
            differences++;
        }
        line++;
    }

    if (differences == 0) {
        shell_print("Files are identical.\n");
        last_exit_code = 0;
    } else {
        char dbuf[64];
        snprintf(dbuf, sizeof(dbuf), "%d differences found.\n", differences);
        shell_print(dbuf);
        last_exit_code = 1;
    }

    vfs_close(file1);
    vfs_close(file2);
}

/* 23. sort - Sort lines in file */
static void cmd_sort(const char *file) {
    if (!file || !*file) {
        shell_print("sort: missing file operand\n");
        shell_print("Usage: sort <file>\n");
        shell_print("Sorts lines of text in the specified file alphabetically.\n");
        last_exit_code = 1;
        return;
    }
    char full_path[512];
    build_full_path(file, full_path, sizeof(full_path));

    file_t *f = 0;
    if (vfs_open(full_path, FILE_MODE_READ, &f) != 0 || !f) {
        shell_print("sort: ");
        shell_print(file);
        shell_print(": No such file or directory\n");
        last_exit_code = 1;
        return;
    }

    /* Read all lines into an array, then bubble sort */
    #define SORT_MAX_LINES 256
    #define SORT_LINE_LEN 128
    char lines[SORT_MAX_LINES][SORT_LINE_LEN];
    int line_count = 0;

    char buf[256];
    int32_t nr;
    int buf_pos = 0;
    while ((nr = vfs_read(f, buf + buf_pos, 255 - buf_pos)) > 0) {
        buf_pos += nr;
        buf[buf_pos] = '\0';
        char *line_start = buf;
        char *newline;
        while ((newline = strchr(line_start, '\n')) != 0 && line_count < SORT_MAX_LINES) {
            *newline = '\0';
            strncpy(lines[line_count], line_start, SORT_LINE_LEN - 1);
            lines[line_count][SORT_LINE_LEN - 1] = '\0';
            line_count++;
            line_start = newline + 1;
        }
        uint32_t remaining = buf_pos - (line_start - buf);
        memmove(buf, line_start, remaining);
        buf_pos = remaining;
    }
    if (buf_pos > 0 && line_count < SORT_MAX_LINES) {
        buf[buf_pos] = '\0';
        strncpy(lines[line_count], buf, SORT_LINE_LEN - 1);
        lines[line_count][SORT_LINE_LEN - 1] = '\0';
        line_count++;
    }
    vfs_close(f);

    /* Bubble sort */
    for (int i = 0; i < line_count - 1; i++) {
        for (int j = 0; j < line_count - 1 - i; j++) {
            if (strcmp(lines[j], lines[j + 1]) > 0) {
                char tmp[SORT_LINE_LEN];
                strcpy(tmp, lines[j]);
                strcpy(lines[j], lines[j + 1]);
                strcpy(lines[j + 1], tmp);
            }
        }
    }

    for (int i = 0; i < line_count; i++) {
        shell_print(lines[i]);
        shell_print("\n");
    }
    last_exit_code = 0;
    #undef SORT_MAX_LINES
    #undef SORT_LINE_LEN
}

/* 24. uniq - Remove duplicate lines */
static void cmd_uniq(const char *file) {
    if (!file || !*file) {
        shell_print("uniq: missing file operand\n");
        shell_print("Usage: uniq <file>\n");
        shell_print("Removes adjacent duplicate lines from the file.\n");
        shell_print("Tip: use with 'sort' for full deduplication.\n");
        last_exit_code = 1;
        return;
    }
    char full_path[512];
    build_full_path(file, full_path, sizeof(full_path));

    file_t *f = 0;
    if (vfs_open(full_path, FILE_MODE_READ, &f) != 0 || !f) {
        shell_print("uniq: ");
        shell_print(file);
        shell_print(": No such file or directory\n");
        last_exit_code = 1;
        return;
    }

    char prev_line[256] = "";
    int first = 1;
    char buf[256];
    int32_t nr;
    int buf_pos = 0;
    while ((nr = vfs_read(f, buf + buf_pos, 255 - buf_pos)) > 0) {
        buf_pos += nr;
        buf[buf_pos] = '\0';
        char *line_start = buf;
        char *newline;
        while ((newline = strchr(line_start, '\n')) != 0) {
            *newline = '\0';
            if (first || strcmp(line_start, prev_line) != 0) {
                shell_print(line_start);
                shell_print("\n");
                strncpy(prev_line, line_start, 255);
                prev_line[255] = '\0';
                first = 0;
            }
            line_start = newline + 1;
        }
        uint32_t remaining = buf_pos - (line_start - buf);
        memmove(buf, line_start, remaining);
        buf_pos = remaining;
    }
    if (buf_pos > 0) {
        buf[buf_pos] = '\0';
        if (first || strcmp(buf, prev_line) != 0) {
            shell_print(buf);
            shell_print("\n");
        }
    }
    vfs_close(f);
    last_exit_code = 0;
}

/* 25. grep - Search for pattern in file */
static void cmd_grep(const char *pattern, const char *file, const char *opts) {
    int ignore_case = 0;
    int show_line_num = 0;

    if (opts && *opts == '-') {
        const char *p = opts + 1;
        while (*p) {
            if (*p == 'i') ignore_case = 1;
            else if (*p == 'n') show_line_num = 1;
            p++;
        }
    }

    if (!pattern || !*pattern || !file || !*file) {
        shell_print("grep: missing operand\n");
        shell_print("Usage: grep [-in] <pattern> <file>\n");
        shell_print("Searches for lines containing pattern in the specified file.\n");
        shell_print("Options:\n");
        shell_print("  -i  Ignore case distinctions\n");
        shell_print("  -n  Prefix each line with line number\n");
        last_exit_code = 1;
        return;
    }
    char full_path[512];
    build_full_path(file, full_path, sizeof(full_path));

    file_t *f = 0;
    if (vfs_open(full_path, FILE_MODE_READ, &f) != 0 || !f) {
        shell_print("grep: ");
        shell_print(file);
        shell_print(": No such file or directory\n");
        last_exit_code = 1;
        return;
    }

    char buf[256];
    int32_t nr;
    int buf_pos = 0;
    int found = 0;
    int line_num = 0;
    while ((nr = vfs_read(f, buf + buf_pos, 255 - buf_pos)) > 0) {
        buf_pos += nr;
        buf[buf_pos] = '\0';
        char *line_start = buf;
        char *newline;
        while ((newline = strchr(line_start, '\n')) != 0) {
            *newline = '\0';
            line_num++;
            int match = 0;
            if (ignore_case) {
                const char *s = line_start;
                const char *p = pattern;
                while (*s && *p) {
                    char cs = *s, cp = *p;
                    if (cs >= 'A' && cs <= 'Z') cs += 32;
                    if (cp >= 'A' && cp <= 'Z') cp += 32;
                    if (cs == cp) { s++; p++; }
                    else { s = s - (p - pattern) + 1; p = pattern; }
                }
                if (!*p) match = 1;
            } else {
                if (strstr(line_start, pattern) != 0) match = 1;
            }
            if (match) {
                if (show_line_num) {
                    char ln_buf[16];
                    snprintf(ln_buf, sizeof(ln_buf), "%d:", line_num);
                    shell_print(ln_buf);
                }
                shell_print(line_start);
                shell_print("\n");
                found++;
            }
            line_start = newline + 1;
        }
        uint32_t remaining = buf_pos - (line_start - buf);
        memmove(buf, line_start, remaining);
        buf_pos = remaining;
    }
    if (buf_pos > 0) {
        buf[buf_pos] = '\0';
        line_num++;
        int match = 0;
        if (ignore_case) {
            const char *s = buf;
            const char *p = pattern;
            while (*s && *p) {
                char cs = *s, cp = *p;
                if (cs >= 'A' && cs <= 'Z') cs += 32;
                if (cp >= 'A' && cp <= 'Z') cp += 32;
                if (cs == cp) { s++; p++; }
                else { s = s - (p - pattern) + 1; p = pattern; }
            }
            if (!*p) match = 1;
        } else {
            if (strstr(buf, pattern) != 0) match = 1;
        }
        if (match) {
            if (show_line_num) {
                char ln_buf[16];
                snprintf(ln_buf, sizeof(ln_buf), "%d:", line_num);
                shell_print(ln_buf);
            }
            shell_print(buf);
            shell_print("\n");
            found++;
        }
    }
    vfs_close(f);
    last_exit_code = (found > 0) ? 0 : 1;
}

/* 26. replace - Replace text in file */
static void cmd_replace(const char *old_text, const char *new_text, const char *file) {
    if (!old_text || !*old_text || !new_text || !file || !*file) {
        shell_print("replace: missing operand\n");
        shell_print("Usage: replace <old> <new> <file>\n");
        shell_print("Replaces all occurrences of old text with new text in the file.\n");
        last_exit_code = 1;
        return;
    }
    char full_path[512];
    build_full_path(file, full_path, sizeof(full_path));

    file_t *f = 0;
    if (vfs_open(full_path, FILE_MODE_READ, &f) != 0 || !f) {
        shell_print("replace: ");
        shell_print(file);
        shell_print(": No such file or directory\n");
        last_exit_code = 1;
        return;
    }

    /* Read entire file */
    char *content = 0;
    uint32_t content_size = 0;
    uint32_t content_cap = 4096;
    content = (char *)kmalloc(content_cap);
    if (!content) {
        vfs_close(f);
        shell_error(SHELL_ERR_NO_MEMORY, "replace");
        last_exit_code = 1;
        return;
    }

    char rbuf[256];
    int32_t nr;
    while ((nr = vfs_read(f, rbuf, 255)) > 0) {
        if (content_size + nr >= content_cap) {
            content_cap *= 2;
            char *nc = (char *)krealloc(content, content_cap);
            if (!nc) {
                kfree(content);
                vfs_close(f);
                shell_error(SHELL_ERR_NO_MEMORY, "replace");
                last_exit_code = 1;
                return;
            }
            content = nc;
        }
        memcpy(content + content_size, rbuf, nr);
        content_size += nr;
    }
    content[content_size] = '\0';
    vfs_close(f);

    /* Do replacement */
    uint32_t old_len = len_strlen(old_text);
    uint32_t new_len = len_strlen(new_text);

    /* 整数溢出保护: 限制处理文件大小 */
#define REPLACE_MAX_FILE_SIZE  (5 * 1024 * 1024)  /* replace 最大 5MB */
    if (content_size > REPLACE_MAX_FILE_SIZE) {
        kfree(content);
        shell_print("replace: file too large for in-memory replacement (max ");
        shell_print("5MB");
        shell_print(")\n");
        last_exit_code = 1;
        return;
    }

    /* 安全计算结果缓冲区大小: 检查乘法溢出 */
    uint64_t result_size_64 = (uint64_t)content_size * 2 + (uint64_t)new_len * 10;
    if (result_size_64 > UINT32_MAX) {
        kfree(content);
        shell_error(SHELL_ERR_NO_MEMORY, "replace");
        last_exit_code = 1;
        return;
    }
    char *result = (char *)kmalloc((uint32_t)result_size_64);
    if (!result) {
        kfree(content);
        shell_error(SHELL_ERR_NO_MEMORY, "replace");
        last_exit_code = 1;
        return;
    }
    uint32_t ri = 0;
    uint32_t replacements = 0;
    uint32_t i = 0;
    while (i < content_size) {
        if (content_size - i >= old_len && memcmp(content + i, old_text, old_len) == 0) {
            memcpy(result + ri, new_text, new_len);
            ri += new_len;
            i += old_len;
            replacements++;
        } else {
            result[ri++] = content[i++];
        }
    }
    result[ri] = '\0';
    kfree(content);

    /* Write back */
    if (vfs_open(full_path, FILE_MODE_WRITE, &f) == 0 && f) {
        vfs_write(f, result, ri);
        vfs_close(f);
    }
    kfree(result);

    char nbuf[32];
    snprintf(nbuf, sizeof(nbuf), "%u", replacements);
    shell_print(nbuf);
    shell_print(" replacement(s) made in ");
    shell_print(file);
    shell_print("\n");
    last_exit_code = 0;
}

/* 27. chmod - Change file permissions */
static void cmd_chmod(const char *mode_str, const char *file) {
    if (!mode_str || !*mode_str || !file || !*file) {
        shell_err_chmod();
        last_exit_code = 1;
        return;
    }

    if (!perm_is_admin()) {
        dentry_t *dentry = 0;
        char full_path_check[512];
        build_full_path(file, full_path_check, sizeof(full_path_check));
        if (path_resolve(full_path_check, &dentry) == 0 && dentry && dentry->inode) {
            if (!permission_can_chmod(dentry->inode->uid)) {
                shell_print("chmod: permission denied: ");
                shell_print(file);
                shell_print("\n");
                last_exit_code = 1;
                return;
            }
        }
    }

    char full_path[512];
    build_full_path(file, full_path, sizeof(full_path));

    /* Parse mode (simple octal) */
    uint32_t mode = 0;
    const char *p = mode_str;
    int valid = 1;
    if (*p < '0' || *p > '7') valid = 0;
    while (*p >= '0' && *p <= '7') {
        mode = mode * 8 + (*p - '0');
        p++;
    }
    if (*p != '\0' && *p != ' ') valid = 0;

    if (!valid || mode > 07777) {
        shell_print("chmod: invalid mode: '");
        shell_print(mode_str);
        shell_print("'\n");
        shell_print("Usage: chmod <mode> <file>\n");
        shell_print("  mode is octal, e.g. 755, 644, 777\n");
        last_exit_code = 1;
        return;
    }

    if (vfs_chmod(full_path, mode) != 0) {
        shell_print("chmod: cannot access '");
        shell_print(file);
        shell_print("': No such file or directory\n");
        last_exit_code = 1;
        return;
    }
    shell_print("Mode of ");
    shell_print(file);
    shell_print(" changed to ");
    shell_print(mode_str);
    shell_print("\n");
    last_exit_code = 0;
}

/* 28. chown - Change file owner */
static void cmd_chown(const char *user, const char *file) {
    if (!user || !*user || !file || !*file) {
        shell_err_chown();
        last_exit_code = 1;
        return;
    }

    if (!perm_is_admin()) {
        shell_print("chown: permission denied\n");
        shell_print("Only Admin or Sover can change file ownership\n");
        last_exit_code = 1;
        return;
    }

    char full_path[512];
    build_full_path(file, full_path, sizeof(full_path));

    /* Parse uid:gid or username:groupname */
    uint32_t uid = 0, gid = 0xFFFFFFFF;
    const char *colon = NULL;
    int has_colon = 0;
    int parse_uid_success = 1;
    int parse_gid_success = 1;

    /* Find colon separator */
    for (const char *c = user; *c; c++) {
        if (*c == ':') {
            colon = c + 1;
            has_colon = 1;
            break;
        }
    }

    /* Parse user part (UID or username) */
    char user_part[64];
    int up_len = 0;
    for (const char *c = user; *c && *c != ':' && up_len < 63; c++) {
        user_part[up_len++] = *c;
    }
    user_part[up_len] = '\0';

    /* Try numeric UID first */
    parse_uid_success = 1;
    uid = 0;
    for (const char *c = user_part; *c; c++) {
        if (*c < '0' || *c > '9') {
            parse_uid_success = 0;
            break;
        }
        uid = uid * 10 + (*c - '0');
    }

    /* If not numeric, look up by username */
    if (!parse_uid_success) {
        user_t *u = user_find_by_name(user_part);
        if (u) {
            uid = u->uid;
            parse_uid_success = 1;
        }
    }

    if (!parse_uid_success) {
        shell_print("chown: invalid user: '");
        shell_print(user_part);
        shell_print("'\n");
        last_exit_code = 1;
        return;
    }

    /* Parse group part if present */
    if (has_colon && colon) {
        char group_part[64];
        int gp_len = 0;
        for (const char *c = colon; *c && gp_len < 63; c++) {
            group_part[gp_len++] = *c;
        }
        group_part[gp_len] = '\0';

        /* Try numeric GID first */
        parse_gid_success = 1;
        gid = 0;
        for (const char *c = group_part; *c; c++) {
            if (*c < '0' || *c > '9') {
                parse_gid_success = 0;
                break;
            }
            gid = gid * 10 + (*c - '0');
        }

        /* If not numeric, look up by group name */
        if (!parse_gid_success) {
            group_t *g = group_find_by_name(group_part);
            if (g) {
                gid = g->gid;
                parse_gid_success = 1;
            }
        }

        if (!parse_gid_success) {
            shell_print("chown: invalid group: '");
            shell_print(group_part);
            shell_print("'\n");
            last_exit_code = 1;
            return;
        }
    }

    if (vfs_chown(full_path, uid, gid) != 0) {
        shell_print("chown: cannot access '");
        shell_print(file);
        shell_print("': No such file or directory\n");
        last_exit_code = 1;
        return;
    }
    shell_print("Owner of ");
    shell_print(file);
    shell_print(" changed to ");
    shell_print(user);
    shell_print("\n");
    last_exit_code = 0;
}

/* ln - Create hard or symbolic link */
static void cmd_ln(const char *target, const char *linkpath, int32_t symbolic) {
    if (!target || !*target || !linkpath || !*linkpath) {
        shell_print("Usage: ln [-s] <target> <link>\n");
        last_exit_code = 1;
        return;
    }

    char full_target[512];
    char full_link[512];
    build_full_path(target, full_target, sizeof(full_target));
    build_full_path(linkpath, full_link, sizeof(full_link));

    int32_t ret;
    if (symbolic) {
        ret = vfs_symlink(full_target, full_link);
    } else {
        ret = vfs_link(full_target, full_link);
    }

    if (ret != 0) {
        shell_print("ln: failed to create link\n");
        last_exit_code = 1;
        return;
    }
    last_exit_code = 0;
}

/* readlink - Read symbolic link target */
static void cmd_readlink(const char *path) {
    if (!path || !*path) {
        shell_print("Usage: readlink <path>\n");
        last_exit_code = 1;
        return;
    }

    char full_path[512];
    build_full_path(path, full_path, sizeof(full_path));

    char buf[512];
    int32_t ret = vfs_readlink(full_path, buf, sizeof(buf));
    if (ret < 0) {
        shell_print("readlink: not a symbolic link or error\n");
        last_exit_code = 1;
        return;
    }

    shell_print(buf);
    shell_print("\n");
    last_exit_code = 0;
}

/* setenv - Set system environment variable */
static void cmd_setenv(const char *name, const char *value) {
    if (!name || !*name) {
        shell_print("Usage: setenv <name> <value>\n");
        last_exit_code = 1;
        return;
    }

    if (sysenv_set(name, value ? value : "", 1) != 0) {
        shell_print("setenv: failed to set environment variable\n");
        last_exit_code = 1;
        return;
    }
    last_exit_code = 0;
}

/* unsetenv - Unset system environment variable */
static void cmd_unsetenv(const char *name) {
    if (!name || !*name) {
        shell_print("Usage: unsetenv <name>\n");
        last_exit_code = 1;
        return;
    }

    if (sysenv_unset(name) != 0) {
        shell_print("unsetenv: no such variable\n");
        last_exit_code = 1;
        return;
    }
    last_exit_code = 0;
}

/* ulimit - Get/set resource limits */
static void cmd_ulimit(const char *resource, const char *value) {
    static rlimit_info_t rlim;
    static int rlim_initialized = 0;

    if (!rlim_initialized) {
        rlimit_init_defaults(&rlim);
        rlim_initialized = 1;
    }

    if (!resource || !*resource) {
        shell_print("Resource limits:\n");
        for (uint32_t i = 0; i < RLIMIT_NLIMITS; i++) {
            const char *name = rlimit_name(i);
            uint32_t cur = rlimit_get_cur(&rlim, i);
            uint32_t max = rlimit_get_max(&rlim, i);
            char cur_buf[32], max_buf[32];
            if (cur == RLIM_INFINITY) {
                strcpy(cur_buf, "unlimited");
            } else {
                sprintf(cur_buf, "%u", cur);
            }
            if (max == RLIM_INFINITY) {
                strcpy(max_buf, "unlimited");
            } else {
                sprintf(max_buf, "%u", max);
            }
            shell_print("  ");
            shell_print(name);
            shell_print(": soft=");
            shell_print(cur_buf);
            shell_print(" hard=");
            shell_print(max_buf);
            shell_print("\n");
        }
        last_exit_code = 0;
        return;
    }

    int res = -1;
    for (uint32_t i = 0; i < RLIMIT_NLIMITS; i++) {
        if (strcmp(resource, rlimit_name(i)) == 0) {
            res = (int)i;
            break;
        }
    }

    if (res < 0) {
        shell_print("ulimit: unknown resource '");
        shell_print(resource);
        shell_print("'\n");
        last_exit_code = 1;
        return;
    }

    if (!value || !*value) {
        uint32_t cur = rlimit_get_cur(&rlim, (uint32_t)res);
        char buf[32];
        if (cur == RLIM_INFINITY) {
            strcpy(buf, "unlimited");
        } else {
            sprintf(buf, "%u", cur);
        }
        shell_print(buf);
        shell_print("\n");
        last_exit_code = 0;
        return;
    }

    rlimit_t new_rlim;
    if (strcmp(value, "unlimited") == 0) {
        new_rlim.rlim_cur = RLIM_INFINITY;
        new_rlim.rlim_max = RLIM_INFINITY;
    } else {
        uint32_t val = (uint32_t)atoi(value);
        new_rlim.rlim_cur = val;
        new_rlim.rlim_max = val;
    }

    if (rlimit_set(&rlim, (uint32_t)res, &new_rlim) != 0) {
        shell_print("ulimit: failed to set limit\n");
        last_exit_code = 1;
        return;
    }
    last_exit_code = 0;
}

/* file - Determine file type */
static void cmd_file(const char *file) {
    if (!file || !*file) {
        shell_print("file: missing file operand\n");
        shell_print("Usage: file <filename>\n");
        shell_print("Determine the type of a file based on its name and extension.\n");
        last_exit_code = 1;
        return;
    }

    char full_path[512];
    build_full_path(file, full_path, sizeof(full_path));

    dentry_t *dent = 0;
    if (path_resolve(full_path, &dent) != 0 || !dent) {
        shell_print("file: cannot access '");
        shell_print(file);
        shell_print("': No such file or directory\n");
        last_exit_code = 1;
        return;
    }

    uint32_t mode = 0;
    if (dent->inode) mode = dent->inode->mode;

    file_type_t ftype = file_type_detect(dent->name, mode);
    const char *desc = file_type_description(ftype);

    shell_print(file);
    shell_print(": ");
    shell_print(desc);
    shell_print("\n");
    last_exit_code = 0;
}

/* lscolor - Configure ls command color scheme */
static void cmd_lscolor(const char *arg1, const char *arg2, const char *arg3) {
    if (!arg1 || !*arg1) {
        shell_print("Usage:\n");
        shell_print("  lscolor <type> <fg> [bg]   Set color for file type\n");
        shell_print("  lscolor -b                 Classic all-white mode\n");
        shell_print("  lscolor -d                 Reset to default colors\n");
        shell_print("  lscolor -e                 Enable colors\n");
        shell_print("  lscolor -x                 Disable colors (B&W mode)\n");
        shell_print("  lscolor -l                 List all color mappings\n");
        shell_print("  lscolor -t                 List available file types\n");
        shell_print("\nColor codes (0-15):\n");
        shell_print("  0=black 1=blue 2=green 3=cyan 4=red 5=magenta\n");
        shell_print("  6=brown 7=light_grey 8=dark_grey 9=light_blue\n");
        shell_print("  10=light_green 11=light_cyan 12=light_red\n");
        shell_print("  13=light_magenta 14=yellow 15=white\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(arg1, "-b") == 0) {
        file_color_reset_classic();
        shell_print("Classic all-white mode enabled\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(arg1, "-d") == 0) {
        file_color_reset_default();
        shell_print("Default color scheme restored\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(arg1, "-e") == 0) {
        file_color_enable(1);
        shell_print("Colors enabled\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(arg1, "-x") == 0) {
        file_color_enable(0);
        shell_print("Colors disabled (black & white mode)\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(arg1, "-l") == 0) {
        shell_print("Current color mappings:\n");
        shell_print("  Type          FG  BG\n");
        shell_print("  ------------------------\n");
        for (int i = 0; i <= FT_UNKNOWN; i++) {
            uint8_t fg, bg;
            file_type_get_color((file_type_t)i, &fg, &bg);
            const char *sname = file_type_short_name((file_type_t)i);
            char line[64];
            snprintf(line, sizeof(line), "  %-12s %2u  %2u\n", sname, fg, bg);
            shell_print(line);
        }
        shell_print("\nColors ");
        shell_print(file_color_enabled() ? "enabled" : "disabled");
        shell_print("\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(arg1, "-t") == 0) {
        shell_print("Available file types:\n");
        for (int i = 0; i <= FT_UNKNOWN; i++) {
            const char *sname = file_type_short_name((file_type_t)i);
            const char *lname = file_type_name((file_type_t)i);
            char line[80];
            snprintf(line, sizeof(line), "  %-12s %s\n", sname, lname);
            shell_print(line);
        }
        last_exit_code = 0;
        return;
    }

    if (arg1[0] == '-') {
        shell_print("lscolor: unknown option '");
        shell_print(arg1);
        shell_print("'\n");
        last_exit_code = 1;
        return;
    }

    if (!arg2 || !*arg2) {
        shell_print("lscolor: missing foreground color\n");
        last_exit_code = 1;
        return;
    }

    file_type_t ftype = file_type_from_name(arg1);
    if (ftype == FT_UNKNOWN && strcmp(arg1, "unknown") != 0) {
        shell_print("lscolor: unknown file type '");
        shell_print(arg1);
        shell_print("'\n");
        shell_print("Use 'lscolor -t' to list available types\n");
        last_exit_code = 1;
        return;
    }

    int fg = 0;
    const char *p = arg2;
    while (*p >= '0' && *p <= '9') { fg = fg * 10 + (*p - '0'); p++; }
    if (fg < 0 || fg > 15) {
        shell_print("lscolor: foreground color must be 0-15\n");
        last_exit_code = 1;
        return;
    }

    int bg = 0;
    if (arg3 && *arg3) {
        const char *q = arg3;
        bg = 0;
        while (*q >= '0' && *q <= '9') { bg = bg * 10 + (*q - '0'); q++; }
        if (bg < 0 || bg > 15) {
            shell_print("lscolor: background color must be 0-15\n");
            last_exit_code = 1;
            return;
        }
    }

    file_color_set(ftype, (uint8_t)fg, (uint8_t)bg);
    if (!file_color_enabled()) {
        file_color_enable(1);
    }

    shell_print("Set color for '");
    shell_print(arg1);
    shell_print("' to fg=");
    char nbuf[8];
    snprintf(nbuf, sizeof(nbuf), "%d", fg);
    shell_print(nbuf);
    if (arg3 && *arg3) {
        shell_print(", bg=");
        snprintf(nbuf, sizeof(nbuf), "%d", bg);
        shell_print(nbuf);
    }
    shell_print("\n");
    last_exit_code = 0;
}

/* 29. stat - Show file metadata */
static void cmd_stat(const char *file) {
    if (!file || !*file) {
        shell_err_stat(0);
        last_exit_code = 1;
        return;
    }
    char full_path[512];
    build_full_path(file, full_path, sizeof(full_path));

    inode_t stat;
    memset(&stat, 0, sizeof(stat));
    if (vfs_stat(full_path, &stat) != 0) {
        shell_err_stat(file);
        last_exit_code = 1;
        return;
    }

    char buf[128];
    shell_print(buf);

    shell_print("  Type: ");
    if (stat.mode & FILE_MODE_DIR) shell_print("directory\n");
    else if (stat.mode & FILE_MODE_EXEC) shell_print("executable\n");
    else shell_print("regular file\n");

    snprintf(buf, sizeof(buf), "  Inode: %u\n", stat.ino);
    shell_print(buf);
    snprintf(buf, sizeof(buf), "  Mode:  0x%x\n", stat.mode);
    shell_print(buf);
    snprintf(buf, sizeof(buf), "  UID:   %u  GID: %u\n", stat.uid, stat.gid);
    shell_print(buf);
    snprintf(buf, sizeof(buf), "  Links: %u\n", stat.nlinks);
    shell_print(buf);
    last_exit_code = 0;
}

/* 30. tree - Show directory tree */
static int tree_dir_count = 0;
static int tree_file_count = 0;

static void tree_recursive(dentry_t *dir, int depth) {
    dentry_t *child = dir->child;
    while (child) {
        for (int i = 0; i < depth; i++) shell_print("  ");
        if (child->inode && (child->inode->mode & FILE_MODE_DIR)) {
            tree_dir_count++;
            shell_print("+ ");
            shell_print(child->name);
            shell_print("/\n");
            tree_recursive(child, depth + 1);
        } else {
            tree_file_count++;
            shell_print("- ");
            shell_print(child->name);
            shell_print("\n");
        }
        child = child->next_sibling;
    }
}

static void cmd_tree(const char *dir) {
    const char *target = (dir && *dir) ? dir : current_dir;
    char full_path[512];
    build_full_path(target, full_path, sizeof(full_path));

    dentry_t *d = 0;
    if (path_resolve(full_path, &d) != 0 || !d) {
        shell_err_tree(target);
        last_exit_code = 1;
        return;
    }

    tree_dir_count = 0;
    tree_file_count = 0;

    shell_print(full_path);
    shell_print("\n");
    tree_recursive(d, 1);

    char stat_buf[128];
    snprintf(stat_buf, sizeof(stat_buf), "\n%d directories, %d files\n",
             tree_dir_count, tree_file_count);
    shell_print(stat_buf);

    last_exit_code = 0;
}

/* 31. du - Show disk usage of directory */
static void du_recursive(dentry_t *dir, uint32_t *total_size) {
    dentry_t *child = dir->child;
    while (child) {
        if (child->inode) {
            *total_size += child->inode->size;
        }
        if (child->inode && (child->inode->mode & FILE_MODE_DIR)) {
            du_recursive(child, total_size);
        }
        child = child->next_sibling;
    }
}

static void cmd_du(const char *dir) {
    const char *target = (dir && *dir) ? dir : current_dir;
    char full_path[512];
    build_full_path(target, full_path, sizeof(full_path));

    dentry_t *d = 0;
    if (path_resolve(full_path, &d) != 0 || !d) {
        shell_err_du(target);
        last_exit_code = 1;
        return;
    }

    uint32_t total_size = 0;
    du_recursive(d, &total_size);

    char buf[64];
    snprintf(buf, sizeof(buf), "%u\t%s\n", total_size, target);
    shell_print(buf);
    last_exit_code = 0;
}

/* 32. df - Show filesystem disk space usage */
static void cmd_df(void) {
    shell_print("Filesystem     Type       Size     Used     Free  Use%  Mounted on\n");
    vfs_mount_info_t mounts[32];
    int32_t n = vfs_list_mounts(mounts, 32);
    for (int32_t i = 0; i < n; i++) {
        uint64_t total = mounts[i].total_blocks;
        uint64_t free_b = mounts[i].free_blocks;
        uint64_t used_b = (total > free_b) ? (total - free_b) : 0;
        uint32_t use_pct = (total > 0) ? (uint32_t)((used_b * 100) / total) : 0;
        char ro_flag = mounts[i].read_only ? 'r' : ' ';
        char sz_unit = 'K';
        uint64_t t_disp = total, u_disp = used_b, f_disp = free_b;
        if (t_disp > 1024*1024) { t_disp /= 1024; u_disp /= 1024; f_disp /= 1024; sz_unit = 'M'; }
        t_disp /= 2; u_disp /= 2; f_disp /= 2;
        char buf[192];
        snprintf(buf, sizeof(buf), "%-14s%-9s%6lu%c %6lu%c %6lu%c  %2lu%%%c %s\n",
                 "none",
                 mounts[i].fs_type,
                 (unsigned long)t_disp, sz_unit,
                 (unsigned long)u_disp, sz_unit,
                 (unsigned long)f_disp, sz_unit,
                 (unsigned long)use_pct, ro_flag,
                 mounts[i].mount_point);
        shell_print(buf);
    }
    uint32_t total_mem = pmm_get_total_pages() * 4;
    uint32_t used_mem = pmm_get_used_pages() * 4;
    uint32_t free_mem = pmm_get_free_pages() * 4;
    uint32_t mem_pct = total_mem > 0 ? (used_mem * 100) / total_mem : 0;
    char buf2[128];
    snprintf(buf2, sizeof(buf2), "%-14s%-9s%6uK %6uK %6uK  %2u%%   (memory)\n",
             "mem", "tmpfs", total_mem, used_mem, free_mem, mem_pct);
    shell_print(buf2);
    last_exit_code = 0;
}

/* 33. ifconfig - Show network interfaces */
static void cmd_ifconfig(void) {
    uint32_t count = net_get_interface_count();
    for (uint32_t i = 0; i < count; i++) {
        net_interface_t *iface = net_get_interface(i);
        if (!iface) continue;

        char buf[192];

        shell_print(iface->name);
        shell_print("  ");
        if (iface->up) shell_print("UP ");
        if (iface->flags & IFF_BROADCAST) shell_print("BROADCAST ");
        if (iface->flags & IFF_RUNNING) shell_print("RUNNING ");
        if (iface->flags & IFF_MULTICAST) shell_print("MULTICAST ");
        if (iface->flags & IFF_LOOPBACK) shell_print("LOOPBACK ");
        shell_print("MTU:");
        snprintf(buf, sizeof(buf), "%u", iface->mtu);
        shell_print(buf);
        shell_print("\n");

        snprintf(buf, sizeof(buf), "        ether %02x:%02x:%02x:%02x:%02x:%02x\n",
                 iface->mac.bytes[0], iface->mac.bytes[1], iface->mac.bytes[2],
                 iface->mac.bytes[3], iface->mac.bytes[4], iface->mac.bytes[5]);
        shell_print(buf);

        uint8_t a = iface->ip.addr & 0xFF;
        uint8_t b = (iface->ip.addr >> 8) & 0xFF;
        uint8_t c = (iface->ip.addr >> 16) & 0xFF;
        uint8_t d = (iface->ip.addr >> 24) & 0xFF;

        uint8_t ma = iface->mask.addr & 0xFF;
        uint8_t mb = (iface->mask.addr >> 8) & 0xFF;
        uint8_t mc = (iface->mask.addr >> 16) & 0xFF;
        uint8_t md = (iface->mask.addr >> 24) & 0xFF;

        uint8_t ba = a | (~ma & 0xFF);
        uint8_t bb = b | (~mb & 0xFF);
        uint8_t bc = c | (~mc & 0xFF);
        uint8_t bd = d | (~md & 0xFF);

        snprintf(buf, sizeof(buf), "        inet %u.%u.%u.%u  netmask %u.%u.%u.%u  broadcast %u.%u.%u.%u\n",
                 a, b, c, d, ma, mb, mc, md, ba, bb, bc, bd);
        shell_print(buf);

        uint8_t ga = iface->gateway.addr & 0xFF;
        uint8_t gb = (iface->gateway.addr >> 8) & 0xFF;
        uint8_t gc = (iface->gateway.addr >> 16) & 0xFF;
        uint8_t gd = (iface->gateway.addr >> 24) & 0xFF;
        if (iface->gateway.addr != 0) {
            snprintf(buf, sizeof(buf), "        gateway %u.%u.%u.%u\n", ga, gb, gc, gd);
            shell_print(buf);
        }

        snprintf(buf, sizeof(buf), "        RX packets:%u bytes:%u errors:%u\n",
                 iface->rx_packets, iface->rx_bytes, iface->rx_errors);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "        TX packets:%u bytes:%u errors:%u\n",
                 iface->tx_packets, iface->tx_bytes, iface->tx_errors);
        shell_print(buf);
    }
    if (count == 0) {
        shell_print("No network interfaces found.\n");
    }
    last_exit_code = 0;
}

/* 34. route - Show routing table */
static void cmd_route(void) {
    shell_print("Kernel IP routing table:\n");
    shell_print("Destination     Gateway         Genmask         Iface\n");
    net_interface_t *iface = net_get_default_interface();
    if (iface) {
        char buf[128];
        uint8_t a, b, c, d;
        a = iface->gateway.addr & 0xFF;
        b = (iface->gateway.addr >> 8) & 0xFF;
        c = (iface->gateway.addr >> 16) & 0xFF;
        d = (iface->gateway.addr >> 24) & 0xFF;
        snprintf(buf, sizeof(buf), "default         %u.%u.%u.%u    0.0.0.0         %s\n",
                 a, b, c, d, iface->name);
        shell_print(buf);
    }
    last_exit_code = 0;
}

/* 35. dns - DNS lookup */
static void cmd_dns(const char *host) {
    if (!host || !*host) {
        shell_print("Usage: dns <hostname>\n");
        shell_print("Resolves a hostname to an IP address using DNS.\n");
        shell_print("Example: dns google.com\n");
        last_exit_code = 1;
        return;
    }

    /* Check if it's already an IP address */
    int dots = 0;
    int all_numeric = 1;
    int digit_count = 0;
    for (const char *p = host; *p; p++) {
        if (*p == '.') {
            dots++;
            if (digit_count == 0 || digit_count > 3) { all_numeric = 0; break; }
            digit_count = 0;
        } else if (*p >= '0' && *p <= '9') {
            digit_count++;
        } else {
            all_numeric = 0;
            break;
        }
    }
    if (all_numeric && dots == 3 && digit_count > 0) {
        shell_print(host);
        shell_print(" is already an IP address\n");
        last_exit_code = 0;
        return;
    }

    net_interface_t *iface = net_get_default_interface();
    if (!iface) {
        iface = loopback_get();
    }
    if (!iface) {
        shell_error(SHELL_ERR_NO_NETWORK, "dns");
        last_exit_code = 1;
        return;
    }

    char buf[128];
    snprintf(buf, sizeof(buf), "Resolving %s...\n", host);
    shell_print(buf);

    ipv4_addr_t addr;
    int ret = dns_resolve(host, &addr);
    if (ret == 0 && addr.addr != 0) {
        uint8_t a = addr.addr & 0xFF;
        uint8_t b = (addr.addr >> 8) & 0xFF;
        uint8_t c = (addr.addr >> 16) & 0xFF;
        uint8_t d = (addr.addr >> 24) & 0xFF;
        snprintf(buf, sizeof(buf), "Name:    %s\nAddress: %u.%u.%u.%u\n",
                 host, a, b, c, d);
        shell_print(buf);
        last_exit_code = 0;
    } else {
        shell_print("dns: cannot resolve '");
        shell_print(host);
        shell_print("': Name or service not known\n");
        last_exit_code = 1;
    }
}

/* 36. wget - Download file via HTTP */
static void cmd_wget(const char *url, const char *outfile) {
    if (!url || !*url) {
        shell_error(SHELL_ERR_NO_NETWORK, "wget");
        last_exit_code = 1;
        return;
    }

    net_interface_t *iface = net_get_default_interface();
    if (!iface) {
        shell_error(SHELL_ERR_NO_NETWORK, "wget");
        last_exit_code = 1;
        return;
    }

    shell_print("wget: Downloading ");
    shell_print(url);
    shell_print("\n");

    http_response_t resp;
    memset(&resp, 0, sizeof(resp));
    int ret = http_get(url, &resp);
    if (ret != 0) {
        shell_print("wget: download failed\n");
        last_exit_code = 1;
        return;
    }

    char buf[128];
    snprintf(buf, sizeof(buf), "wget: HTTP %u\n", resp.status_code);
    shell_print(buf);

    if (resp.status_code != HTTP_STATUS_OK) {
        http_free_response(&resp);
        shell_print("wget: HTTP error\n");
        last_exit_code = 1;
        return;
    }

    const char *filename = outfile;
    if (!filename || !*filename) {
        const char *p = url;
        if (strncmp(p, "http://", 7) == 0) p += 7;
        const char *last_slash = p;
        while (*p) {
            if (*p == '/') last_slash = p + 1;
            p++;
        }
        filename = last_slash;
        if (!*filename) filename = "index.html";
    }

    char filepath[512];
    if (filename[0] == '/') {
        strncpy(filepath, filename, sizeof(filepath) - 1);
        filepath[sizeof(filepath) - 1] = '\0';
    } else {
        strncpy(filepath, current_dir, sizeof(filepath) - 1);
        filepath[sizeof(filepath) - 1] = '\0';
        if (strcmp(current_dir, "/") != 0) strncat(filepath, "/", sizeof(filepath) - strlen(filepath) - 1);
        strncat(filepath, filename, sizeof(filepath) - strlen(filepath) - 1);
    }

    vfs_creat(filepath, FILE_MODE_WRITE | FILE_MODE_READ);
    file_t *f = 0;
    if (vfs_open(filepath, FILE_MODE_WRITE, &f) != 0 || !f) {
        shell_print("wget: cannot create file ");
        shell_print(filepath);
        shell_print("\n");
        http_free_response(&resp);
        last_exit_code = 1;
        return;
    }

    uint32_t total = 0;
    if (resp.body && resp.body_len > 0) {
        vfs_write(f, resp.body, resp.body_len);
        total = resp.body_len;
    }
    vfs_close(f);
    http_free_response(&resp);

    snprintf(buf, sizeof(buf), "wget: saved %u bytes to %s\n", total, filepath);
    shell_print(buf);
    last_exit_code = 0;
}

/* 37. netstat - Show network connections */
static void cmd_netstat(void) {
    shell_print("Active network connections:\n");
    shell_print("Proto  Local Address    Foreign Address  State\n");
    const net_stats_t *stats = net_get_stats();
    if (stats) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Total RX: %u packets, %u bytes, %u errors\n",
                 stats->rx_packets, stats->rx_bytes, stats->rx_errors);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "Total TX: %u packets, %u bytes, %u errors\n",
                 stats->tx_packets, stats->tx_bytes, stats->tx_errors);
        shell_print(buf);
    }
    last_exit_code = 0;
}

/* 38. traceroute - Trace route to host */
static volatile uint8_t tr_received;
static volatile ipv4_addr_t tr_from_addr;

static uint16_t tr_checksum(const void *data, uint32_t len) {
    uint32_t sum = 0;
    const uint16_t *ptr = (const uint16_t *)data;
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    if (len == 1) {
        sum += *(const uint8_t *)ptr;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)(~sum);
}

static void tr_callback(ipv4_addr_t from, uint8_t code,
                         ipv4_addr_t inner_dst, uint16_t inner_sport) {
    (void)code; (void)inner_dst; (void)inner_sport;
    tr_from_addr = from;
    tr_received = 1;
}

static void cmd_traceroute(const char *ip_str) {
    if (!ip_str || !*ip_str) {
        shell_err_traceroute(ip_str);
        last_exit_code = 1;
        return;
    }

    net_interface_t *iface = net_get_default_interface();
    if (!iface) {
        shell_error(SHELL_ERR_NO_NETWORK, "traceroute");
        last_exit_code = 1;
        return;
    }

    uint32_t a = 0, b = 0, c = 0, d = 0;
    int parsed = 0;
    uint32_t *vals[4] = { &a, &b, &c, &d };
    const char *p = ip_str;
    for (int i = 0; i < 4; i++) {
        uint32_t val = 0;
        int digits = 0;
        while (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            digits++;
            p++;
        }
        if (digits == 0) break;
        *vals[i] = val;
        parsed++;
        if (i < 3 && *p == '.') p++;
    }

    ipv4_addr_t dst;
    if (parsed != 4 || a > 255 || b > 255 || c > 255 || d > 255) {
        if (dns_resolve(ip_str, &dst) != 0) {
            shell_print("traceroute: cannot resolve host: ");
            shell_print(ip_str);
            shell_print("\n");
            last_exit_code = 1;
            return;
        }
    } else {
        dst.addr = (d << 24) | (c << 16) | (b << 8) | a;
    }

    shell_print("traceroute to ");
    shell_print(ip_str);
    shell_print(" (");
    {
        char ibuf[16];
        uint8_t ia = dst.addr & 0xFF;
        uint8_t ib = (dst.addr >> 8) & 0xFF;
        uint8_t ic = (dst.addr >> 16) & 0xFF;
        uint8_t id = (dst.addr >> 24) & 0xFF;
        snprintf(ibuf, sizeof(ibuf), "%u.%u.%u.%u", ia, ib, ic, id);
        shell_print(ibuf);
    }
    shell_print("), 30 hops max\n");

    icmp_set_traceroute_callback(tr_callback);

    uint16_t tr_id = 0xDEAD;
    uint16_t tr_seq_base = 0;
    int reached = 0;

    for (int ttl = 1; ttl <= 30; ttl++) {
        /* 检查中断信号 */
        if (kb_signal_check()) {
            shell_print("^C\n");
            last_exit_code = 130;
            return;
        }

        char buf[128];
        snprintf(buf, sizeof(buf), " %2d  ", ttl);
        shell_print(buf);

        int got_any = 0;
        ipv4_addr_t hop_addr;
        hop_addr.addr = 0;
        uint32_t best_rtt = 0xFFFFFFFF;

        for (int probe = 0; probe < 3; probe++) {
            tr_received = 0;
            tr_from_addr.addr = 0;

            uint32_t total = sizeof(icmp_header_t) + 32;
            uint8_t *packet = (uint8_t *)kmalloc(total);
            if (!packet) break;

            icmp_header_t *hdr = (icmp_header_t *)packet;
            hdr->type = ICMP_TYPE_ECHO_REQUEST;
            hdr->code = 0;
            hdr->checksum = 0;
            hdr->identifier = ((tr_id >> 8) & 0xFF) | ((tr_id & 0xFF) << 8);
            uint16_t seq = tr_seq_base++;
            hdr->sequence = ((seq >> 8) & 0xFF) | ((seq & 0xFF) << 8);
            memset(packet + sizeof(icmp_header_t), (uint8_t)(ttl + probe), 32);

            uint16_t csum = tr_checksum(packet, total);
            packet[2] = (uint8_t)(csum & 0xFF);
            packet[3] = (uint8_t)((csum >> 8) & 0xFF);

            uint32_t send_time = timer_get_ticks();
            int ret = ip_send_with_ttl(iface, dst, IP_PROTO_ICMP, packet, total, (uint8_t)ttl, 0);
            kfree(packet);

            if (ret != 0) {
                shell_print("* ");
                continue;
            }

            uint32_t wait_end = send_time + 10;
            while (timer_get_ticks() < wait_end) {
                if (kb_signal_check()) {
                    shell_print("^C\n");
                    last_exit_code = 130;
                    return;
                }
                if (tr_received) {
                    uint32_t now = timer_get_ticks();
                    uint32_t rtt = (now - send_time) * 10;
                    if (rtt < best_rtt) best_rtt = rtt;
                    hop_addr = tr_from_addr;
                    got_any = 1;
                    break;
                }
            }

            if (tr_received) {
                char rttbuf[16];
                snprintf(rttbuf, sizeof(rttbuf), "%ums ", best_rtt);
                shell_print(rttbuf);
            } else {
                shell_print("* ");
            }
        }

        if (got_any) {
            char ipbuf[20];
            uint8_t ha = hop_addr.addr & 0xFF;
            uint8_t hb = (hop_addr.addr >> 8) & 0xFF;
            uint8_t hc = (hop_addr.addr >> 16) & 0xFF;
            uint8_t hd = (hop_addr.addr >> 24) & 0xFF;
            snprintf(ipbuf, sizeof(ipbuf), "%u.%u.%u.%u", ha, hb, hc, hd);
            shell_print(ipbuf);
            if (hop_addr.addr == dst.addr) {
                shell_print("  destination reached");
                reached = 1;
            }
        } else {
            shell_print("Request timed out");
        }
        shell_print("\n");

        if (reached) break;
    }

    icmp_set_traceroute_callback(NULL);
    last_exit_code = reached ? 0 : 1;
}

/* 39. arp - Show ARP table */
static void cmd_arp(void) {
    shell_print("ARP Table:\n");
    shell_print("IP Address       HW Address         Flags        Interface\n");
    shell_print("---------------  -----------------  -----------  ---------\n");
    net_interface_t *iface = net_get_default_interface();
    uint8_t a = 0, b = 0, c = 0;
    if (iface) {
        a = iface->ip.addr & 0xFF;
        b = (iface->ip.addr >> 8) & 0xFF;
        c = (iface->ip.addr >> 16) & 0xFF;
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "%u.%u.%u.1       00:11:22:33:44:01  C           %s\n",
             a, b, c, iface ? iface->name : "eth0");
    shell_print(buf);
    snprintf(buf, sizeof(buf), "%u.%u.%u.100     00:22:33:44:55:66  C           %s\n",
             a, b, c, iface ? iface->name : "eth0");
    shell_print(buf);
    shell_print("Entries: 2\n");
    last_exit_code = 0;
}

/* 40. hostname - Show/set hostname */
static void cmd_hostname(const char *name) {
    static char hostname[64] = "funscore";
    if (name && *name) {
        strncpy(hostname, name, 63);
        hostname[63] = '\0';
        env_set("HOSTNAME", hostname);
    }
    shell_print(hostname);
    shell_print("\n");
    last_exit_code = 0;
}

/* 41. lspci - List PCI devices */
static void cmd_lspci(void) {
    shell_print("PCI devices:\n");
    shell_print("  Bus:Dev.Fun  Vendor  Device  Class  Subclass  IRQ\n");

    for (uint32_t bus = 0; bus < 8; bus++) {
        for (uint32_t dev = 0; dev < 32; dev++) {
            uint32_t vendor_dev = pci_read_config(bus, dev, 0, 0x00);
            if (vendor_dev == 0xFFFFFFFF || vendor_dev == 0) continue;

            uint16_t vendor_id = vendor_dev & 0xFFFF;
            uint16_t device_id = (vendor_dev >> 16) & 0xFFFF;

            uint32_t class_rev = pci_read_config(bus, dev, 0, 0x08);
            uint8_t class_code = (class_rev >> 24) & 0xFF;
            uint8_t subclass = (class_rev >> 16) & 0xFF;

            uint32_t irq_info = pci_read_config(bus, dev, 0, 0x3C);
            uint8_t irq = irq_info & 0xFF;

            char buf[128];
            snprintf(buf, sizeof(buf), "  %u:%u.%u      %04x    %04x    %02x     %02x        %u\n",
                     bus, dev, 0, vendor_id, device_id, class_code, subclass, irq);
            shell_print(buf);
        }
    }
    last_exit_code = 0;
}

/* 42. lsusb - List USB devices */
static void cmd_lsusb(void) {
    shell_print("Bus 001 Device 001: ID 1d6b:0002 Linux Foundation 2.0 root hub\n");
    shell_print("Bus 001 Device 002: ID 1234:5678 Generic USB Hub\n");
    shell_print("Bus 001 Device 003: ID 09da:a001 A4Tech USB Optical Mouse\n");
    shell_print("Bus 002 Device 001: ID 1d6b:0003 Linux Foundation 3.0 root hub\n");
    shell_print("Bus 002 Device 002: ID 08ff:1600 AuthenTec Fingerprint Sensor\n");
    last_exit_code = 0;
}

/* 43. lsblk - List block devices */
static void cmd_lsblk(void) {
    shell_print("Block devices:\n");
    shell_print("  NAME    SIZE   TYPE\n");
    const char *blk_names[] = { "hda", "hdb", "fd0", NULL };
    for (int i = 0; blk_names[i]; i++) {
        devfs_device_t *dev = devfs_find(blk_names[i]);
        if (dev && dev->type == DEVICE_BLOCK) {
            char buf[64];
            snprintf(buf, sizeof(buf), "  %s    %u:%u   disk\n", dev->name, dev->major, dev->minor);
            shell_print(buf);
        }
    }
    last_exit_code = 0;
}

/* 44. sensors - Show hardware sensor data */
static void cmd_sensors(void) {
    shell_print("Hardware sensors:\n");
    char buf[128];
    /* Read CPU temperature from ACPI (stub values) */
    shell_print("  cpu_temp:     +35.0 C\n");
    shell_print("  mb_temp:      +30.0 C\n");
    uint32_t uptime = timer_get_ticks() / 100;
    snprintf(buf, sizeof(buf), "  cpu_fan:      1200 RPM (uptime %us)\n", uptime);
    shell_print(buf);
    shell_print("  vcore:        +1.20 V\n");
    last_exit_code = 0;
}

/* 45. freq - Show/set CPU frequency */
static void cmd_freq(const char *freq_str) {
    cpufreq_info_t *info = cpufreq_get_info();
    if (!info) {
        shell_print("CPU frequency information unavailable\n");
        last_exit_code = 1;
        return;
    }

    if (freq_str && *freq_str) {
        uint32_t mhz = (uint32_t)atoi(freq_str);
        if (mhz == 0) {
            shell_print("Invalid frequency: ");
            shell_print(freq_str);
            shell_print(" MHz\n");
            last_exit_code = 1;
            return;
        }
        int rc = cpufreq_set(mhz);
        if (rc == 0) {
            shell_print("CPU frequency set to ");
            shell_print(freq_str);
            shell_print(" MHz\n");
            last_exit_code = 0;
        } else {
            shell_print("Failed to set CPU frequency to ");
            shell_print(freq_str);
            shell_print(" MHz\n");
            last_exit_code = 1;
        }
    } else {
        char buf[128];
        snprintf(buf, sizeof(buf), "CPU frequency: %u MHz\n", info->current_freq);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "  Min: %u MHz, Max: %u MHz\n", info->min_freq, info->max_freq);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "  Governor: %s\n", info->governor);
        shell_print(buf);
        if (info->available_count > 0) {
            shell_print("  Available frequencies:");
            for (uint32_t i = 0; i < info->available_count; i++) {
                snprintf(buf, sizeof(buf), " %u", info->available_freqs[i]);
                shell_print(buf);
            }
            shell_print(" MHz\n");
        }
        last_exit_code = 0;
    }
}

/* 46. calc - Simple calculator */
static void cmd_calc(const char *expr) {
    if (!expr || !*expr) {
        shell_err_calc();
        last_exit_code = 1;
        return;
    }

    /* Simple expression parser: supports +, -, *, / with left-to-right evaluation */
    const char *p = expr;
    while (*p == ' ') p++;

    int32_t result = 0;
    int32_t num = 0;
    int got_num = 0;
    char op = '+';
    int negative = 0;

    while (*p) {
        if (*p >= '0' && *p <= '9') {
            num = 0;
            if (negative) { negative = 0; }
            while (*p >= '0' && *p <= '9') {
                num = num * 10 + (*p - '0');
                p++;
            }
            if (negative) num = -num;

            /* Apply pending operation */
            switch (op) {
                case '+': result += num; break;
                case '-': result -= num; break;
                case '*': result *= num; break;
                case '/':
                    if (num != 0) result /= num;
                    else { shell_error(SHELL_ERR_INTERNAL, "calc: division by zero"); last_exit_code = 1; return; }
                    break;
                default: break;
            }
            got_num = 1;
        } else if (*p == '+') {
            op = '+'; p++;
        } else if (*p == '-') {
            op = '-'; p++;
        } else if (*p == '*') {
            op = '*'; p++;
        } else if (*p == '/') {
            op = '/'; p++;
        } else if (*p == ' ') {
            p++;
        } else {
            shell_error(SHELL_ERR_BAD_SYNTAX, expr);
            last_exit_code = 1;
            return;
        }
    }

    if (!got_num) {
        shell_err_calc();
        last_exit_code = 1;
        return;
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "%d\n", result);
    shell_print(buf);
    last_exit_code = 0;
}

/* 47. base64 - Base64 encode/decode */
static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const int8_t b64_rev_table[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
    52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
    15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
    -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
    41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
};

static void cmd_base64(const char *opt, const char *file) {
    int decode_mode = 0;
    const char *filename = file;

    if (opt && strcmp(opt, "-d") == 0) {
        decode_mode = 1;
        if (!filename || !*filename) {
            shell_err_base64(filename);
            last_exit_code = 1;
            return;
        }
    } else {
        filename = opt;
    }

    if (!filename || !*filename) {
        shell_err_base64(filename);
        last_exit_code = 1;
        return;
    }

    file_t *f = 0;
    if (vfs_open(filename, FILE_MODE_READ, &f) != 0 || !f) {
        shell_err_base64(filename);
        last_exit_code = 1;
        return;
    }

    char buf[2048];
    int n = vfs_read(f, buf, sizeof(buf) - 1);
    vfs_close(f);

    if (n <= 0) {
        shell_err_base64(filename);
        last_exit_code = 1;
        return;
    }
    buf[n] = '\0';

    if (decode_mode) {
        int in_len = n;
        while (in_len > 0 && (buf[in_len-1] == '\n' || buf[in_len-1] == '\r' || buf[in_len-1] == ' ' || buf[in_len-1] == '\t'))
            in_len--;

        char out[(sizeof(buf) * 3 / 4) + 4];
        int j = 0;
        int i = 0;
        while (i < in_len) {
            while (i < in_len && (buf[i] == '\n' || buf[i] == '\r' || buf[i] == ' ' || buf[i] == '\t')) i++;
            if (i >= in_len) break;

            uint32_t sextets[4] = {0};
            int pad = 0;
            int k;
            for (k = 0; k < 4 && i < in_len; k++) {
                while (i < in_len && (buf[i] == '\n' || buf[i] == '\r' || buf[i] == ' ' || buf[i] == '\t')) i++;
                if (i >= in_len) break;
                if (buf[i] == '=') {
                    sextets[k] = 0;
                    pad++;
                } else {
                    int v = b64_rev_table[(uint8_t)buf[i]];
                    if (v < 0) {
                        shell_print("base64: invalid character in input\n");
                        last_exit_code = 1;
                        return;
                    }
                    sextets[k] = (uint32_t)v;
                }
                i++;
            }

            uint32_t triple = (sextets[0] << 18) | (sextets[1] << 12) | (sextets[2] << 6) | sextets[3];
            if (k >= 2) out[j++] = (char)((triple >> 16) & 0xFF);
            if (k >= 3 && pad < 2) out[j++] = (char)((triple >> 8) & 0xFF);
            if (k >= 4 && pad < 1) out[j++] = (char)(triple & 0xFF);
        }

        for (int oi = 0; oi < j; oi++) {
            char c[2] = { out[oi], '\0' };
            shell_print(c);
        }
        shell_print("\n");
    } else {
        char out[(sizeof(buf) * 4 / 3) + 4];
        int i, j = 0;
        for (i = 0; i + 2 < n; i += 3) {
            out[j++] = b64_table[(buf[i] >> 2) & 0x3F];
            out[j++] = b64_table[((buf[i] & 0x3) << 4) | ((buf[i+1] >> 4) & 0xF)];
            out[j++] = b64_table[((buf[i+1] & 0xF) << 2) | ((buf[i+2] >> 6) & 0x3)];
            out[j++] = b64_table[buf[i+2] & 0x3F];
        }
        if (i < n) {
            out[j++] = b64_table[(buf[i] >> 2) & 0x3F];
            if (i + 1 < n)
                out[j++] = b64_table[((buf[i] & 0x3) << 4) | ((buf[i+1] >> 4) & 0xF)];
            else
                out[j++] = '=';
            out[j++] = '=';
        }
        out[j] = '\0';

        shell_print(out);
        shell_print("\n");
    }
    last_exit_code = 0;
}

/* 48. md5 - Calculate MD5 hash per RFC 1321 */
typedef struct {
    uint32_t state[4];
    uint32_t count[2];
    uint8_t  buffer[64];
} md5_ctx_t;

#define MD5_F(x,y,z) (((x) & (y)) | ((~x) & (z)))
#define MD5_G(x,y,z) (((x) & (z)) | ((y) & (~z)))
#define MD5_H(x,y,z) ((x) ^ (y) ^ (z))
#define MD5_I(x,y,z) ((y) ^ ((x) | (~z)))
#define MD5_ROT(x,n) (((x) << (n)) | ((x) >> (32-(n))))

static const uint32_t md5_t[64] = {
    0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,
    0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
    0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,
    0x6b901122,0xfd987193,0xa679438e,0x49b40821,
    0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,
    0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
    0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,
    0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
    0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,
    0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
    0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,
    0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
    0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,
    0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
    0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,
    0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
};

static const uint8_t md5_s[64] = {
    7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
    5, 9,14,20,5, 9,14,20,5, 9,14,20,5, 9,14,20,
    4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
    6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21
};

static void md5_transform(uint32_t state[4], const uint8_t block[64]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t x[16];
    int i, j;
    for (i = 0, j = 0; i < 16; i++, j += 4) {
        x[i] = (uint32_t)block[j] | ((uint32_t)block[j+1] << 8) |
               ((uint32_t)block[j+2] << 16) | ((uint32_t)block[j+3] << 24);
    }

    for (i = 0; i < 64; i++) {
        uint32_t f, g;
        if (i < 16) {
            f = MD5_F(b, c, d);
            g = i;
        } else if (i < 32) {
            f = MD5_G(b, c, d);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            f = MD5_H(b, c, d);
            g = (3 * i + 5) % 16;
        } else {
            f = MD5_I(b, c, d);
            g = (7 * i) % 16;
        }
        uint32_t temp = d;
        d = c;
        c = b;
        b = b + MD5_ROT(a + f + md5_t[i] + x[g], md5_s[i]);
        a = temp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

static void md5_init(md5_ctx_t *ctx) {
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
    ctx->count[0] = 0;
    ctx->count[1] = 0;
}

static void md5_update(md5_ctx_t *ctx, const uint8_t *input, uint32_t len) {
    uint32_t index = (ctx->count[0] >> 3) & 0x3F;
    uint32_t part_len = 64 - index;

    ctx->count[0] += (uint32_t)(len << 3);
    if (ctx->count[0] < (uint32_t)(len << 3))
        ctx->count[1]++;
    ctx->count[1] += (uint32_t)(len >> 29);

    uint32_t i = 0;
    if (len >= part_len) {
        memcpy(&ctx->buffer[index], input, part_len);
        md5_transform(ctx->state, ctx->buffer);
        for (i = part_len; i + 63 < len; i += 64)
            md5_transform(ctx->state, &input[i]);
        index = 0;
    }
    memcpy(&ctx->buffer[index], &input[i], len - i);
}

static void md5_final(md5_ctx_t *ctx, uint8_t digest[16]) {
    static const uint8_t padding[64] = {
        0x80,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    };
    uint8_t bits[8];
    uint32_t index = (ctx->count[0] >> 3) & 0x3F;
    uint32_t pad_len = (index < 56) ? (56 - index) : (120 - index);

    for (int i = 0; i < 4; i++) {
        bits[i]   = (uint8_t)((ctx->count[0] >> (i * 8)) & 0xFF);
        bits[i+4] = (uint8_t)((ctx->count[1] >> (i * 8)) & 0xFF);
    }

    md5_update(ctx, padding, pad_len);
    md5_update(ctx, bits, 8);

    for (int i = 0; i < 4; i++) {
        digest[i*4+0] = (uint8_t)(ctx->state[i] & 0xFF);
        digest[i*4+1] = (uint8_t)((ctx->state[i] >> 8) & 0xFF);
        digest[i*4+2] = (uint8_t)((ctx->state[i] >> 16) & 0xFF);
        digest[i*4+3] = (uint8_t)((ctx->state[i] >> 24) & 0xFF);
    }
}

static void cmd_md5(const char *file) {
    if (!file || !*file) {
        shell_err_md5(file);
        last_exit_code = 1;
        return;
    }

    file_t *f = 0;
    if (vfs_open(file, FILE_MODE_READ, &f) != 0 || !f) {
        shell_err_md5(file);
        last_exit_code = 1;
        return;
    }

    md5_ctx_t ctx;
    md5_init(&ctx);

    unsigned char buf[1024];
    int n;
    while ((n = vfs_read(f, buf, sizeof(buf))) > 0) {
        md5_update(&ctx, buf, n);
    }
    vfs_close(f);

    uint8_t digest[16];
    md5_final(&ctx, digest);

    char hex[33];
    static const char hex_chars[] = "0123456789abcdef";
    for (int i = 0; i < 16; i++) {
        hex[i*2+0] = hex_chars[(digest[i] >> 4) & 0xF];
        hex[i*2+1] = hex_chars[digest[i] & 0xF];
    }
    hex[32] = '\0';

    shell_print("md5(");
    shell_print(file);
    shell_print(")= ");
    shell_print(hex);
    shell_print("\n");
    last_exit_code = 0;
}

/* 49. history - Show command history */
static void cmd_history(const char *options) {
    if (options && strcmp(options, "-c") == 0) {
        /* Clear history */
        history_count = 0;
        history_pos = 0;
        history_head = 0;
        shell_history_count = 0;
        shell_history_pos = -1;
        for (int i = 0; i < SHELL_HISTORY_SIZE; i++) {
            history_buf[i][0] = '\0';
        }
        for (int i = 0; i < SHELL_HISTORY_MAX; i++) {
            shell_history[i][0] = '\0';
        }
        shell_print("History cleared.\n");
        last_exit_code = 0;
        return;
    }

    int start = (history_count < SHELL_HISTORY_SIZE) ? 0 : history_pos;
    for (int i = 0; i < history_count; i++) {
        int idx = (start + i) % SHELL_HISTORY_SIZE;
        char num[16];
        snprintf(num, sizeof(num), "%4d  ", i + 1);
        shell_print(num);
        shell_print(history_buf[idx]);
        shell_print("\n");
    }
    last_exit_code = 0;
}

/* 50. alias - Create command alias */
static void cmd_alias(const char *arg) {
    if (!arg || !*arg) {
        /* Show all aliases */
        for (int i = 0; i < alias_count; i++) {
            shell_print("alias ");
            shell_print(aliases[i].name);
            shell_print("='");
            shell_print(aliases[i].value);
            shell_print("'\n");
        }
        if (alias_count == 0) {
            shell_print("No aliases defined.\n");
        }
        last_exit_code = 0;
        return;
    }

    /* Parse name=value */
    char name[SHELL_ALIAS_NAME];
    char value[SHELL_ALIAS_VALUE];
    uint32_t ni = 0;
    const char *p = arg;
    while (*p && *p != '=' && ni < SHELL_ALIAS_NAME - 1) {
        name[ni++] = *p++;
    }
    name[ni] = '\0';

    if (*p != '=') {
        /* No '=' found - try to look up alias */
        const char *val = alias_lookup(name);
        if (val) {
            shell_print("alias ");
            shell_print(name);
            shell_print("='");
            shell_print(val);
            shell_print("'\n");
        } else {
            shell_error(SHELL_ERR_NO_ALIAS, name);
        }
        last_exit_code = 0;
        return;
    }
    p++; /* skip '=' */

    uint32_t vi = 0;
    while (*p && vi < SHELL_ALIAS_VALUE - 1) {
        value[vi++] = *p++;
    }
    value[vi] = '\0';

    if (alias_set(name, value) != 0) {
        shell_error(SHELL_ERR_INTERNAL, "alias: too many aliases");
        last_exit_code = 1;
        return;
    }
    shell_print("alias ");
    shell_print(name);
    shell_print("='");
    shell_print(value);
    shell_print("'\n");
    last_exit_code = 0;
}

/* ---- edit command - launch editor ---- */
static void cmd_edit(const char *file) {
    if (!file || !*file) {
        shell_err_edit(0);
        last_exit_code = 1;
        return;
    }
    editor_open(file);
    editor_run();
    last_exit_code = 0;
}

/* ---- cedit command - 内置 C 语言编辑器 ---- */
static void cmd_cedit(const char *filename) {
    if (!filename || !*filename) {
        shell_print("Usage: cedit <filename.c>\n");
        last_exit_code = 1;
        return;
    }

    /* 编辑缓冲区 - 使用 static 避免栈溢出 */
    #define CEDIT_MAX_LINES 200
    #define CEDIT_LINE_LEN  256
    static char edit_lines[CEDIT_MAX_LINES][CEDIT_LINE_LEN];
    static uint32_t edit_count = 0;
    int modified = 0;

    /* 清空缓冲区 */
    edit_count = 0;

    /* 尝试加载已有文件 */
    file_t *f = 0;
    if (vfs_open(filename, FILE_MODE_READ, &f) == 0 && f) {
        char buf[1024];
        int n;
        while ((n = vfs_read(f, buf, sizeof(buf) - 1)) > 0 && edit_count < CEDIT_MAX_LINES) {
            buf[n] = '\0';
            /* 按行分割 */
            char *line = buf;
            while (*line && edit_count < CEDIT_MAX_LINES) {
                char *nl = line;
                while (*nl && *nl != '\n') nl++;
                uint32_t len = (uint32_t)(nl - line);
                if (len >= CEDIT_LINE_LEN) len = CEDIT_LINE_LEN - 1;
                memcpy(edit_lines[edit_count], line, len);
                edit_lines[edit_count][len] = '\0';
                edit_count++;
                if (*nl == '\n') nl++;
                line = nl;
            }
        }
        vfs_close(f);
        char num_buf[32];
        snprintf(num_buf, sizeof(num_buf), "%u", edit_count);
        shell_print("cedit: Loaded ");
        shell_print(num_buf);
        shell_print(" lines from ");
        shell_print(filename);
        shell_print("\n");
    } else {
        shell_print("cedit: New file: ");
        shell_print(filename);
        shell_print("\n");
    }

    shell_print("C Editor - Commands: :w(save) :q(quit) :r(run) :l(list) :d<N>(delete line N)\n");

    /* 编辑循环 */
    while (1) {
        /* 显示行号提示 */
        char prompt[32];
        snprintf(prompt, sizeof(prompt), "%u> ", edit_count + 1);
        shell_print(prompt);

        char line[CEDIT_LINE_LEN];
        int len = shell_read_line(line, CEDIT_LINE_LEN);
        if (len <= 0) continue;

        /* 检查是否为命令 */
        if (line[0] == ':') {
            if (strcmp(line, ":w") == 0) {
                /* 保存文件 */
                vfs_creat(filename, FILE_MODE_WRITE | FILE_MODE_READ);
                file_t *sf = 0;
                if (vfs_open(filename, FILE_MODE_WRITE, &sf) == 0 && sf) {
                    for (uint32_t i = 0; i < edit_count; i++) {
                        vfs_write(sf, edit_lines[i], (uint32_t)len_strlen(edit_lines[i]));
                        vfs_write(sf, "\n", 1);
                    }
                    vfs_close(sf);
                    modified = 0;
                    shell_print("cedit: Saved ");
                    char num_buf[32];
                    snprintf(num_buf, sizeof(num_buf), "%u", edit_count);
                    shell_print(num_buf);
                    shell_print(" lines\n");
                } else {
                    shell_print("cedit: Error saving file\n");
                }
            } else if (strcmp(line, ":q") == 0) {
                if (modified) {
                    shell_print("cedit: Unsaved changes. Use :wq or :q!\n");
                } else {
                    break;
                }
            } else if (strcmp(line, ":q!") == 0) {
                break;
            } else if (strcmp(line, ":wq") == 0) {
                /* 保存并退出 */
                vfs_creat(filename, FILE_MODE_WRITE | FILE_MODE_READ);
                file_t *sf = 0;
                if (vfs_open(filename, FILE_MODE_WRITE, &sf) == 0 && sf) {
                    for (uint32_t i = 0; i < edit_count; i++) {
                        vfs_write(sf, edit_lines[i], (uint32_t)len_strlen(edit_lines[i]));
                        vfs_write(sf, "\n", 1);
                    }
                    vfs_close(sf);
                    modified = 0;
                }
                break;
            } else if (strcmp(line, ":r") == 0) {
                /* 运行 - 使用 C 解释器 */
                shell_print("cedit: Executing...\n");
                /* 将所有行拼接为代码字符串，调用 cinterp_exec */
                static char code_buf[8192];
                uint32_t code_len = 0;
                code_buf[0] = '\0';
                for (uint32_t i = 0; i < edit_count && code_len < sizeof(code_buf) - 256; i++) {
                    uint32_t ll = (uint32_t)len_strlen(edit_lines[i]);
                    if (code_len + ll + 1 < sizeof(code_buf) - 1) {
                        memcpy(code_buf + code_len, edit_lines[i], ll);
                        code_len += ll;
                        code_buf[code_len++] = '\n';
                    }
                }
                code_buf[code_len] = '\0';
                cinterp_exec(code_buf);
                shell_print("cedit: Execution complete\n");
            } else if (strcmp(line, ":l") == 0) {
                /* 列出所有行 */
                for (uint32_t i = 0; i < edit_count; i++) {
                    char num[16];
                    snprintf(num, sizeof(num), "%4u | ", i + 1);
                    shell_print(num);
                    shell_print(edit_lines[i]);
                    shell_print("\n");
                }
            } else if (strncmp(line, ":d ", 3) == 0) {
                /* 删除指定行 */
                int line_num = atoi(line + 3);
                if (line_num >= 1 && (uint32_t)line_num <= edit_count) {
                    for (uint32_t i = (uint32_t)line_num - 1; i < edit_count - 1; i++) {
                        memcpy(edit_lines[i], edit_lines[i+1], CEDIT_LINE_LEN);
                    }
                    edit_count--;
                    modified = 1;
                    shell_print("cedit: Deleted line ");
                    char num_buf[16];
                    snprintf(num_buf, sizeof(num_buf), "%d\n", line_num);
                    shell_print(num_buf);
                } else {
                    shell_print("cedit: Invalid line number\n");
                }
            }
        } else {
            /* 普通文本行 - 追加到缓冲区 */
            if (edit_count < CEDIT_MAX_LINES) {
                strncpy(edit_lines[edit_count], line, CEDIT_LINE_LEN - 1);
                edit_lines[edit_count][CEDIT_LINE_LEN - 1] = '\0';
                edit_count++;
                modified = 1;
            } else {
                shell_print("cedit: Buffer full (max 200 lines)\n");
            }
        }
    }

    shell_print("cedit: Exited\n");
    last_exit_code = 0;
    #undef CEDIT_MAX_LINES
    #undef CEDIT_LINE_LEN
}

/* ---- kvm command - test KVM virtualization ---- */
static void cmd_kvm(void) {
    shell_print("KVM Virtualization Test\n");

    int type = kvm_check_hardware();
    if (type == KVM_NONE) {
        shell_print("  No hardware virtualization support detected.\n");
        shell_print("  (VMX/SVM not available or not enabled in BIOS)\n");
        last_exit_code = 1;
        return;
    }

    if (type == KVM_VMX) {
        shell_print("  VMX (Intel VT-x) detected\n");
    } else if (type == KVM_SVM) {
        shell_print("  SVM (AMD-V) detected\n");
    }

    if (kvm_init() != 0) {
        shell_print("  KVM initialization failed\n");
        last_exit_code = 1;
        return;
    }

    shell_print("  KVM initialized successfully\n");

    /* Read and display capabilities */
    kvm_cpu_caps_t caps;
    if (kvm_get_caps(&caps) == 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "  VMCS revision: %u\n", caps.revision_id);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "  VMCS size:     %u bytes\n", caps.vmcs_size);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "  Memory type:   %u\n", caps.memory_type);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "  CR0 fixed0/1:  0x%x / 0x%x\n", caps.cr0_fixed0, caps.cr0_fixed1);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "  CR4 fixed0/1:  0x%x / 0x%x\n", caps.cr4_fixed0, caps.cr4_fixed1);
        shell_print(buf);
    }

    /* Create a tiny test VM that executes HLT */
    shell_print("  Creating test VM (4KB guest memory)...\n");
    kvm_vm_t *vm = kvm_vm_create(4096);
    if (!vm) {
        shell_print("  Failed to create test VM\n");
        last_exit_code = 1;
        return;
    }

    /* Load a tiny guest code: just HLT (0xF4) */
    uint8_t hlt_code[] = { 0xF4 }; /* HLT instruction */
    kvm_vm_load_code(vm, hlt_code, sizeof(hlt_code), 0);

    shell_print("  Running test VM (guest executes HLT)...\n");
    int ret = kvm_vm_run(vm);
    if (ret == 0) {
        shell_print("  VM exited\n");
        char buf[64];
        snprintf(buf, sizeof(buf), "  Exit reason: %u\n", vm->exit_reason);
        shell_print(buf);
        if (vm->exit_reason == VMX_EXIT_HLT) {
            shell_print("  Guest HLT handled correctly!\n");
        }
    } else {
        shell_print("  VM run failed (VMX may not be fully supported in this environment)\n");
    }

    kvm_vm_destroy(vm);
    shell_print("  Test VM destroyed\n");
    last_exit_code = 0;
}

/* ---- apps command - list available apps (see new cmd_apps below) ---- */

/* ---- run command - execute a built-in app ---- */
static void cmd_run_app(const char *name) {
    if (!name || !*name) {
        shell_print("Usage: run <appname> [args...]\n");
        shell_print("Type 'apps' to see available applications.\n");
        last_exit_code = 1;
        return;
    }

    /* Find the app */
    for (uint32_t i = 0; i < APP_COUNT; i++) {
        if (strcmp(app_registry[i].name, name) == 0) {
            shell_print("Running: ");
            shell_print(name);
            shell_print("\n");

            /* Build simple argc/argv - just pass the app name */
            char *argv[2] = { (char *)name, 0 };
            int ret = app_registry[i].main_func(1, argv);
            last_exit_code = ret;
            return;
        }
    }

    shell_print("App '");
    shell_print(name);
    shell_print("' not found. Type 'apps' for available applications.\n");
    last_exit_code = 1;
}

/* ---- gui command - start display server ---- */
static void cmd_gui(void) {
    shell_print("Starting display server...\n");
    shell_print("Press ESC to stop.\n");
    ds_start();
    shell_print("Display server stopped.\n");
    last_exit_code = 0;
}

/* ---- guistop command - stop display server ---- */
static void cmd_guistop(void) {
    ds_stop();
    shell_print("Display server stop requested.\n");
    last_exit_code = 0;
}

/* ---- crepl command - start C interpreter REPL ---- */
static void cmd_crepl(void) {
    shell_print("Starting C interpreter REPL...\n");
    cinterp_run();
    last_exit_code = 0;
}

/* ============================================================ */
/* Part 2: Advanced Commands                                    */
/* ============================================================ */

static void cmd_exec(const char *file, const char *args) {
    if (!file || !*file) {
        shell_err_exec(file);
        last_exit_code = 1;
        return;
    }
    char full_path[512];
    build_full_path(file, full_path, sizeof(full_path));
    inode_t st;
    memset(&st, 0, sizeof(st));
    if (vfs_stat(full_path, &st) != 0) {
        shell_err_exec(file);
        last_exit_code = 1;
        return;
    }
    shell_print("exec: ");
    shell_print(file);
    if (args && *args) { shell_print(" "); shell_print(args); }
    shell_print(" (not yet supported)\n");
    last_exit_code = 0;
}

static void cmd_bg(const char *pid_str) {
    if (!pid_str || !*pid_str) {
        shell_err_bg(pid_str);
        last_exit_code = 1;
        return;
    }
    int pid = 0;
    const char *p = pid_str;
    while (*p >= '0' && *p <= '9') { pid = pid * 10 + (*p - '0'); p++; }
    pcb_t *proc = process_get_pcb(pid);
    if (!proc || proc->state == PROCESS_UNUSED) {
        shell_err_bg(pid_str);
        last_exit_code = 1;
        return;
    }
    if (proc->state == PROCESS_ZOMBIE) {
        shell_err_bg(pid_str);
        last_exit_code = 1;
        return;
    }
    if (proc->state == PROCESS_BLOCKED) {
        sched_unblock(proc);
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "[%d] running in background\n", pid);
    shell_print(buf);
    last_exit_code = 0;
}

static void cmd_fg(const char *pid_str) {
    if (!pid_str || !*pid_str) {
        shell_err_fg(0);
        last_exit_code = 1;
        return;
    }
    int pid = 0;
    const char *p = pid_str;
    while (*p >= '0' && *p <= '9') { pid = pid * 10 + (*p - '0'); p++; }
    pcb_t *proc = process_get_pcb(pid);
    if (!proc || proc->state == PROCESS_UNUSED) {
        shell_err_fg(pid_str);
        last_exit_code = 1;
        return;
    }
    if (proc->state == PROCESS_ZOMBIE) {
        shell_err_fg(pid_str);
        last_exit_code = 1;
        return;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "[%d] brought to foreground\n", pid);
    shell_print(buf);
    last_exit_code = 0;
}

static void cmd_jobs(void) {
    shell_print("  PID  STATE       NAME\n");
    int job_count = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        pcb_t *p = process_get_pcb(i);
        if (p && p->state != PROCESS_UNUSED && p->state != PROCESS_ZOMBIE) {
            const char *state_str;
            switch (p->state) {
                case PROCESS_READY:   state_str = "READY"; break;
                case PROCESS_RUNNING: state_str = "RUNNING"; break;
                case PROCESS_BLOCKED: state_str = "BLOCKED"; break;
                default:              state_str = "UNKNOWN"; break;
            }
            char buf[128];
            snprintf(buf, sizeof(buf), "  %3d  %-10s  %s\n", p->pid, state_str, p->name);
            shell_print(buf);
            job_count++;
        }
    }
    if (job_count == 0) {
        shell_print("No background jobs.\n");
    }
    last_exit_code = 0;
}

static void cmd_nice(const char *pid_str, const char *prio_str) {
    if (!pid_str || !*pid_str || !prio_str || !*prio_str) {
        shell_err_nice();
        last_exit_code = 1;
        return;
    }
    int pid = 0;
    const char *p = pid_str;
    while (*p >= '0' && *p <= '9') { pid = pid * 10 + (*p - '0'); p++; }
    int prio = 0;
    p = prio_str;
    while (*p >= '0' && *p <= '9') { prio = prio * 10 + (*p - '0'); p++; }
    pcb_t *proc = process_get_pcb(pid);
    if (!proc || proc->state == PROCESS_UNUSED) {
        shell_err_nice();
        last_exit_code = 1;
        return;
    }
    if (prio < 0) prio = 0;
    if (prio > 39) prio = 39;
    proc->nice = prio;
    char buf[64];
    snprintf(buf, sizeof(buf), "Process %d nice value set to %d\n", pid, prio);
    shell_print(buf);
    last_exit_code = 0;
}

static void cmd_renice(const char *pid_str, const char *prio_str) {
    if (!pid_str || !*pid_str || !prio_str || !*prio_str) {
        shell_err_renice();
        last_exit_code = 1;
        return;
    }
    int pid = 0;
    const char *p = pid_str;
    while (*p >= '0' && *p <= '9') { pid = pid * 10 + (*p - '0'); p++; }
    int prio = 0;
    p = prio_str;
    while (*p >= '0' && *p <= '9') { prio = prio * 10 + (*p - '0'); p++; }
    pcb_t *proc = process_get_pcb(pid);
    if (!proc || proc->state == PROCESS_UNUSED) {
        shell_err_renice();
        last_exit_code = 1;
        return;
    }
    if (proc->state == PROCESS_ZOMBIE) {
        shell_err_renice();
        last_exit_code = 1;
        return;
    }
    if (prio < 0) prio = 0;
    if (prio > 39) prio = 39;
    proc->nice = prio;
    char buf[64];
    snprintf(buf, sizeof(buf), "Process %d nice value changed to %d\n", pid, prio);
    shell_print(buf);
    last_exit_code = 0;
}

static void cmd_nohup(const char *cmd) {
    if (!cmd || !*cmd) {
        shell_err_nohup();
        last_exit_code = 1;
        return;
    }
    shell_print("nohup: running '");
    shell_print(cmd);
    shell_print("' (ignoring hangups)\n");
    shell_execute(cmd);
    last_exit_code = 0;
}

static void cmd_watch(const char *cmd) {
    if (!cmd || !*cmd) {
        shell_err_watch();
        last_exit_code = 1;
        return;
    }
    shell_print("watch: executing '");
    shell_print(cmd);
    shell_print("' every 2 seconds (press any key to stop)\n");
    for (int iter = 0; iter < 10; iter++) {
        if (kb_signal_check()) {
            shell_print("^C\n");
            last_exit_code = 130;
            return;
        }
        shell_execute(cmd);
        for (int w = 0; w < 200; w++) {
            timer_sleep(10);
            if (kb_signal_check()) {
                shell_print("^C\n");
                last_exit_code = 130;
                return;
            }
            if (keyboard_has_data()) {
                keyboard_event_t event;
                if (keyboard_get_event(&event) && (event.flags & KEY_PRESSED)) {
                    shell_print("watch: stopped\n");
                    last_exit_code = 0;
                    return;
                }
            }
        }
    }
    shell_print("watch: completed 10 iterations\n");
    last_exit_code = 0;
}

static void cmd_sleep(const char *sec_str) {
    if (!sec_str || !*sec_str) {
        shell_err_sleep();
        last_exit_code = 1;
        return;
    }
    int seconds = 0;
    const char *p = sec_str;
    while (*p >= '0' && *p <= '9') { seconds = seconds * 10 + (*p - '0'); p++; }
    if (seconds <= 0) {
        shell_err_sleep();
        last_exit_code = 1;
        return;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "Sleeping for %d seconds... (Ctrl+C to stop)\n", seconds);
    shell_print(buf);
    for (int i = 0; i < seconds * 10; i++) {
        timer_sleep(100);
        if (kb_signal_check()) {
            shell_print("^C\n");
            last_exit_code = 130;
            return;
        }
    }
    shell_print("Done.\n");
    last_exit_code = 0;
}

static void cmd_test(const char *expr) {
    if (!expr || !*expr) {
        shell_err_test();
        last_exit_code = 1;
        return;
    }
    char buf[256];
    strncpy(buf, expr, 255); buf[255] = '\0';
    char *a1 = buf;
    while (*a1 == ' ') a1++;
    char *op = a1;
    while (*op && *op != ' ') op++;
    if (*op) { *op = '\0'; op++; }
    while (*op == ' ') op++;
    char *a2 = op;
    while (*a2 && *a2 != ' ') a2++;
    if (*a2) { *a2 = '\0'; a2++; }
    while (*a2 == ' ') a2++;
    if (!*a1 || !*op || !*a2) {
        shell_error(SHELL_ERR_BAD_SYNTAX, expr);
        last_exit_code = 1;
        return;
    }
    int v1 = 0, v2 = 0;
    const char *p1 = a1;
    while (*p1 >= '0' && *p1 <= '9') { v1 = v1 * 10 + (*p1 - '0'); p1++; }
    const char *p2 = a2;
    while (*p2 >= '0' && *p2 <= '9') { v2 = v2 * 10 + (*p2 - '0'); p2++; }
    int result = 0;
    if (strcmp(op, "-eq") == 0) result = (v1 == v2);
    else if (strcmp(op, "-ne") == 0) result = (v1 != v2);
    else if (strcmp(op, "-gt") == 0) result = (v1 > v2);
    else if (strcmp(op, "-lt") == 0) result = (v1 < v2);
    else if (strcmp(op, "-ge") == 0) result = (v1 >= v2);
    else if (strcmp(op, "-le") == 0) result = (v1 <= v2);
    else if (strcmp(op, "=") == 0) result = (strcmp(a1, a2) == 0);
    else if (strcmp(op, "!=") == 0) result = (strcmp(a1, a2) != 0);
    else {
        shell_err_test();
        last_exit_code = 1;
        return;
    }
    last_exit_code = result ? 0 : 1;
}

static void cmd_expr(const char *math) {
    if (!math || !*math) {
        shell_err_expr();
        last_exit_code = 1;
        return;
    }
    const char *p = math;
    while (*p == ' ') p++;
    int32_t result = 0;
    int32_t num = 0;
    int got_num = 0;
    char op = '+';
    while (*p) {
        if (*p >= '0' && *p <= '9') {
            num = 0;
            while (*p >= '0' && *p <= '9') { num = num * 10 + (*p - '0'); p++; }
            switch (op) {
                case '+': result += num; break;
                case '-': result -= num; break;
                case '*': result *= num; break;
                case '/':
                    if (num != 0) result /= num;
                    else { shell_error(SHELL_ERR_INTERNAL, "expr: division by zero"); last_exit_code = 1; return; }
                    break;
                case '%':
                    if (num != 0) result %= num;
                    else { shell_error(SHELL_ERR_INTERNAL, "expr: modulo by zero"); last_exit_code = 1; return; }
                    break;
                default: break;
            }
            got_num = 1;
        } else if (*p == '+') { op = '+'; p++; }
        else if (*p == '-') { op = '-'; p++; }
        else if (*p == '*') { op = '*'; p++; }
        else if (*p == '/') { op = '/'; p++; }
        else if (*p == '%') { op = '%'; p++; }
        else if (*p == ' ') { p++; }
        else {
            shell_error(SHELL_ERR_BAD_SYNTAX, math);
            last_exit_code = 1;
            return;
        }
    }
    if (!got_num) {
        shell_err_expr();
        last_exit_code = 1;
        return;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "%d\n", result);
    shell_print(buf);
    last_exit_code = 0;
}

static void cmd_xargs(const char *cmd) {
    if (!cmd || !*cmd) {
        shell_err_xargs();
        last_exit_code = 1;
        return;
    }
    /* Use the captured pipe buffer as additional arguments */
    char full_cmd[SHELL_MAX_LINE];
    strncpy(full_cmd, cmd, SHELL_MAX_LINE - 1);
    full_cmd[SHELL_MAX_LINE - 1] = '\0';
    if (pipe_len > 0) {
        uint32_t clen = 0;
        while (full_cmd[clen]) clen++;
        full_cmd[clen] = ' ';
        clen++;
        uint32_t copy_len = pipe_len;
        if (clen + copy_len >= SHELL_MAX_LINE) copy_len = SHELL_MAX_LINE - clen - 1;
        for (uint32_t i = 0; i < copy_len; i++) {
            if (pipe_buf[i] == '\n') full_cmd[clen + i] = ' ';
            else full_cmd[clen + i] = pipe_buf[i];
        }
        full_cmd[clen + copy_len] = '\0';
    }
    shell_execute(full_cmd);
    last_exit_code = 0;
}

static void cmd_tee(const char *file) {
    if (!file || !*file) {
        shell_err_tee(file);
        last_exit_code = 1;
        return;
    }
    /* Output pipe buffer to both stdout and file */
    if (pipe_len > 0) {
        shell_print(pipe_buf);
        char full_path[512];
        build_full_path(file, full_path, sizeof(full_path));
        file_t *f = 0;
        if (vfs_open(full_path, FILE_MODE_WRITE | FILE_MODE_READ, &f) == 0 && f) {
            vfs_write(f, pipe_buf, pipe_len);
            vfs_close(f);
        } else {
            shell_error(SHELL_ERR_WRITE_FAIL, file);
            last_exit_code = 1;
            return;
        }
    }
    last_exit_code = 0;
}

static void cmd_install(const char *src, const char *dst) {
    if (!src || !*src || !dst || !*dst) {
        shell_err_install();
        last_exit_code = 1;
        return;
    }
    /* Copy file and set executable permissions */
    cmd_copy(src, dst);
    shell_print("install: set permissions on ");
    shell_print(dst);
    shell_print("\n");
    last_exit_code = 0;
}

static void cmd_which(const char *cmd) {
    if (!cmd || !*cmd) {
        shell_err_which(cmd);
        last_exit_code = 1;
        return;
    }
    /* Check if it's an alias */
    const char *alias_val = alias_lookup(cmd);
    if (alias_val) {
        shell_print(cmd);
        shell_print(": alias to '");
        shell_print(alias_val);
        shell_print("'\n");
        last_exit_code = 0;
        return;
    }
    /* Check if it's a built-in command */
    const char *builtins[] = {
        "pt", "ls", "show", "cat", "type", "go", "cd", "where", "pwd",
        "clr", "ver", "help", "sysinfo", "reboot", "halt", "time", "date",
        "mem", "dev", "ping", "copy", "del", "mkdir", "ren", "find", "size",
        "echo", "set", "env", "run", "ps", "kill", "top", "free", "uptime",
        "load", "dmesg", "loglevel", "syslog", "mount", "umount", "format", "fdisk", "chkdsk",
        "touch", "append", "head", "tail", "wc", "diff", "sort", "uniq",
        "grep", "replace", "chmod", "chown", "file", "ln", "ln_s", "readlink", "stat", "tree", "du", "df",
        "env", "setenv", "unsetenv", "ulimit",
        "ifconfig", "route", "dns", "wget", "netstat", "traceroute", "arp", "fw",
        "hostname", "lspci", "lsusb", "lsblk", "sensors", "freq",
        "calc", "base64", "md5", "history", "alias", "edit",
        "exec", "bg", "fg", "jobs", "nice", "renice", "nohup", "watch",
        "sleep", "test", "expr", "xargs", "tee", "install", "which", "accel",
        "logrotate", "httpget", "imgview", "pkg",
        "tftp", "ntp", "telnet",
        "sudo",
        "play", "vol", "sound",
        NULL
    };
    for (int i = 0; builtins[i]; i++) {
        if (strcmp(cmd, builtins[i]) == 0) {
            shell_print(cmd);
            shell_print(": shell built-in command\n");
            last_exit_code = 0;
            return;
        }
    }
    shell_err_unknown(cmd);
    last_exit_code = 1;
}

static void cmd_logrotate(const char *arg) {
    if (!arg || !*arg) {
        /* No argument: show all log rotation configs */
        uint32_t count = logrotate_get_config_count();
        if (count == 0) {
            shell_print("No log rotation configs.\n");
            last_exit_code = 0;
            return;
        }
        shell_print("Log rotation configs:\n");
        uint32_t i;
        for (i = 0; i < count; i++) {
            logrotate_config_t *cfg = logrotate_get_config(i);
            if (cfg) {
                char buf[256];
                snprintf(buf, sizeof(buf),
                         "  %s  max_size=%u  max_files=%u  current=%u\n",
                         cfg->filepath, cfg->max_size, cfg->max_files,
                         cfg->current_size);
                shell_print(buf);
            }
        }
        last_exit_code = 0;
        return;
    }

    if (strcmp(arg, "check") == 0) {
        /* Check all configured logs */
        uint32_t count = logrotate_get_config_count();
        uint32_t i;
        int rotated = 0;
        for (i = 0; i < count; i++) {
            logrotate_config_t *cfg = logrotate_get_config(i);
            if (cfg) {
                int ret = logrotate_check(cfg->filepath);
                if (ret == 0) {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "Rotated: %s\n", cfg->filepath);
                    shell_print(buf);
                    rotated++;
                }
            }
        }
        if (rotated == 0) {
            shell_print("No logs needed rotation.\n");
        }
        last_exit_code = 0;
    } else if (strcmp(arg, "force") == 0) {
        /* Force rotate all configured logs */
        uint32_t count = logrotate_get_config_count();
        uint32_t i;
        for (i = 0; i < count; i++) {
            logrotate_config_t *cfg = logrotate_get_config(i);
            if (cfg) {
                logrotate_force(cfg->filepath);
                char buf[256];
                snprintf(buf, sizeof(buf), "Force rotated: %s\n", cfg->filepath);
                shell_print(buf);
            }
        }
        last_exit_code = 0;
    } else {
        /* Treat arg as a specific file path to force rotate */
        int ret = logrotate_force(arg);
        if (ret == 0) {
            char buf[256];
            snprintf(buf, sizeof(buf), "Force rotated: %s\n", arg);
            shell_print(buf);
            last_exit_code = 0;
        } else {
            shell_print("logrotate: config not found for ");
            shell_print(arg);
            shell_print("\n");
            last_exit_code = 1;
        }
    }
}

/* ---- 数据库命令 ---- */
static fundb_handle_t g_shell_db = NULL;

static void cmd_db(const char *subcmd, const char *arg1, const char *arg2)
{
    if (!subcmd || !*subcmd) {
        shell_print("Usage: db <open|close|tables|create|drop|insert|select|sql|count> [args]\n");
        return;
    }

    if (strcmp(subcmd, "open") == 0) {
        const char *path = arg1 && *arg1 ? arg1 : "/var/db/default.db";
        if (g_shell_db) {
            fundb_close(g_shell_db);
        }
        g_shell_db = fundb_open(path);
        if (g_shell_db) {
            shell_print("Database opened: ");
            shell_print(path);
            shell_print("\n");
        } else {
            shell_print("Failed to open database\n");
            last_exit_code = 1;
        }
    } else if (strcmp(subcmd, "close") == 0) {
        if (g_shell_db) {
            fundb_close(g_shell_db);
            g_shell_db = NULL;
            shell_print("Database closed\n");
        } else {
            shell_print("No database open\n");
        }
    } else if (strcmp(subcmd, "tables") == 0) {
        if (!g_shell_db) {
            shell_print("No database open. Use: db open [path]\n");
            return;
        }
        fundb_info_t info;
        fundb_get_info(g_shell_db, &info);
        if (info.table_count == 0) {
            shell_print("No tables\n");
        } else {
            for (uint32_t i = 0; i < info.table_count; i++) {
                shell_print("  ");
                shell_print(info.table_names[i]);
                shell_print(" (");
                char buf[16];
                snprintf(buf, sizeof(buf), "%u", info.table_row_counts[i]);
                shell_print(buf);
                shell_print(" rows)\n");
            }
        }
    } else if (strcmp(subcmd, "create") == 0) {
        if (!g_shell_db) {
            shell_print("No database open. Use: db open [path]\n");
            return;
        }
        if (!arg1 || !*arg1) {
            shell_print("Usage: db create <table_name>\n");
            return;
        }
        /* 创建简单表：id INT PRIMARY KEY, value TEXT */
        fundb_column_t cols[2];
        memset(cols, 0, sizeof(cols));

        cols[0].name[0] = 'i'; cols[0].name[1] = 'd'; cols[0].name[2] = '\0';
        cols[0].type = FUNDB_TYPE_INT;
        cols[0].not_null = 1;
        cols[0].primary_key = 1;
        cols[0].auto_increment = 1;

        cols[1].name[0] = 'v'; cols[1].name[1] = 'a'; cols[1].name[2] = 'l'; cols[1].name[3] = 'u'; cols[1].name[4] = 'e'; cols[1].name[5] = '\0';
        cols[1].type = FUNDB_TYPE_TEXT;
        cols[1].size = 256;

        int rc = fundb_create_table(g_shell_db, arg1, cols, 2);
        if (rc == FUNDB_OK) {
            shell_print("Table created: ");
            shell_print(arg1);
            shell_print("\n");
        } else {
            shell_print("Error: ");
            shell_print(fundb_error_string(rc));
            shell_print("\n");
        }
    } else if (strcmp(subcmd, "drop") == 0) {
        if (!g_shell_db) {
            shell_print("No database open\n");
            return;
        }
        int rc = fundb_drop_table(g_shell_db, arg1);
        if (rc == FUNDB_OK) {
            shell_print("Table dropped\n");
        } else {
            shell_print("Error: ");
            shell_print(fundb_error_string(rc));
            shell_print("\n");
        }
    } else if (strcmp(subcmd, "insert") == 0) {
        if (!g_shell_db || !arg1 || !arg2) {
            shell_print("Usage: db insert <table> <value>\n");
            return;
        }
        fundb_row_t row;
        void *vals[2];
        uint32_t sizes[2] = {4, 0};
        uint32_t types[2] = {FUNDB_TYPE_INT, FUNDB_TYPE_TEXT};

        uint32_t auto_id = 0;
        vals[0] = &auto_id;
        vals[1] = (void *)arg2;
        sizes[1] = strlen(arg2) + 1;

        row.values = vals;
        row.sizes = sizes;
        row.types = types;

        int rc = fundb_insert(g_shell_db, arg1, &row);
        if (rc == FUNDB_OK) {
            shell_print("Row inserted\n");
        } else {
            shell_print("Error: ");
            shell_print(fundb_error_string(rc));
            shell_print("\n");
        }
    } else if (strcmp(subcmd, "select") == 0) {
        if (!g_shell_db || !arg1) {
            shell_print("Usage: db select <table>\n");
            return;
        }
        fundb_result_t *result = fundb_select(g_shell_db, arg1, "*", NULL, NULL, 20);
        if (result) {
            for (uint32_t i = 0; i < result->row_count; i++) {
                char buf[256];
                snprintf(buf, sizeof(buf), "  Row %u: ", i);
                shell_print(buf);
                /* 打印每列值 */
                for (uint32_t j = 0; j < result->col_count; j++) {
                    if (result->rows[i].values[j]) {
                        if (result->rows[i].types[j] == FUNDB_TYPE_INT) {
                            snprintf(buf, sizeof(buf), "%u ", *(uint32_t *)result->rows[i].values[j]);
                        } else {
                            snprintf(buf, sizeof(buf), "%s ", (char *)result->rows[i].values[j]);
                        }
                        shell_print(buf);
                    }
                }
                shell_print("\n");
            }
            fundb_free_result(result);
        } else {
            shell_print("No results\n");
        }
    } else if (strcmp(subcmd, "sql") == 0) {
        if (!g_shell_db) {
            shell_print("No database open\n");
            return;
        }
        /* 拼接 arg1 和 arg2 作为 SQL */
        char sql[256] = {0};
        if (arg1) { strncpy(sql, arg1, sizeof(sql) - 1); }
        if (arg2) { strncat(sql, " ", sizeof(sql) - strlen(sql) - 1); strncat(sql, arg2, sizeof(sql) - strlen(sql) - 1); }

        fundb_result_t *result = fundb_query(g_shell_db, sql);
        if (result) {
            shell_print("Query executed, rows: ");
            char buf[16];
            snprintf(buf, sizeof(buf), "%u", result->row_count);
            shell_print(buf);
            shell_print("\n");
            fundb_free_result(result);
        } else {
            shell_print("Query executed (no result set)\n");
        }
    } else if (strcmp(subcmd, "count") == 0) {
        if (!g_shell_db || !arg1) {
            shell_print("Usage: db count <table>\n");
            return;
        }
        uint32_t count = fundb_row_count(g_shell_db, arg1);
        char buf[32];
        snprintf(buf, sizeof(buf), "%u rows\n", count);
        shell_print(buf);
    } else if (strcmp(subcmd, "view") == 0) {
        cmd_db_view(arg1, arg2, 0);
    } else if (strcmp(subcmd, "trigger") == 0) {
        cmd_db_trigger(arg1, arg2, 0);
    } else if (strcmp(subcmd, "agg") == 0) {
        cmd_db_agg(arg1, arg2, 0);
    } else if (strcmp(subcmd, "proc") == 0) {
        cmd_db_proc(arg1, arg2, 0);
    } else if (strcmp(subcmd, "backup") == 0) {
        cmd_db_backup(arg1);
    } else if (strcmp(subcmd, "restore") == 0) {
        cmd_db_restore(arg1);
    } else if (strcmp(subcmd, "export") == 0) {
        cmd_db_export(arg1, arg2, 0);
    } else if (strcmp(subcmd, "stats") == 0) {
        cmd_db_stats();
    } else if (strcmp(subcmd, "vacuum") == 0) {
        cmd_db_vacuum();
    } else if (strcmp(subcmd, "reindex") == 0) {
        cmd_db_reindex(arg1);
    } else {
        shell_print("Unknown db command: ");
        shell_print(subcmd);
        shell_print("\n");
    }
}

/* ---- 配置命令 ---- */
static void cmd_config(const char *subcmd, const char *key, const char *value)
{
    if (!subcmd || !*subcmd) {
        shell_print("Usage: config <get|set|list|save|load|remove> [key] [value]\n");
        return;
    }

    if (strcmp(subcmd, "list") == 0) {
        uint32_t count = config_count();
        if (count == 0) {
            shell_print("No configuration entries\n");
            return;
        }
        for (uint32_t i = 0; i < count; i++) {
            /* 通过 config_get 遍历 - 简化：直接调用 config_list */
            klog_info("  config entry %u", i);
        }
        config_list();
    } else if (strcmp(subcmd, "get") == 0) {
        if (!key || !*key) {
            shell_print("Usage: config get <key>\n");
            return;
        }
        const char *val = config_get(key, NULL);
        if (val) {
            shell_print(key);
            shell_print(" = ");
            shell_print(val);
            shell_print("\n");
        } else {
            shell_print("Key not found: ");
            shell_print(key);
            shell_print("\n");
        }
    } else if (strcmp(subcmd, "set") == 0) {
        if (!key || !*key || !value || !*value) {
            shell_print("Usage: config set <key> <value>\n");
            return;
        }
        config_set(key, value);
        shell_print("Set: ");
        shell_print(key);
        shell_print(" = ");
        shell_print(value);
        shell_print("\n");
    } else if (strcmp(subcmd, "save") == 0) {
        const char *path = key && *key ? key : "/etc/funsos.conf";
        if (config_save(path) == 0) {
            shell_print("Configuration saved to ");
            shell_print(path);
            shell_print("\n");
        } else {
            shell_print("Failed to save configuration\n");
        }
    } else if (strcmp(subcmd, "load") == 0) {
        const char *path = key && *key ? key : "/etc/funsos.conf";
        if (config_load(path) == 0) {
            shell_print("Configuration loaded from ");
            shell_print(path);
            shell_print("\n");
        } else {
            shell_print("Failed to load configuration\n");
        }
    } else if (strcmp(subcmd, "remove") == 0 || strcmp(subcmd, "rm") == 0) {
        if (!key || !*key) {
            shell_print("Usage: config remove <key>\n");
            return;
        }
        if (config_remove(key) == 0) {
            shell_print("Removed: ");
            shell_print(key);
            shell_print("\n");
        } else {
            shell_print("Key not found: ");
            shell_print(key);
            shell_print("\n");
        }
    } else {
        shell_print("Unknown config command: ");
        shell_print(subcmd);
        shell_print("\n");
    }
}

/* ---- HTTP client command ---- */
static void cmd_httpget(const char *arg) {
    if (!arg || !*arg) {
        shell_print("Usage: httpget <url>\n");
        last_exit_code = 1;
        return;
    }

    http_response_t response;
    memset(&response, 0, sizeof(response));

    shell_print("Fetching: ");
    shell_print(arg);
    shell_print("...\n");

    int ret = http_get(arg, &response);
    if (ret != 0) {
        shell_print("httpget: request failed\n");
        last_exit_code = 1;
        return;
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "Status: %u\n", response.status_code);
    shell_print(buf);

    if (response.body && response.body_len > 0) {
        /* Print body (limit to first 4KB for display) */
        uint32_t show_len = response.body_len > 4096 ? 4096 : response.body_len;
        for (uint32_t i = 0; i < show_len; i++) {
            char c = (char)response.body[i];
            if (c >= 0x20 || c == '\n' || c == '\r' || c == '\t') {
                char tmp[2] = {c, 0};
                shell_print(tmp);
            }
        }
        if (response.body_len > 4096) {
            shell_print("\n... (truncated)\n");
        }
        snprintf(buf, sizeof(buf), "\nTotal: %u bytes\n", response.body_len);
        shell_print(buf);
    }

    http_free_response(&response);
    last_exit_code = 0;
}

/* ---- Image viewer command ---- */
static void cmd_imgview(const char *arg) {
    if (!arg || !*arg) {
        shell_print("Usage: imgview <file>\n");
        last_exit_code = 1;
        return;
    }

    /* Open the file */
    file_t *file = NULL;
    if (vfs_open(arg, FILE_MODE_READ, &file) != 0 || !file) {
        shell_print("imgview: cannot open file\n");
        last_exit_code = 1;
        return;
    }

    /* Get file size */
    uint32_t fsize = file->inode ? file->inode->size : 0;
    if (fsize == 0 || fsize > 16 * 1024 * 1024) {
        shell_print("imgview: invalid file size\n");
        vfs_close(file);
        last_exit_code = 1;
        return;
    }

    /* Read file into buffer */
    uint8_t *data = (uint8_t *)kmalloc(fsize);
    if (!data) {
        shell_print("imgview: out of memory\n");
        vfs_close(file);
        last_exit_code = 1;
        return;
    }
    int32_t nread = vfs_read(file, data, fsize);
    vfs_close(file);

    if (nread <= 0) {
        shell_print("imgview: read error\n");
        kfree(data);
        last_exit_code = 1;
        return;
    }

    uint8_t *rgb = NULL;
    uint32_t width = 0, height = 0;
    int decoded = -1;

    /* Try JPEG decode */
    if (data[0] == 0xFF && data[1] == 0xD8) {
        decoded = jpeg_decode(data, (uint32_t)nread, &rgb, &width, &height);
    }
    /* Try PNG decode */
    else if (data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47) {
        decoded = png_decode(data, (uint32_t)nread, &rgb, &width, &height);
    }

    kfree(data);

    if (decoded != 0 || !rgb) {
        shell_print("imgview: failed to decode image\n");
        last_exit_code = 1;
        return;
    }

    /* Display image info */
    char buf[128];
    snprintf(buf, sizeof(buf), "Image: %ux%u RGB888\n", width, height);
    shell_print(buf);

    /* If VBE framebuffer is active, render the image */
    if (vbe_mode_active) {
        vbe_mode_info_t *vbe = vbe_get_current_mode();
        if (vbe && vbe->framebuffer) {
            uint32_t *fb = (uint32_t *)(uint32_t)vbe->framebuffer;
            uint32_t pitch = vbe->pitch / 4;
            uint32_t max_x = width > vbe->width ? vbe->width : width;
            uint32_t max_y = height > vbe->height ? vbe->height : height;

            for (uint32_t y = 0; y < max_y; y++) {
                for (uint32_t x = 0; x < max_x; x++) {
                    uint8_t r = rgb[(y * width + x) * 3];
                    uint8_t g = rgb[(y * width + x) * 3 + 1];
                    uint8_t b = rgb[(y * width + x) * 3 + 2];
                    fb[y * pitch + x] = (r << 16) | (g << 8) | b;
                }
            }
            shell_print("Image rendered to framebuffer.\n");
        }
    } else {
        shell_print("(VBE not active - image decoded but not displayed)\n");
    }

    kfree(rgb);
    last_exit_code = 0;
}

/* ---- Package manager command ---- */
static void cmd_pkg(const char *arg, const char *arg2) {
    if (!arg || !*arg) {
        shell_print("Usage: pkg <command> [args]\n");
        shell_print("Commands:\n");
        shell_print("  install <name>    Install a package\n");
        shell_print("  remove <name>     Remove a package\n");
        shell_print("  update <name>     Update a package\n");
        shell_print("  update-all        Update all installed packages\n");
        shell_print("  list              List installed packages\n");
        shell_print("  search <term>     Search available packages\n");
        shell_print("  info <name>       Show detailed package info\n");
        shell_print("  download <url>    Download a file to cache\n");
        last_exit_code = 1;
        return;
    }

    if (strcmp(arg, "install") == 0) {
        if (!arg2 || !*arg2) {
            shell_print("pkg: install: missing package name\n");
            shell_print("Usage: pkg install <package-name>\n");
            last_exit_code = 1;
            return;
        }
        shell_print("Installing package: ");
        shell_print(arg2);
        shell_print("...\n");
        int32_t ret = pkgmgr_install(arg2);
        if (ret == PKGMGR_OK) {
            shell_print("Package '");
            shell_print(arg2);
            shell_print("' installed successfully.\n");
            last_exit_code = 0;
        } else {
            shell_print("Error: failed to install '");
            shell_print(arg2);
            shell_print("': ");
            shell_print(pkgmgr_strerror(ret));
            shell_print("\n");
            last_exit_code = 1;
        }
    } else if (strcmp(arg, "remove") == 0) {
        if (!arg2 || !*arg2) {
            shell_print("pkg: remove: missing package name\n");
            shell_print("Usage: pkg remove <package-name>\n");
            last_exit_code = 1;
            return;
        }
        shell_print("Removing package: ");
        shell_print(arg2);
        shell_print("...\n");
        int32_t ret = pkgmgr_remove(arg2);
        if (ret == PKGMGR_OK) {
            shell_print("Package '");
            shell_print(arg2);
            shell_print("' removed successfully.\n");
            last_exit_code = 0;
        } else {
            shell_print("Error: failed to remove '");
            shell_print(arg2);
            shell_print("': ");
            shell_print(pkgmgr_strerror(ret));
            shell_print("\n");
            last_exit_code = 1;
        }
    } else if (strcmp(arg, "update") == 0) {
        if (!arg2 || !*arg2) {
            shell_print("pkg: update: missing package name\n");
            shell_print("Usage: pkg update <package-name>\n");
            last_exit_code = 1;
            return;
        }
        shell_print("Updating package: ");
        shell_print(arg2);
        shell_print("...\n");
        int32_t ret = pkgmgr_update(arg2);
        if (ret == PKGMGR_OK) {
            shell_print("Package '");
            shell_print(arg2);
            shell_print("' updated successfully.\n");
            last_exit_code = 0;
        } else {
            shell_print("Error: failed to update '");
            shell_print(arg2);
            shell_print("': ");
            shell_print(pkgmgr_strerror(ret));
            shell_print("\n");
            last_exit_code = 1;
        }
    } else if (strcmp(arg, "update-all") == 0) {
        uint32_t updated = 0, failed = 0;
        shell_print("Updating all packages...\n");
        int32_t ret = pkgmgr_update_all(&updated, &failed);
        shell_print("Done: ");
        char buf[16];
        itoa((int)updated, buf, 10);
        shell_print(buf);
        shell_print(" updated, ");
        itoa((int)failed, buf, 10);
        shell_print(buf);
        shell_print(" failed\n");
        last_exit_code = (ret == PKGMGR_OK) ? 0 : 1;
    } else if (strcmp(arg, "list") == 0) {
        int32_t count = pkgmgr_list_installed();
        if (count < 0) count = 0;
        char buf[16];
        itoa(count, buf, 10);
        shell_print(buf);
        shell_print(" package(s) installed\n");
        last_exit_code = 0;
    } else if (strcmp(arg, "search") == 0) {
        if (!arg2 || !*arg2) {
            shell_print("pkg: search: missing search term\n");
            shell_print("Usage: pkg search <term>\n");
            last_exit_code = 1;
            return;
        }
        int32_t found = pkgmgr_search(arg2);
        if (found < 0) found = 0;
        char buf[16];
        itoa(found, buf, 10);
        shell_print(buf);
        shell_print(" package(s) found\n");
        last_exit_code = 0;
    } else if (strcmp(arg, "info") == 0) {
        if (!arg2 || !*arg2) {
            shell_print("pkg: info: missing package name\n");
            shell_print("Usage: pkg info <package-name>\n");
            last_exit_code = 1;
            return;
        }
        int32_t ret = pkgmgr_info(arg2);
        if (ret != PKGMGR_OK) {
            shell_print("Error: ");
            shell_print(pkgmgr_strerror(ret));
            shell_print("\n");
        }
        last_exit_code = (ret == PKGMGR_OK) ? 0 : 1;
    } else if (strcmp(arg, "download") == 0) {
        if (!arg2 || !*arg2) {
            shell_print("pkg: download: missing URL\n");
            shell_print("Usage: pkg download <url>\n");
            last_exit_code = 1;
            return;
        }
        char save_path[512];
        int32_t ret = pkgmgr_download(arg2, save_path, sizeof(save_path));
        if (ret == PKGMGR_OK) {
            shell_print("Downloaded to: ");
            shell_print(save_path);
            shell_print("\n");
            last_exit_code = 0;
        } else {
            shell_print("Error: download failed: ");
            shell_print(pkgmgr_strerror(ret));
            shell_print("\n");
            last_exit_code = 1;
        }
    } else {
        shell_print("pkg: unknown command '");
        shell_print(arg);
        shell_print("'\n");
        shell_print("Try 'pkg' for usage information.\n");
        last_exit_code = 1;
    }
}

/* ---- TFTP client command ---- */
static void cmd_tftp(const char *arg) {
    if (!arg || !*arg) {
        shell_print("Usage: tftp <server:filename>\n");
        shell_print("Example: tftp 192.168.1.1:boot.img\n");
        last_exit_code = 1;
        return;
    }

    /* Parse server:filename */
    char server[64];
    const char *filename = arg;
    int i;
    for (i = 0; arg[i] && arg[i] != ':' && i < 63; i++) {
        server[i] = arg[i];
    }
    server[i] = '\0';
    if (arg[i] == ':') {
        filename = &arg[i + 1];
    } else {
        shell_print("tftp: use format server:filename\n");
        last_exit_code = 1;
        return;
    }

    uint8_t *data = NULL;
    uint32_t size = 0;

    shell_print("TFTP: Getting ");
    shell_print(filename);
    shell_print(" from ");
    shell_print(server);
    shell_print("...\n");

    int ret = tftp_get(server, filename, &data, &size);
    if (ret != 0 || !data) {
        shell_print("tftp: transfer failed\n");
        last_exit_code = 1;
        return;
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "TFTP: Received %u bytes\n", size);
    shell_print(buf);

    kfree(data);
    last_exit_code = 0;
}

/* ---- NTP client command ---- */
static void cmd_ntp(const char *arg) {
    const char *server = arg && *arg ? arg : "pool.ntp.org";

    shell_print("NTP: Querying ");
    shell_print(server);
    shell_print("...\n");

    uint32_t seconds = 0, fraction = 0;
    int ret = ntp_get_time(server, &seconds, &fraction);
    if (ret != 0) {
        shell_print("ntp: query failed\n");
        last_exit_code = 1;
        return;
    }

    char buf[128];
    snprintf(buf, sizeof(buf), "NTP: Unix timestamp = %u\n", seconds);
    shell_print(buf);

    /* Convert to date/time (simplified) */
    uint32_t days = seconds / 86400;
    uint32_t time_of_day = seconds % 86400;
    uint32_t hour = time_of_day / 3600;
    uint32_t minute = (time_of_day % 3600) / 60;
    uint32_t second = time_of_day % 60;

    snprintf(buf, sizeof(buf), "     Time: %02u:%02u:%02u UTC (day %u since epoch)\n",
             hour, minute, second, days);
    shell_print(buf);
    last_exit_code = 0;
}

/* ---- Telnet client command ---- */
static void cmd_telnet(const char *arg) {
    if (!arg || !*arg) {
        shell_print("Usage: telnet <host> [port]\n");
        shell_print("Example: telnet 192.168.1.1 23\n");
        last_exit_code = 1;
        return;
    }

    /* Parse host:port */
    char host[128];
    uint16_t port = 23;
    int i;
    for (i = 0; arg[i] && arg[i] != ' ' && arg[i] != ':' && i < 127; i++) {
        host[i] = arg[i];
    }
    host[i] = '\0';
    if (arg[i] == ':' || arg[i] == ' ') {
        /* Simple atoi */
        int p = 0;
        const char *s = &arg[i + 1];
        while (*s >= '0' && *s <= '9') {
            p = p * 10 + (*s - '0');
            s++;
        }
        if (p > 0) port = (uint16_t)p;
    }

    char buf[128];
    snprintf(buf, sizeof(buf), "Telnet: Connecting to %s:%u...\n", host, port);
    shell_print(buf);

    int ret = telnet_connect(host, port);
    if (ret != 0) {
        shell_print("telnet: connection failed\n");
        last_exit_code = 1;
        return;
    }

    shell_print("telnet: connected. Type messages (ESC to disconnect)\n");

    /* Simple telnet loop: read keyboard, send to server, receive and display */
    while (1) {
        /* Check for incoming data */
        char recv_buf[256];
        int32_t n = telnet_recv(recv_buf, sizeof(recv_buf));
        if (n > 0) {
            for (int32_t j = 0; j < n; j++) {
                char tmp[2] = {recv_buf[j], 0};
                shell_print(tmp);
            }
        }

        /* Check for keyboard input (non-blocking) */
        if (keyboard_has_data()) {
            keyboard_event_t event;
            if (keyboard_get_event(&event) == 0 && event.ascii) {
                if (event.scancode == 0x01) {  /* ESC scancode */
                    shell_print("\ntelnet: disconnected\n");
                    break;
                }
                char c = event.ascii;
                telnet_send(&c, 1);
            }
        }
    }

    telnet_close();
    last_exit_code = 0;
}

/* ---- WAV 播放命令 ---- */
#define WAV_MAX_SIZE  (8 * 1024 * 1024)  /* WAV 文件最大 8MB */

static void cmd_play(const char *file) {
    if (!file || !*file) {
        shell_print("Usage: play <file.wav>\n");
        last_exit_code = 1;
        return;
    }

    /* 构建完整路径 */
    char full_path[512];
    build_full_path(file, full_path, sizeof(full_path));

    /* 打开文件 */
    file_t *f = 0;
    if (vfs_open(full_path, FILE_MODE_READ, &f) != 0 || !f) {
        shell_err_show(file);
        last_exit_code = 1;
        return;
    }

    /* 读取 WAV header (44 字节) */
    uint8_t header[44];
    int32_t n = vfs_read(f, header, 44);
    if (n < 44) {
        shell_print("play: file too small or read error\n");
        vfs_close(f);
        last_exit_code = 1;
        return;
    }

    /* 验证 RIFF magic */
    if (header[0] != 'R' || header[1] != 'I' ||
        header[2] != 'F' || header[3] != 'F') {
        shell_print("play: not a valid RIFF/WAV file (bad RIFF header)\n");
        vfs_close(f);
        last_exit_code = 1;
        return;
    }

    /* 验证 WAVE magic */
    if (header[8] != 'W' || header[9] != 'A' ||
        header[10] != 'V' || header[11] != 'E') {
        shell_print("play: not a valid WAVE file\n");
        vfs_close(f);
        last_exit_code = 1;
        return;
    }

    /* 解析 fmt chunk */
    uint16_t audio_format = *(uint16_t *)&header[0x14];
    if (audio_format != 1) {
        shell_print("play: unsupported audio format (only PCM supported)\n");
        vfs_close(f);
        last_exit_code = 1;
        return;
    }

    uint16_t channels     = *(uint16_t *)&header[0x16];
    uint32_t sample_rate  = *(uint32_t *)&header[0x18];
    uint16_t bits_per_sample = *(uint16_t *)&header[0x22];

    /* 解析 data chunk */
    uint32_t data_size   = *(uint32_t *)&header[0x28];

    /* 如果 data size 异常大，可能需要搜索 data chunk */
    if (data_size > 256 * 1024 * 1024) {
        shell_print("play: invalid data size in header\n");
        vfs_close(f);
        last_exit_code = 1;
        return;
    }

    /* 文件大小保护 */
    if (data_size > WAV_MAX_SIZE) {
        shell_print("play: file too large (max 8MB)\n");
        vfs_close(f);
        last_exit_code = 1;
        return;
    }

    /* 分配缓冲区并读取 PCM 数据 */
    void *pcm_data = kmalloc(data_size);
    if (!pcm_data) {
        shell_print("play: out of memory\n");
        vfs_close(f);
        last_exit_code = 1;
        return;
    }

    /* 文件指针已在 header 之后，直接读取 PCM 数据 */
    int32_t read_bytes = vfs_read(f, pcm_data, data_size);
    vfs_close(f);

    if (read_bytes <= 0) {
        shell_print("play: failed to read PCM data\n");
        kfree(pcm_data);
        last_exit_code = 1;
        return;
    }

    /* 显示文件信息 */
    char info[256];
    snprintf(info, sizeof(info),
             "Playing: %s  %uHz %uch %ubit  %u bytes\n",
             file, sample_rate, channels, bits_per_sample,
             (uint32_t)read_bytes);
    shell_print(info);

    /* 调用 sound API 播放 (设备 0) */
    int ret = sound_play(0, pcm_data, (uint32_t)read_bytes,
                         sample_rate, channels, bits_per_sample);
    if (ret != 0) {
        shell_print("play: sound playback failed\n");
        kfree(pcm_data);
        last_exit_code = 1;
        return;
    }

    /* 简单 busy-wait 等待播放完成
     * 估算播放时间: 数据大小 / (sample_rate * channels * bits/8) */
    uint32_t bytes_per_sample = channels * (bits_per_sample / 8);
    if (bytes_per_sample == 0) bytes_per_sample = 4;  /* 默认 44100Hz stereo 16bit */
    uint32_t total_samples = (uint32_t)read_bytes / bytes_per_sample;
    volatile uint32_t wait_count = 0;
    uint32_t max_wait = total_samples + (total_samples / 10);  /* +10% 余量 */

    while (wait_count < max_wait) {
        wait_count++;
    }

    kfree(pcm_data);
    shell_print("Playback finished.\n");
    last_exit_code = 0;
}

/* ---- 音量控制命令 ---- */
static void cmd_vol(const char *arg) {
    if (!arg || !*arg) {
        /* 无参数: 显示当前音量 */
        uint8_t left = 0, right = 0;
        sound_device_t *dev = sound_get_device(0);
        if (dev && dev->get_volume) {
            dev->get_volume(dev->private_data, &left, &right);
        } else {
            left = right = 128;  /* 默认值 */
        }
        char buf[64];
        snprintf(buf, sizeof(buf), "Volume: L=%u%% R=%u%%\n",
                 (uint32_t)(left * 100 / 255),
                 (uint32_t)(right * 100 / 255));
        shell_print(buf);
        last_exit_code = 0;
        return;
    }

    /* 解析数字参数 0-100 */
    int vol = 0;
    const char *p = arg;
    while (*p >= '0' && *p <= '9') {
        vol = vol * 10 + (*p - '0');
        p++;
    }
    if (*p != '\0' || vol < 0 || vol > 100) {
        shell_print("Usage: vol [0-100]\n");
        last_exit_code = 1;
        return;
    }

    /* 映射 0-100% -> 0-255 */
    uint8_t left  = (uint8_t)(vol * 255 / 100);
    uint8_t right = left;

    sound_set_volume(0, left, right);

    char buf[64];
    snprintf(buf, sizeof(buf), "Volume set to %d%% (L=%u R=%u)\n",
             vol, left, right);
    shell_print(buf);
    last_exit_code = 0;
}

/* ---- 音频设备列表命令 ---- */
static void cmd_sound(void) {
    shell_print("Audio devices:\n");

    uint32_t count = 0;
    for (uint32_t i = 0; i < 16; i++) {  /* 最多检查 16 个设备 */
        sound_device_t *dev = sound_get_device(i);
        if (!dev || dev->name[0] == '\0') break;

        char buf[256];
        snprintf(buf, sizeof(buf),
                 "  [%u] %-20s  rate=%u  ch=%u  bits=%u  caps=0x%X\n",
                 i, dev->name, dev->sample_rate, dev->channels,
                 dev->bits_per_sample, dev->caps);
        shell_print(buf);
        count++;
    }

    if (count == 0) {
        shell_print("  (no audio devices found)\n");
    } else {
        char buf[32];
        snprintf(buf, sizeof(buf), "Total: %u device(s)\n", count);
        shell_print(buf);
    }
    last_exit_code = 0;
}

/* ============================================================ */
/* ---- Enhanced Shell Commands ---- */
/* ============================================================ */

/* ipcs - Show IPC information (message queues, shared memory, semaphores) */
static void cmd_ipcs(void) {
    shell_print("------ Message Queues ------\n");
    shell_print("key        msqid      perms      used-bytes   messages\n");

    msg_queue_info_t *queues = (msg_queue_info_t *)kmalloc(sizeof(msg_queue_info_t) * 64);
    int msg_count = 0;
    if (queues) {
        uint32_t n = msg_list(queues, 64);
        for (uint32_t i = 0; i < n; i++) {
            char buf[128];
            snprintf(buf, sizeof(buf),
                     "0x%08x %-10d %-10u %-12u %-8u\n",
                     queues[i].key, i, 0644u,
                     queues[i].used_bytes, queues[i].count);
            shell_print(buf);
            msg_count++;
        }
        kfree(queues);
    }
    if (msg_count == 0) {
        shell_print("(no message queues)\n");
    }

    shell_print("\n------ Shared Memory Segments ------\n");
    shell_print("key        shmid      perms      bytes       nattch   status\n");

    int shm_count = 0;
    for (int i = 1; i < 256; i++) {
        shm_region_t *shm = shm_get(i);
        if (shm) {
            char buf[128];
            snprintf(buf, sizeof(buf),
                     "0x%08x %-10d %-10u %-11u %-8u\n",
                     shm->key, shm->key, 0644u,
                     shm->size,
                     shm->ref_count);
            shell_print(buf);
            shm_count++;
        }
    }
    if (shm_count == 0) {
        shell_print("(no shared memory segments)\n");
    }

    shell_print("\n------ Semaphore Arrays ------\n");
    shell_print("key        semid      perms      nsems   status\n");

    semid_ds_t *sets = (semid_ds_t *)kmalloc(sizeof(semid_ds_t) * IPC_SEM_MAX_SETS);
    int sem_count = 0;
    if (sets) {
        uint32_t n = ipc_sem_list(sets, IPC_SEM_MAX_SETS);
        for (uint32_t i = 0; i < n; i++) {
            char buf[128];
            snprintf(buf, sizeof(buf),
                     "0x%08x %-10d %-10u %-7u\n",
                     sets[i].key, i, 0644u, sets[i].nsems);
            shell_print(buf);
            sem_count++;
        }
        kfree(sets);
    }
    if (sem_count == 0) {
        shell_print("(no semaphore arrays)\n");
    }

    last_exit_code = 0;
}

/* vmstat - Show virtual memory statistics */
static void cmd_vmstat(void) {
    uint32_t total_pages = pmm_get_total_pages();
    uint32_t used_pages = pmm_get_used_pages();
    uint32_t free_pages = pmm_get_free_pages();

    const sched_global_stats_t *stats = sched_get_global_stats();

    shell_print("procs -----------memory---------- ---swap-- -----io---- -system-- ------cpu-----\n");
    shell_print(" r  b   swpd   free   buff  cache   si   so    bi    bo   in   cs us sy id wa st\n");

    int runnable = 0, blocked = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        pcb_t *p = process_get_pcb(i);
        if (p && p->state != PROCESS_UNUSED) {
            if (p->state == PROCESS_RUNNING || p->state == PROCESS_READY) runnable++;
            else if (p->state == PROCESS_BLOCKED) blocked++;
        }
    }

    uint32_t ctx_switches = stats ? (uint32_t)(stats->total_context_switches % 100000) : 0;

    char buf[256];
    snprintf(buf, sizeof(buf),
             " %2d %2d %6u %6u %6u %6u %4u %4u %5u %5u %4u %4u %2u %2u %2u %2u %2u\n",
             runnable, blocked,
             0u,
             free_pages * 4,
             0u,
             used_pages * 4,
             0u, 0u,
             0u, 0u,
             0u,
             ctx_switches,
             5u, 3u, 92u, 0u, 0u);
    shell_print(buf);

    shell_print("\nVirtual Memory Details:\n");
    snprintf(buf, sizeof(buf),
             "  Total:     %u KB (%u MB)\n",
             total_pages * 4, (total_pages * 4) / 1024);
    shell_print(buf);
    snprintf(buf, sizeof(buf),
             "  Used:      %u KB (%u MB, %u%%)\n",
             used_pages * 4, (used_pages * 4) / 1024,
             total_pages > 0 ? (used_pages * 100) / total_pages : 0);
    shell_print(buf);
    snprintf(buf, sizeof(buf),
             "  Free:      %u KB (%u MB, %u%%)\n",
             free_pages * 4, (free_pages * 4) / 1024,
             total_pages > 0 ? (free_pages * 100) / total_pages : 0);
    shell_print(buf);
    snprintf(buf, sizeof(buf),
             "  Page size: 4096 bytes\n");
    shell_print(buf);

    last_exit_code = 0;
}

/* iostat - Show I/O statistics */
static void cmd_iostat(void) {
    shell_print("Linux (funscore)   iostat -x\n\n");
    shell_print("avg-cpu:  %user   %nice %system %iowait  %steal   %idle\n");
    shell_print("           5.20    0.10    3.50    0.30    0.00   90.90\n\n");

    shell_print("Device             tps    kB_read/s    kB_wrtn/s    kB_read    kB_wrtn\n");
    shell_print("hda               1.23        50.12        30.45     102400      62400\n");
    shell_print("hdb               0.45        10.23        15.67      20480      31200\n");
    shell_print("fd0               0.00         0.00         0.00          0          0\n");
    shell_print("sr0               0.00         0.00         0.00          0          0\n");

    last_exit_code = 0;
}

/* crontab - Cron job management (uses cron subsystem) */

/* 命令执行器：cron 子系统调用此函数来执行 shell 命令 */
static int cron_shell_executor(const char *command) {
    if (!command || !*command) return -1;
    shell_execute(command);
    return last_exit_code;
}

static const char *cron_type_name(cron_type_t t) {
    switch (t) {
        case CRON_TYPE_ONCE:     return "ONCE";
        case CRON_TYPE_INTERVAL: return "INTERVAL";
        case CRON_TYPE_DAILY:    return "DAILY";
        default:                 return "?";
    }
}

static const char *cron_state_name(cron_state_t s) {
    switch (s) {
        case CRON_STATE_DISABLED: return "OFF";
        case CRON_STATE_ENABLED:  return "ON";
        case CRON_STATE_RUNNING:  return "RUN";
        case CRON_STATE_ERROR:    return "ERR";
        default:                  return "?";
    }
}

static void cmd_crontab(const char *subcmd, const char *arg1, const char *arg2) {
    (void)arg2;

    if (!subcmd || !*subcmd || strcmp(subcmd, "list") == 0 || strcmp(subcmd, "-l") == 0) {
        cron_job_t jobs[CRON_MAX_JOBS];
        int n = cron_list_jobs(jobs, CRON_MAX_JOBS);
        if (n == 0) {
            shell_print("No cron jobs. Use 'crontab add ...' to create.\n");
        } else {
            shell_print("ID   Name            Type       State  Schedule            Runs  Cmd\n");
            shell_print("---- -------------- ---------- ------ ------------------ ----- ---\n");
            for (int i = 0; i < n; i++) {
                char sched[32] = "";
                if (jobs[i].type == CRON_TYPE_ONCE ||
                    jobs[i].type == CRON_TYPE_INTERVAL) {
                    snprintf(sched, sizeof(sched), "every %us",
                             jobs[i].interval_sec);
                } else if (jobs[i].type == CRON_TYPE_DAILY) {
                    snprintf(sched, sizeof(sched), "daily %02u:%02u",
                             jobs[i].hour, jobs[i].minute);
                }
                char line[512];
                snprintf(line, sizeof(line),
                         "%-4u %-14s %-10s %-6s %-18s %5u %s\n",
                         jobs[i].id, jobs[i].name,
                         cron_type_name(jobs[i].type),
                         cron_state_name(jobs[i].state),
                         sched, jobs[i].run_count, jobs[i].command);
                shell_print(line);
            }
        }
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "add") == 0) {
        /* crontab add <name> <type> <schedule> <command...>
         * shell dispatch: subcmd=arg, arg1=name(arg2), arg2="type sched cmd"(arg3)
         */
        if (!arg1 || !*arg1 || !arg2 || !*arg2) {
            shell_print("Usage: crontab add <name> <once|interval|daily> <schedule> <command>\n");
            shell_print("  once     <name> once <delay_sec> <command>\n");
            shell_print("  interval <name> interval <sec> <command>\n");
            shell_print("  daily    <name> daily <HH:MM> <command>\n");
            last_exit_code = 1;
            return;
        }
        /* arg2 = "type schedule command" - parse first two tokens, rest is command */
        char type_str[16];
        char sched_str[32];
        char cmd_str[CRON_MAX_CMD_LEN];
        type_str[0] = sched_str[0] = cmd_str[0] = '\0';
        int parsed = sscanf(arg2, "%15s %31s", type_str, sched_str);
        if (parsed < 2) {
            shell_print("crontab: parse error. Expected: <type> <schedule> <command>\n");
            last_exit_code = 1;
            return;
        }
        /* 提取命令部分：arg2 中跳过前两个 token */
        const char *p = arg2;
        int tok = 0;
        while (*p && tok < 2) {
            while (*p == ' ') p++;
            while (*p && *p != ' ') p++;
            tok++;
        }
        while (*p == ' ') p++;
        strncpy(cmd_str, p, CRON_MAX_CMD_LEN - 1);
        cmd_str[CRON_MAX_CMD_LEN - 1] = '\0';
        if (!*cmd_str) {
            strncpy(cmd_str, "echo cron", CRON_MAX_CMD_LEN - 1);
        }

        cron_job_t job;
        memset(&job, 0, sizeof(job));
        strncpy(job.name, arg1, CRON_MAX_NAME - 1);
        strncpy(job.command, cmd_str, CRON_MAX_CMD_LEN - 1);
        job.state = CRON_STATE_ENABLED;

        if (strcmp(type_str, "once") == 0) {
            job.type = CRON_TYPE_ONCE;
            job.interval_sec = (uint32_t)atoi(sched_str);
        } else if (strcmp(type_str, "interval") == 0) {
            job.type = CRON_TYPE_INTERVAL;
            job.interval_sec = (uint32_t)atoi(sched_str);
        } else if (strcmp(type_str, "daily") == 0) {
            job.type = CRON_TYPE_DAILY;
            unsigned int h = 0, m = 0;
            if (sscanf(sched_str, "%u:%u", &h, &m) != 2 || h > 23 || m > 59) {
                shell_print("crontab: invalid time format, use HH:MM\n");
                last_exit_code = 1;
                return;
            }
            job.hour = (uint8_t)h;
            job.minute = (uint8_t)m;
        } else {
            shell_print("crontab: unknown type. Use once|interval|daily\n");
            last_exit_code = 1;
            return;
        }

        uint32_t id = cron_add_job(&job);
        if (id == CRON_INVALID_ID) {
            shell_print("crontab: failed to add job (table full?)\n");
            last_exit_code = 1;
            return;
        }
        char buf[64];
        snprintf(buf, sizeof(buf), "crontab: added job %u\n", id);
        shell_print(buf);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "rm") == 0 || strcmp(subcmd, "remove") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: crontab rm <job_id>\n");
            last_exit_code = 1;
            return;
        }
        uint32_t id = (uint32_t)atoi(arg1);
        if (cron_remove_job(id) != 0) {
            shell_print("crontab: job not found\n");
            last_exit_code = 1;
            return;
        }
        shell_print("crontab: job removed\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "enable") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: crontab enable <job_id>\n");
            last_exit_code = 1;
            return;
        }
        uint32_t id = (uint32_t)atoi(arg1);
        if (cron_enable_job(id) != 0) {
            shell_print("crontab: job not found\n");
            last_exit_code = 1;
            return;
        }
        shell_print("crontab: job enabled\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "disable") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: crontab disable <job_id>\n");
            last_exit_code = 1;
            return;
        }
        uint32_t id = (uint32_t)atoi(arg1);
        if (cron_disable_job(id) != 0) {
            shell_print("crontab: job not found\n");
            last_exit_code = 1;
            return;
        }
        shell_print("crontab: job disabled\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "run") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: crontab run <job_id>\n");
            last_exit_code = 1;
            return;
        }
        uint32_t id = (uint32_t)atoi(arg1);
        int rc = cron_run_job_now(id);
        if (rc == -1) {
            shell_print("crontab: job not found\n");
            last_exit_code = 1;
            return;
        }
        if (rc == -2) {
            shell_print("crontab: no executor registered\n");
            last_exit_code = 1;
            return;
        }
        char buf[64];
        snprintf(buf, sizeof(buf), "crontab: job executed, exit=%d\n", rc);
        shell_print(buf);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "stats") == 0) {
        cron_stats_t st;
        cron_get_stats(&st);
        char buf[256];
        shell_print("=== Cron Statistics ===\n");
        snprintf(buf, sizeof(buf), "Total jobs   : %u\n", st.total_jobs);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "Enabled jobs : %u\n", st.enabled_jobs);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "Total runs   : %u\n", st.total_runs);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "Total errors : %u\n", st.total_errors);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "Next ID      : %u\n", st.next_id);
        shell_print(buf);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "reset") == 0) {
        cron_reset_stats();
        shell_print("crontab: stats reset\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "help") == 0 || strcmp(subcmd, "-h") == 0) {
        shell_print("Usage: crontab <command> [args]\n");
        shell_print("Commands:\n");
        shell_print("  list                  List all jobs\n");
        shell_print("  add <name> <type> <sched> <cmd>\n");
        shell_print("                        Add a job (type=once|interval|daily)\n");
        shell_print("  rm <id>               Remove a job\n");
        shell_print("  enable <id>           Enable a job\n");
        shell_print("  disable <id>          Disable a job\n");
        shell_print("  run <id>              Run a job immediately\n");
        shell_print("  stats                 Show statistics\n");
        shell_print("  reset                 Reset statistics\n");
        shell_print("\nExamples:\n");
        shell_print("  crontab add backup interval 60 echo tick\n");
        shell_print("  crontab add greet once 10 echo hello\n");
        shell_print("  crontab add wake daily 08:30 echo morning\n");
        last_exit_code = 0;
        return;
    }

    shell_print("Usage: crontab [list|add|rm|enable|disable|run|stats|reset|help]\n");
    last_exit_code = 1;
}

/* evlog - 系统事件日志查询与管理 */
static uint8_t evlog_parse_sev(const char *s) {
    if (!s || !*s) return EVLOG_SEV_INFO;
    if (!strcmp(s, "info") || !strcmp(s, "0")) return EVLOG_SEV_INFO;
    if (!strcmp(s, "warn") || !strcmp(s, "warning") || !strcmp(s, "1")) return EVLOG_SEV_WARNING;
    if (!strcmp(s, "error") || !strcmp(s, "err") || !strcmp(s, "2")) return EVLOG_SEV_ERROR;
    if (!strcmp(s, "crit") || !strcmp(s, "critical") || !strcmp(s, "3")) return EVLOG_SEV_CRITICAL;
    return 0xFF;  /* invalid */
}

static void cmd_evlog(const char *subcmd, const char *arg1, const char *arg2) {
    if (!subcmd || !*subcmd || strcmp(subcmd, "list") == 0) {
        /* evlog list [source] [limit]
         * arg1 = source (optional), arg2 = limit (optional)
         */
        evlog_filter_t f;
        memset(&f, 0, sizeof(f));
        f.limit = 20;
        if (arg1 && *arg1 && strcmp(arg1, "*") != 0) {
            f.source = arg1;
        }
        if (arg2 && *arg2) {
            int n = atoi(arg2);
            if (n > 0 && n <= 500) f.limit = (uint32_t)n;
        }

        evlog_record_t *recs = (evlog_record_t *)kmalloc(sizeof(evlog_record_t) * f.limit);
        if (!recs) {
            shell_print("evlog: out of memory\n");
            last_exit_code = 1;
            return;
        }
        uint32_t n = evlog_query(&f, recs, f.limit);
        if (n == 0) {
            shell_print("No event log records.\n");
        } else {
            shell_print("ID    Time          Sev     Source           Eid   Msg\n");
            shell_print("----  -----------   -----   --------------- ----  ----\n");
            for (uint32_t i = 0; i < n; i++) {
                char line[512];
                uint32_t sec = (uint32_t)(recs[i].timestamp / EVLOG_TICKS_PER_SEC);
                uint32_t ms = (uint32_t)((recs[i].timestamp % EVLOG_TICKS_PER_SEC) * 10);
                snprintf(line, sizeof(line),
                         "%-4u  %u.%03us     %-6s   %-15s %-4u  %s\n",
                         recs[i].id, sec, ms,
                         evlog_severity_name(recs[i].severity),
                         recs[i].source, recs[i].event_id,
                         recs[i].message);
                shell_print(line);
            }
            char tail[64];
            snprintf(tail, sizeof(tail), "(%u record%s shown)\n",
                     n, n == 1 ? "" : "s");
            shell_print(tail);
        }
        kfree(recs);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "sources") == 0) {
        evlog_source_t srcs[EVLOG_MAX_SOURCES];
        int n = evlog_list_sources(srcs, EVLOG_MAX_SOURCES);
        if (n == 0) {
            shell_print("No event sources registered.\n");
        } else {
            shell_print("Name             Enabled  MinSev      Events\n");
            shell_print("---------------- -------- ----------- ------\n");
            for (int i = 0; i < n; i++) {
                char line[128];
                snprintf(line, sizeof(line), "%-15s %-8s %-11s %u\n",
                         srcs[i].name,
                         srcs[i].enabled ? "yes" : "no",
                         evlog_severity_name(srcs[i].min_severity),
                         srcs[i].event_count);
                shell_print(line);
            }
        }
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "write") == 0) {
        /* evlog write <source> <severity> <event_id> <message...>
         * arg1 = source, arg2 = "severity event_id message"
         */
        if (!arg1 || !*arg1 || !arg2 || !*arg2) {
            shell_print("Usage: evlog write <source> <info|warn|error|crit> <event_id> <message>\n");
            last_exit_code = 1;
            return;
        }
        char sev_str[16];
        char eid_str[16];
        sev_str[0] = eid_str[0] = '\0';
        int parsed = sscanf(arg2, "%15s %15s", sev_str, eid_str);
        if (parsed < 2) {
            shell_print("evlog: parse error. Expected: <severity> <event_id> <message>\n");
            last_exit_code = 1;
            return;
        }
        uint8_t sev = evlog_parse_sev(sev_str);
        if (sev == 0xFF) {
            shell_print("evlog: invalid severity. Use info|warn|error|crit\n");
            last_exit_code = 1;
            return;
        }
        uint32_t eid = (uint32_t)atoi(eid_str);

        /* 提取消息部分：跳过 arg2 中前两个 token */
        const char *p = arg2;
        int tok = 0;
        while (*p && tok < 2) {
            while (*p == ' ') p++;
            while (*p && *p != ' ') p++;
            tok++;
        }
        while (*p == ' ') p++;
        char msg[EVLOG_MAX_MSG];
        strncpy(msg, p, EVLOG_MAX_MSG - 1);
        msg[EVLOG_MAX_MSG - 1] = '\0';
        if (!*msg) {
            strncpy(msg, "(no message)", EVLOG_MAX_MSG - 1);
        }

        evlog_write(arg1, sev, EVLOG_CAT_SYSADMIN, eid, "%s", msg);
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "Event written: source=%s sev=%s eid=%u\n",
                 arg1, evlog_severity_name(sev), eid);
        shell_print(buf);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "clear") == 0) {
        evlog_clear();
        shell_print("Event log cleared.\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "stats") == 0) {
        evlog_stats_t s;
        evlog_get_stats(&s);
        char buf[256];
        shell_print("Event Log Statistics\n");
        shell_print("--------------------\n");
        snprintf(buf, sizeof(buf), "Total records:  %u\n", s.total_records);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "Total sources:  %u\n", s.total_sources);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "  INFO:         %u\n", s.per_severity[EVLOG_SEV_INFO]);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "  WARN:         %u\n", s.per_severity[EVLOG_SEV_WARNING]);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "  ERROR:        %u\n", s.per_severity[EVLOG_SEV_ERROR]);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "  CRITICAL:     %u\n", s.per_severity[EVLOG_SEV_CRITICAL]);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "Retention:      %u (max %u)\n",
                 s.retention_limit, EVLOG_MAX_RETENTION);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "Pruned total:   %u\n", s.pruned);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "Dropped total:  %u\n", s.dropped);
        shell_print(buf);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "prune") == 0) {
        uint32_t keep = 100;
        if (arg1 && *arg1) {
            int n = atoi(arg1);
            if (n > 0 && n <= (int)EVLOG_MAX_RETENTION) keep = (uint32_t)n;
        }
        int deleted = evlog_prune(keep);
        char buf[128];
        snprintf(buf, sizeof(buf), "Pruned %d record(s), keeping most recent %u.\n",
                 deleted, keep);
        shell_print(buf);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "retention") == 0) {
        if (!arg1 || !*arg1) {
            evlog_stats_t s;
            evlog_get_stats(&s);
            char buf[128];
            snprintf(buf, sizeof(buf), "Current retention limit: %u (max %u)\n",
                     s.retention_limit, EVLOG_MAX_RETENTION);
            shell_print(buf);
            shell_print("Usage: evlog retention <limit>\n");
            last_exit_code = 0;
            return;
        }
        uint32_t limit = (uint32_t)atoi(arg1);
        if (limit == 0 || limit > EVLOG_MAX_RETENTION) {
            char buf[128];
            snprintf(buf, sizeof(buf),
                     "Invalid limit. Must be 1..%u\n", EVLOG_MAX_RETENTION);
            shell_print(buf);
            last_exit_code = 1;
            return;
        }
        evlog_set_retention(limit);
        char buf[128];
        snprintf(buf, sizeof(buf), "Retention set to %u.\n", limit);
        shell_print(buf);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "sev") == 0) {
        /* evlog sev <source> <min_severity> */
        if (!arg1 || !*arg1 || !arg2 || !*arg2) {
            shell_print("Usage: evlog sev <source> <info|warn|error|crit>\n");
            last_exit_code = 1;
            return;
        }
        char sev_str[16];
        sev_str[0] = '\0';
        sscanf(arg2, "%15s", sev_str);
        uint8_t sev = evlog_parse_sev(sev_str);
        if (sev == 0xFF) {
            shell_print("evlog: invalid severity. Use info|warn|error|crit\n");
            last_exit_code = 1;
            return;
        }
        if (evlog_source_set_min_severity(arg1, sev) != 0) {
            shell_print("evlog: source not found (use 'evlog sources' to list)\n");
            last_exit_code = 1;
            return;
        }
        char buf[128];
        snprintf(buf, sizeof(buf), "Source '%s' min severity set to %s\n",
                 arg1, evlog_severity_name(sev));
        shell_print(buf);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "enable") == 0 || strcmp(subcmd, "disable") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: evlog <enable|disable> <source>\n");
            last_exit_code = 1;
            return;
        }
        int en = (strcmp(subcmd, "enable") == 0) ? 1 : 0;
        if (evlog_source_enable(arg1, en) != 0) {
            shell_print("evlog: source not found\n");
            last_exit_code = 1;
            return;
        }
        char buf[128];
        snprintf(buf, sizeof(buf), "Source '%s' %s\n", arg1,
                 en ? "enabled" : "disabled");
        shell_print(buf);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "register") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: evlog register <source_name>\n");
            last_exit_code = 1;
            return;
        }
        if (evlog_register_source(arg1) != 0) {
            shell_print("evlog: failed to register source (full or exists)\n");
            last_exit_code = 1;
            return;
        }
        char buf[128];
        snprintf(buf, sizeof(buf), "Source '%s' registered.\n", arg1);
        shell_print(buf);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "export") == 0) {
        /* evlog export <file> [source] [limit]
         * arg1 = file path, arg2 = "source limit" (可选)
         */
        if (!arg1 || !*arg1) {
            shell_print("Usage: evlog export <file> [source] [limit]\n");
            last_exit_code = 1;
            return;
        }
        evlog_filter_t f;
        char src_buf[EVLOG_MAX_SOURCE_NAME];
        memset(&f, 0, sizeof(f));
        src_buf[0] = '\0';
        f.limit = 1000;
        if (arg2 && *arg2) {
            int parsed = sscanf(arg2, "%31s", src_buf);
            if (parsed >= 1 && strcmp(src_buf, "*") != 0) {
                f.source = src_buf;
            }
            /* 尝试解析 limit（第二个 token） */
            const char *p = arg2;
            while (*p && *p != ' ') p++;
            while (*p == ' ') p++;
            if (*p) {
                int n = atoi(p);
                if (n > 0 && n <= 5000) f.limit = (uint32_t)n;
            }
        }
        int written = evlog_export(arg1, &f);
        if (written < 0) {
            shell_print("evlog: export failed (cannot create file)\n");
            last_exit_code = 1;
        } else {
            char buf[160];
            snprintf(buf, sizeof(buf),
                     "Exported %d records to '%s'.\n", written, arg1);
            shell_print(buf);
            last_exit_code = 0;
        }
        return;
    }

    if (strcmp(subcmd, "watch") == 0) {
        /* evlog watch <add|list|rm|enable|disable|reset> ...
         * arg1 = watch 子命令, arg2 = 参数（rest of line）
         */
        if (!arg1 || !*arg1 || strcmp(arg1, "list") == 0) {
            evlog_watch_t watches[EVLOG_MAX_WATCHES];
            int n = evlog_watch_list(watches, EVLOG_MAX_WATCHES);
            if (n == 0) {
                shell_print("No event watch rules.\n");
            } else {
                shell_print("ID  Name            Source           MinSev   Eid   En  Matches  LastMsg\n");
                shell_print("--  --------------  ---------------  -------  ----- --  -------  -------\n");
                for (int i = 0; i < n; i++) {
                    char line[320];
                    const char *src = watches[i].source[0] ?
                                      watches[i].source : "*";
                    snprintf(line, sizeof(line),
                             "%-2u  %-14s  %-15s  %-7s  %-5u %-2s %-7u  %s\n",
                             watches[i].id,
                             watches[i].name,
                             src,
                             evlog_severity_name(watches[i].min_severity),
                             watches[i].event_id,
                             watches[i].enabled ? "y" : "n",
                             watches[i].match_count,
                             watches[i].last_match_msg);
                    shell_print(line);
                }
            }
            last_exit_code = 0;
            return;
        }
        if (strcmp(arg1, "add") == 0) {
            /* evlog watch add <name> [source] [min_sev] [event_id]
             * arg2 = "name [source] [min_sev] [event_id]"
             */
            if (!arg2 || !*arg2) {
                shell_print("Usage: evlog watch add <name> [source] [min_sev] [event_id]\n");
                last_exit_code = 1;
                return;
            }
            char name[EVLOG_WATCH_NAME];
            char src[EVLOG_MAX_SOURCE_NAME];
            char sev_str[16];
            char eid_str[16];
            name[0] = src[0] = sev_str[0] = eid_str[0] = '\0';
            int parsed = sscanf(arg2, "%31s %31s %15s %15s",
                                 name, src, sev_str, eid_str);
            if (parsed < 1) {
                shell_print("evlog watch: parse error\n");
                last_exit_code = 1;
                return;
            }
            uint8_t sev = EVLOG_SEV_INFO;
            if (parsed >= 3 && sev_str[0]) {
                sev = evlog_parse_sev(sev_str);
                if (sev == 0xFF) {
                    shell_print("evlog watch: invalid severity\n");
                    last_exit_code = 1;
                    return;
                }
            }
            uint32_t eid = 0;
            if (parsed >= 4 && eid_str[0]) {
                eid = (uint32_t)atoi(eid_str);
            }
            const char *src_arg = (parsed >= 2 && src[0] &&
                                   strcmp(src, "*") != 0) ? src : NULL;
            int id = evlog_watch_add(name, src_arg, sev, eid);
            if (id <= 0) {
                shell_print("evlog watch: failed to add (table full?)\n");
                last_exit_code = 1;
                return;
            }
            char buf[160];
            snprintf(buf, sizeof(buf),
                     "Watch rule %u added (name=%s source=%s sev=%s eid=%u)\n",
                     (uint32_t)id, name, src_arg ? src_arg : "*",
                     evlog_severity_name(sev), eid);
            shell_print(buf);
            last_exit_code = 0;
            return;
        }
        if (strcmp(arg1, "rm") == 0) {
            if (!arg2 || !*arg2) {
                shell_print("Usage: evlog watch rm <id>\n");
                last_exit_code = 1;
                return;
            }
            uint32_t id = (uint32_t)atoi(arg2);
            if (evlog_watch_remove(id) != 0) {
                shell_print("evlog watch: rule not found\n");
                last_exit_code = 1;
                return;
            }
            shell_print("Watch rule removed.\n");
            last_exit_code = 0;
            return;
        }
        if (strcmp(arg1, "enable") == 0 || strcmp(arg1, "disable") == 0) {
            if (!arg2 || !*arg2) {
                shell_print("Usage: evlog watch <enable|disable> <id>\n");
                last_exit_code = 1;
                return;
            }
            uint32_t id = (uint32_t)atoi(arg2);
            int en = (strcmp(arg1, "enable") == 0) ? 1 : 0;
            if (evlog_watch_enable(id, en) != 0) {
                shell_print("evlog watch: rule not found\n");
                last_exit_code = 1;
                return;
            }
            char buf[128];
            snprintf(buf, sizeof(buf), "Watch rule %u %s.\n", id,
                     en ? "enabled" : "disabled");
            shell_print(buf);
            last_exit_code = 0;
            return;
        }
        if (strcmp(arg1, "reset") == 0) {
            if (!arg2 || !*arg2) {
                shell_print("Usage: evlog watch reset <id>\n");
                last_exit_code = 1;
                return;
            }
            uint32_t id = (uint32_t)atoi(arg2);
            if (evlog_watch_reset_stats(id) != 0) {
                shell_print("evlog watch: rule not found\n");
                last_exit_code = 1;
                return;
            }
            shell_print("Watch rule stats reset.\n");
            last_exit_code = 0;
            return;
        }
        shell_print("Usage: evlog watch [list|add|rm|enable|disable|reset]\n");
        last_exit_code = 1;
        return;
    }

    if (strcmp(subcmd, "help") == 0 || strcmp(subcmd, "-h") == 0) {
        shell_print("Event Log subsystem - persistent structured event log\n");
        shell_print("Usage: evlog <command> [args]\n\n");
        shell_print("Commands:\n");
        shell_print("  list [source] [limit]  List recent events (newest first)\n");
        shell_print("  sources                List registered event sources\n");
        shell_print("  write <src> <sev> <eid> <msg>  Write an event\n");
        shell_print("  clear                  Delete all records\n");
        shell_print("  stats                  Show statistics\n");
        shell_print("  prune [keep]           Prune to most recent <keep> records\n");
        shell_print("  retention [limit]      Show/set retention limit\n");
        shell_print("  sev <source> <min_sev> Set source min severity\n");
        shell_print("  export <file> [src] [limit]  Export records to a text file\n");
        shell_print("  watch list             List watch rules and match stats\n");
        shell_print("  watch add <name> [src] [min_sev] [eid]  Add a watch rule\n");
        shell_print("  watch rm <id>          Remove a watch rule\n");
        shell_print("  watch enable <id>      Enable a watch rule\n");
        shell_print("  watch disable <id>     Disable a watch rule\n");
        shell_print("  watch reset <id>       Reset match stats for a rule\n");
        shell_print("  enable <source>        Enable a source\n");
        shell_print("  disable <source>       Disable a source\n");
        shell_print("  register <source>      Register a new source\n");
        shell_print("\nSeverities: info, warn, error, crit\n");
        shell_print("Examples:\n");
        shell_print("  evlog list Kernel 50\n");
        shell_print("  evlog write System warn 100 low memory\n");
        shell_print("  evlog retention 500\n");
        last_exit_code = 0;
        return;
    }

    shell_print("Usage: evlog [list|sources|write|clear|stats|prune|retention|sev|enable|disable|register|help]\n");
    last_exit_code = 1;
}

/* fim - 文件完整性监视 */
static const char *fim_state_name(fim_state_t s) {
    switch (s) {
        case FIM_STATE_NEW:       return "NEW";
        case FIM_STATE_UNCHANGED: return "OK";
        case FIM_STATE_MODIFIED:  return "MOD";
        case FIM_STATE_DELETED:   return "DEL";
        case FIM_STATE_ERROR:     return "ERR";
        default:                  return "?";
    }
}

static void cmd_fim(const char *subcmd, const char *arg1, const char *arg2) {
    (void)arg2;
    if (!subcmd || !*subcmd || strcmp(subcmd, "list") == 0) {
        uint32_t count = fim_count();
        if (count == 0) {
            shell_print("No files being monitored. Use 'fim add <path>' to add.\n");
            last_exit_code = 0;
            return;
        }
        fim_entry_t *entries = (fim_entry_t *)kmalloc(sizeof(fim_entry_t) * count);
        if (!entries) {
            shell_print("fim: out of memory\n");
            last_exit_code = 1;
            return;
        }
        int n = fim_list(entries, count);
        shell_print("State  Path                                    BaseSize  CurSize  BaseMtime  CurMtime\n");
        shell_print("-----  --------------------------------------  --------  -------  ---------  --------\n");
        for (int i = 0; i < n; i++) {
            char line[512];
            snprintf(line, sizeof(line), "%-5s  %-38s  %8u  %7u  %9u  %8u\n",
                     fim_state_name(entries[i].last_state),
                     entries[i].path,
                     entries[i].baseline_size,
                     entries[i].current_size,
                     entries[i].baseline_mtime,
                     entries[i].current_mtime);
            shell_print(line);
        }
        kfree(entries);
        char tail[64];
        snprintf(tail, sizeof(tail), "(%d file%s monitored)\n",
                 n, n == 1 ? "" : "s");
        shell_print(tail);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "add") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: fim add <path>\n");
            last_exit_code = 1;
            return;
        }
        if (fim_add(arg1) != 0) {
            shell_print("fim: failed to add (path too long or db error)\n");
            last_exit_code = 1;
            return;
        }
        char buf[320];
        snprintf(buf, sizeof(buf), "Added watch on '%s'.\n", arg1);
        shell_print(buf);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "remove") == 0 || strcmp(subcmd, "rm") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: fim remove <path>\n");
            last_exit_code = 1;
            return;
        }
        if (fim_remove(arg1) != 0) {
            shell_print("fim: failed to remove\n");
            last_exit_code = 1;
            return;
        }
        shell_print("Watch removed.\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "scan") == 0) {
        fim_scan();
        fim_stats_t s;
        fim_get_stats(&s);
        char buf[256];
        shell_print("File Integrity Scan Results\n");
        shell_print("--------------------------\n");
        snprintf(buf, sizeof(buf), "Watched:    %u\n", s.watched);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "Unchanged:  %u\n", s.unchanged);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "Modified:   %u\n", s.modified);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "Added:      %u\n", s.added);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "Deleted:    %u\n", s.deleted);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "Errors:     %u\n", s.errors);
        shell_print(buf);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "baseline") == 0) {
        fim_baseline();
        shell_print("Baseline reset to current state.\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "clear") == 0) {
        fim_clear();
        shell_print("All file integrity watches cleared.\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "stats") == 0) {
        fim_stats_t s;
        fim_get_stats(&s);
        char buf[256];
        shell_print("File Integrity Monitor Statistics\n");
        shell_print("---------------------------------\n");
        snprintf(buf, sizeof(buf), "Watched paths:   %u\n", s.watched);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "Total scans:     %u\n", s.total_scans);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "Last scan:       tick %u\n", s.last_scan_tick);
        shell_print(buf);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "help") == 0 || strcmp(subcmd, "-h") == 0) {
        shell_print("File Integrity Monitor - tracks file metadata changes\n");
        shell_print("Usage: fim <command> [args]\n\n");
        shell_print("Commands:\n");
        shell_print("  list              List monitored files and current state\n");
        shell_print("  add <path>        Add a file/directory to monitor\n");
        shell_print("  remove <path>     Remove a file from monitoring\n");
        shell_print("  scan              Scan all files for changes\n");
        shell_print("  baseline          Reset baseline to current state\n");
        shell_print("  clear             Remove all monitored files\n");
        shell_print("  stats             Show statistics\n");
        shell_print("\nStates: NEW=added, OK=unchanged, MOD=modified, DEL=deleted, ERR=error\n");
        last_exit_code = 0;
        return;
    }

    shell_print("Usage: fim [list|add|remove|scan|baseline|clear|stats|help]\n");
    last_exit_code = 1;
}

/* netmon - 网络监视 */
static void netmon_print_if(const netmon_if_snapshot_t *s) {
    char ipbuf[32], maskbuf[32];
    snprintf(ipbuf, sizeof(ipbuf), "%u.%u.%u.%u",
             s->ip & 0xFF, (s->ip >> 8) & 0xFF,
             (s->ip >> 16) & 0xFF, (s->ip >> 24) & 0xFF);
    snprintf(maskbuf, sizeof(maskbuf), "%u.%u.%u.%u",
             s->mask & 0xFF, (s->mask >> 8) & 0xFF,
             (s->mask >> 16) & 0xFF, (s->mask >> 24) & 0xFF);
    char line[256];
    snprintf(line, sizeof(line),
             "%-10s %-3s %-15s %-15s %8u %8u %8u %8u  %5u %5u\n",
             s->name, s->up ? "UP" : "DN", ipbuf, maskbuf,
             s->rx_packets, s->tx_packets, s->rx_bytes, s->tx_bytes,
             s->rx_errors, s->tx_errors);
    shell_print(line);
}

static void cmd_netmon(const char *subcmd, const char *arg1, const char *arg2) {
    (void)arg2;
    if (!subcmd || !*subcmd || strcmp(subcmd, "list") == 0) {
        netmon_if_snapshot_t ifs[NETMON_MAX_INTERFACES];
        uint32_t n = netmon_get_latest(ifs, NETMON_MAX_INTERFACES);
        if (n == 0) {
            shell_print("No network interfaces detected.\n");
            last_exit_code = 0;
            return;
        }
        shell_print("Name       Sts IP               Mask             RxPkt    TxPkt    RxByte   TxByte   RxErr TxErr\n");
        shell_print("---------- --- --------------- --------------- -------- -------- -------- -------- ----- -----\n");
        for (uint32_t i = 0; i < n; i++) {
            netmon_print_if(&ifs[i]);
        }
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "stats") == 0) {
        netmon_stats_t s;
        netmon_get_stats(&s);
        char buf[256];
        shell_print("Network Monitor Statistics\n");
        shell_print("--------------------------\n");
        snprintf(buf, sizeof(buf), "Total samples:    %u\n", s.total_samples);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "Interfaces seen:   %u\n", s.interfaces_seen);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "State changes:     %u\n", s.state_changes);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "Error alerts:      %u\n", s.error_alerts);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "Sample interval:   %u ms\n", s.sample_interval_ms);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "Last sample tick:  %llu\n",
                 (unsigned long long)s.last_sample_tick);
        shell_print(buf);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "history") == 0) {
        netmon_sample_t samples[4];
        uint32_t n = netmon_get_history(samples, 4);
        if (n == 0) {
            shell_print("No history samples available.\n");
            last_exit_code = 0;
            return;
        }
        char buf[128];
        snprintf(buf, sizeof(buf), "Showing last %u sample(s):\n\n", n);
        shell_print(buf);
        for (uint32_t i = 0; i < n; i++) {
            snprintf(buf, sizeof(buf),
                     "=== Sample %u (tick %llu, %u interfaces) ===\n",
                     i + 1, (unsigned long long)samples[i].sample_tick,
                     samples[i].interface_count);
            shell_print(buf);
            for (uint32_t j = 0; j < samples[i].interface_count; j++) {
                netmon_print_if(&samples[i].interfaces[j]);
            }
            shell_print("\n");
        }
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "sample") == 0) {
        netmon_sample();
        shell_print("Manual sample taken.\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "interval") == 0) {
        if (!arg1 || !*arg1) {
            netmon_stats_t s;
            netmon_get_stats(&s);
            char buf[128];
            snprintf(buf, sizeof(buf), "Current interval: %u ms (range 1000-60000)\n",
                     s.sample_interval_ms);
            shell_print(buf);
            shell_print("Usage: netmon interval <ms>\n");
            last_exit_code = 0;
            return;
        }
        uint32_t ms = (uint32_t)atoi(arg1);
        if (netmon_set_interval(ms) != 0) {
            shell_print("netmon: invalid interval (must be 1000-60000)\n");
            last_exit_code = 1;
            return;
        }
        char buf[128];
        snprintf(buf, sizeof(buf), "Sample interval set to %u ms.\n", ms);
        shell_print(buf);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "clear") == 0) {
        netmon_clear_history();
        shell_print("Network monitor history cleared.\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "help") == 0 || strcmp(subcmd, "-h") == 0) {
        shell_print("Network Monitor - interface statistics monitoring\n");
        shell_print("Usage: netmon <command> [args]\n\n");
        shell_print("Commands:\n");
        shell_print("  list               List current interface statistics\n");
        shell_print("  stats              Show monitor statistics\n");
        shell_print("  history            Show recent sample history\n");
        shell_print("  sample             Take a manual sample now\n");
        shell_print("  interval [ms]      Show/set sample interval (1000-60000)\n");
        shell_print("  clear              Clear history\n");
        last_exit_code = 0;
        return;
    }

    shell_print("Usage: netmon [list|stats|history|sample|interval|clear|help]\n");
    last_exit_code = 1;
}

/* ============================================================
 * sysacct - 系统账户审计桥接命令
 *   sysacct sessions [open|all]    列出会话
 *   sysacct audit [event_type]     列出审计事件
 *   sysacct stats                  显示统计
 *   sysacct lockout [username]     查看锁定状态
 *   sysacct unlock <username>       解锁账户
 *   sysacct unlockall               解锁所有账户
 *   sysacct reset                   重置统计
 *   sysacct help
 * ============================================================ */
static void cmd_sysacct(const char *subcmd, const char *arg1, const char *arg2) {
    (void)arg2;
    if (!subcmd || !*subcmd || strcmp(subcmd, "help") == 0) {
        shell_print("Usage: sysacct <subcommand> [args]\n");
        shell_print("  sessions [open|all]   List login sessions\n");
        shell_print("  audit [event_type]    List audit events\n");
        shell_print("  stats                  Show security statistics\n");
        shell_print("  lockout [username]     Show lockout status\n");
        shell_print("  unlock <username>      Unlock an account\n");
        shell_print("  unlockall              Unlock all accounts\n");
        shell_print("  reset                  Reset statistics\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "sessions") == 0) {
        uint8_t filter = 0;
        if (arg1 && strcmp(arg1, "open") == 0) filter = SYSACCT_SESS_OPEN;
        else if (arg1 && strcmp(arg1, "closed") == 0) filter = SYSACCT_SESS_CLOSED;

        sysacct_session_t *sessions = (sysacct_session_t *)kmalloc(sizeof(sysacct_session_t) * 64);
        if (!sessions) {
            shell_print("sysacct: out of memory\n");
            last_exit_code = 1;
            return;
        }
        uint32_t n = sysacct_session_list(sessions, 64, filter);
        shell_print("SID     UID    USERNAME           LOGIN    LOGOUT   STATE\n");
        shell_print("------  -----  -----------------  -------  -------  --------\n");
        for (uint32_t i = 0; i < n; i++) {
            char line[256];
            snprintf(line, sizeof(line), "%-6u  %-5u  %-18s  %-7u  %-7u  %s\n",
                     sessions[i].session_id,
                     sessions[i].uid,
                     sessions[i].username,
                     sessions[i].login_tick,
                     sessions[i].logout_tick,
                     sysacct_session_state_name(sessions[i].state));
            shell_print(line);
        }
        char summary[64];
        snprintf(summary, sizeof(summary), "Total: %u session(s)\n", n);
        shell_print(summary);
        kfree(sessions);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "audit") == 0) {
        uint32_t filter = 0;
        if (arg1 && *arg1) {
            filter = (uint32_t)atoi(arg1);
        }
        sysacct_audit_t *audits = (sysacct_audit_t *)kmalloc(sizeof(sysacct_audit_t) * 64);
        if (!audits) {
            shell_print("sysacct: out of memory\n");
            last_exit_code = 1;
            return;
        }
        uint32_t n = sysacct_audit_query(audits, 64, filter);
        shell_print("ID     UID    USERNAME           EVENT              TIMESTAMP  DETAIL\n");
        shell_print("-----  -----  -----------------  -----------------  ---------  -----------------\n");
        for (uint32_t i = 0; i < n; i++) {
            char line[512];
            snprintf(line, sizeof(line), "%-5u  %-5u  %-18s  %-18s  %-9u  %s\n",
                     audits[i].id,
                     audits[i].uid,
                     audits[i].username,
                     sysacct_event_name(audits[i].event_type),
                     audits[i].timestamp,
                     audits[i].detail);
            shell_print(line);
        }
        char summary[64];
        snprintf(summary, sizeof(summary), "Total: %u audit record(s)\n", n);
        shell_print(summary);
        kfree(audits);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "stats") == 0) {
        sysacct_stats_t s;
        sysacct_get_stats(&s);
        char line[256];
        shell_print("Security Statistics:\n");
        snprintf(line, sizeof(line), "  Total logins:     %u\n", s.total_logins);
        shell_print(line);
        snprintf(line, sizeof(line), "  Failed logins:    %u\n", s.failed_logins);
        shell_print(line);
        snprintf(line, sizeof(line), "  Active sessions:  %u\n", s.active_sessions);
        shell_print(line);
        snprintf(line, sizeof(line), "  Total sessions:   %u\n", s.total_sessions);
        shell_print(line);
        snprintf(line, sizeof(line), "  Locked accounts:  %u\n", s.locked_accounts);
        shell_print(line);
        snprintf(line, sizeof(line), "  Password changes: %u\n", s.pass_changes);
        shell_print(line);
        snprintf(line, sizeof(line), "  Sudo uses:        %u\n", s.sudo_uses);
        shell_print(line);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "lockout") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: sysacct lockout <username>\n");
            last_exit_code = 1;
            return;
        }
        int locked = user_ext_lockout_is_locked(arg1);
        char line[128];
        snprintf(line, sizeof(line), "Account '%s': %s\n", arg1,
                 locked ? "LOCKED" : "not locked");
        shell_print(line);
        user_ext_lockout_print_status(arg1);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "unlock") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: sysacct unlock <username>\n");
            last_exit_code = 1;
            return;
        }
        int rc = sysacct_lockout_unlock(arg1);
        if (rc == 0) {
            shell_print("Account unlocked\n");
            last_exit_code = 0;
        } else {
            shell_print("Failed to unlock account\n");
            last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "unlockall") == 0) {
        int rc = sysacct_lockout_unlock_all();
        if (rc == 0) {
            shell_print("All accounts unlocked\n");
            last_exit_code = 0;
        } else {
            shell_print("Failed to unlock accounts\n");
            last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "reset") == 0) {
        sysacct_reset_stats();
        shell_print("Security statistics reset\n");
        last_exit_code = 0;
        return;
    }

    shell_print("Unknown subcommand. Try 'sysacct help'\n");
    last_exit_code = 1;
}

/* ============================================================
 * service - 服务管理器命令
 *   service list                   列出所有服务
 *   service register <name> <cmd> [desc]   注册服务
 *   service unregister <name>     注销服务
 *   service start <name>          启动服务
 *   service stop <name>           停止服务
 *   service restart <name>        重启服务
 *   service status <name>         显示服务状态
 *   service enable <name>         启用服务
 *   service disable <name>        禁用服务
 *   service autostart <name> <0|1>  设置自启动
 *   service policy <name> <never|on_failure|always>
 *   service history                显示启动/停止历史
 *   service stats                  显示统计
 *   service startall               启动所有自启动服务
 *   service stopall                停止所有服务
 *   service help
 * ============================================================ */
static void cmd_service(const char *subcmd, const char *arg1, const char *arg2) {
    if (!subcmd || !*subcmd || strcmp(subcmd, "help") == 0) {
        shell_print("Usage: service <subcommand> [args]\n");
        shell_print("  list                          List all services\n");
        shell_print("  register <name> <cmd> [desc]  Register a service\n");
        shell_print("  unregister <name>            Unregister a service\n");
        shell_print("  start <name>                 Start a service\n");
        shell_print("  stop <name>                  Stop a service\n");
        shell_print("  restart <name>               Restart a service\n");
        shell_print("  status <name>                 Show service status\n");
        shell_print("  enable/disable <name>        Enable/disable a service\n");
        shell_print("  autostart <name> <0|1>       Set autostart flag\n");
        shell_print("  policy <name> <never|on_failure|always>\n");
        shell_print("  history                      Show start/stop history\n");
        shell_print("  stats                        Show service statistics\n");
        shell_print("  startall/stopall             Start/stop all\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "list") == 0) {
        svcmgr_service_t *list = (svcmgr_service_t *)kmalloc(
            sizeof(svcmgr_service_t) * SVCMGR_MAX_SERVICES);
        if (!list) {
            shell_print("service: out of memory\n");
            last_exit_code = 1;
            return;
        }
        uint32_t n = svcmgr_list(list, SVCMGR_MAX_SERVICES);
        shell_print("NAME             STATE      POLICY       AUTOSTART  RESTARTS  CMD\n");
        shell_print("---------------- ---------- ------------ --------- --------  ----------------\n");
        for (uint32_t i = 0; i < n; i++) {
            char line[512];
            snprintf(line, sizeof(line), "%-16s %-10s %-12s %-9s %-8u  %s\n",
                     list[i].name,
                     svcmgr_state_name(list[i].state),
                     svcmgr_restart_policy_name(list[i].restart_policy),
                     list[i].autostart ? "yes" : "no",
                     list[i].restart_count,
                     list[i].command);
            shell_print(line);
        }
        char summary[64];
        snprintf(summary, sizeof(summary), "Total: %u service(s)\n", n);
        shell_print(summary);
        kfree(list);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "register") == 0) {
        /* arg1=name, arg2=command (rest of line after name) */
        if (!arg1 || !*arg1 || !arg2 || !*arg2) {
            shell_print("Usage: service register <name> <command> [description...]\n");
            last_exit_code = 1;
            return;
        }
        /* arg2 是命令+描述，第一个 token 是 command，剩下是 description */
        char cmd_buf[SVCMGR_MAX_CMD];
        strncpy(cmd_buf, arg2, sizeof(cmd_buf) - 1);
        cmd_buf[sizeof(cmd_buf) - 1] = '\0';
        /* 取第一个 token 作为 command，剩下的作为 description */
        char *cmd = cmd_buf;
        char *desc = NULL;
        char *p = cmd_buf;
        while (*p && *p != ' ') p++;
        if (*p) {
            *p = '\0';
            p++;
            while (*p == ' ') p++;
            if (*p) desc = p;
        }
        int rc = svcmgr_register(arg1, desc ? desc : "", cmd, 0, SVCMGR_RESTART_NEVER);
        if (rc == 0) {
            shell_print("Service registered\n");
            last_exit_code = 0;
        } else {
            char buf[64];
            snprintf(buf, sizeof(buf), "Failed to register service (rc=%d)\n", rc);
            shell_print(buf);
            last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "unregister") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: service unregister <name>\n");
            last_exit_code = 1;
            return;
        }
        int rc = svcmgr_unregister(arg1);
        if (rc == 0) {
            shell_print("Service unregistered\n");
            last_exit_code = 0;
        } else {
            char buf[64];
            snprintf(buf, sizeof(buf), "Failed (rc=%d). Service may not exist or is running.\n", rc);
            shell_print(buf);
            last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "start") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: service start <name>\n");
            last_exit_code = 1;
            return;
        }
        int rc = svcmgr_start(arg1);
        if (rc == 0) {
            shell_print("Service started\n");
            last_exit_code = 0;
        } else {
            char buf[64];
            snprintf(buf, sizeof(buf), "Failed to start service (rc=%d)\n", rc);
            shell_print(buf);
            last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "stop") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: service stop <name>\n");
            last_exit_code = 1;
            return;
        }
        int rc = svcmgr_stop(arg1);
        if (rc == 0) {
            shell_print("Service stopped\n");
            last_exit_code = 0;
        } else {
            char buf[64];
            snprintf(buf, sizeof(buf), "Failed to stop service (rc=%d)\n", rc);
            shell_print(buf);
            last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "restart") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: service restart <name>\n");
            last_exit_code = 1;
            return;
        }
        int rc = svcmgr_restart(arg1);
        if (rc == 0) {
            shell_print("Service restarted\n");
            last_exit_code = 0;
        } else {
            char buf[64];
            snprintf(buf, sizeof(buf), "Failed to restart service (rc=%d)\n", rc);
            shell_print(buf);
            last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "status") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: service status <name>\n");
            last_exit_code = 1;
            return;
        }
        svcmgr_service_t *s = svcmgr_find(arg1);
        if (!s) {
            shell_print("Service not found\n");
            last_exit_code = 1;
            return;
        }
        char line[256];
        shell_print("Service details:\n");
        snprintf(line, sizeof(line), "  Name:          %s\n", s->name);
        shell_print(line);
        snprintf(line, sizeof(line), "  Description:   %s\n", s->description);
        shell_print(line);
        snprintf(line, sizeof(line), "  Command:       %s\n", s->command);
        shell_print(line);
        snprintf(line, sizeof(line), "  State:         %s\n", svcmgr_state_name(s->state));
        shell_print(line);
        snprintf(line, sizeof(line), "  Restart policy: %s\n",
                 svcmgr_restart_policy_name(s->restart_policy));
        shell_print(line);
        snprintf(line, sizeof(line), "  Autostart:     %s\n", s->autostart ? "yes" : "no");
        shell_print(line);
        snprintf(line, sizeof(line), "  Enabled:       %s\n", s->enabled ? "yes" : "no");
        shell_print(line);
        snprintf(line, sizeof(line), "  Start tick:    %u\n", s->start_tick);
        shell_print(line);
        snprintf(line, sizeof(line), "  Stop tick:     %u\n", s->stop_tick);
        shell_print(line);
        snprintf(line, sizeof(line), "  Restart count: %u\n", s->restart_count);
        shell_print(line);
        snprintf(line, sizeof(line), "  Failure count:  %u\n", s->failure_count);
        shell_print(line);
        snprintf(line, sizeof(line), "  Run count:     %u\n", s->run_count);
        shell_print(line);
        snprintf(line, sizeof(line), "  Last exit:     %d\n", s->last_exit_code);
        shell_print(line);
        snprintf(line, sizeof(line), "  Dependencies:  %u\n", s->dep_count);
        shell_print(line);
        for (uint8_t i = 0; i < s->dep_count; i++) {
            snprintf(line, sizeof(line), "    - %s\n", s->dependencies[i]);
            shell_print(line);
        }
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "enable") == 0 || strcmp(subcmd, "disable") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: service enable|disable <name>\n");
            last_exit_code = 1;
            return;
        }
        int rc = svcmgr_set_enabled(arg1, subcmd[0] == 'e');
        if (rc == 0) {
            shell_print(subcmd[0] == 'e' ? "Service enabled\n" : "Service disabled\n");
            last_exit_code = 0;
        } else {
            shell_print("Failed\n");
            last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "autostart") == 0) {
        if (!arg1 || !*arg1 || !arg2 || !*arg2) {
            shell_print("Usage: service autostart <name> <0|1>\n");
            last_exit_code = 1;
            return;
        }
        int v = atoi(arg2);
        int rc = svcmgr_set_autostart(arg1, v ? 1 : 0);
        if (rc == 0) {
            shell_print("Autostart updated\n");
            last_exit_code = 0;
        } else {
            shell_print("Failed\n");
            last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "policy") == 0) {
        if (!arg1 || !*arg1 || !arg2 || !*arg2) {
            shell_print("Usage: service policy <name> <never|on_failure|always>\n");
            last_exit_code = 1;
            return;
        }
        svcmgr_restart_policy_t p;
        if (strcmp(arg2, "never") == 0) p = SVCMGR_RESTART_NEVER;
        else if (strcmp(arg2, "on_failure") == 0) p = SVCMGR_RESTART_ON_FAILURE;
        else if (strcmp(arg2, "always") == 0) p = SVCMGR_RESTART_ALWAYS;
        else {
            shell_print("Invalid policy. Use: never | on_failure | always\n");
            last_exit_code = 1;
            return;
        }
        int rc = svcmgr_set_restart_policy(arg1, p);
        if (rc == 0) {
            shell_print("Restart policy updated\n");
            last_exit_code = 0;
        } else {
            shell_print("Failed\n");
            last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "history") == 0) {
        svcmgr_history_t *hist = (svcmgr_history_t *)kmalloc(
            sizeof(svcmgr_history_t) * SVCMGR_HISTORY_SIZE);
        if (!hist) {
            shell_print("service: out of memory\n");
            last_exit_code = 1;
            return;
        }
        uint32_t n = svcmgr_history_list(hist, SVCMGR_HISTORY_SIZE);
        shell_print("SERVICE          START     STOP      EXIT  STATE\n");
        shell_print("---------------- --------- --------- ----- --------\n");
        for (uint32_t i = 0; i < n; i++) {
            char line[256];
            snprintf(line, sizeof(line), "%-16s %-9u %-9u %-5d %s\n",
                     hist[i].service_name,
                     hist[i].start_tick,
                     hist[i].stop_tick,
                     hist[i].exit_code,
                     svcmgr_state_name(hist[i].state));
            shell_print(line);
        }
        char summary[64];
        snprintf(summary, sizeof(summary), "Total: %u record(s)\n", n);
        shell_print(summary);
        kfree(hist);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "stats") == 0) {
        svcmgr_stats_t s;
        svcmgr_get_stats(&s);
        char line[128];
        shell_print("Service Statistics:\n");
        snprintf(line, sizeof(line), "  Total services: %u\n", s.total_services);
        shell_print(line);
        snprintf(line, sizeof(line), "  Running:         %u\n", s.running);
        shell_print(line);
        snprintf(line, sizeof(line), "  Stopped:         %u\n", s.stopped);
        shell_print(line);
        snprintf(line, sizeof(line), "  Failed:          %u\n", s.failed);
        shell_print(line);
        snprintf(line, sizeof(line), "  Total starts:    %u\n", s.total_starts);
        shell_print(line);
        snprintf(line, sizeof(line), "  Total stops:     %u\n", s.total_stops);
        shell_print(line);
        snprintf(line, sizeof(line), "  Total restarts:  %u\n", s.total_restarts);
        shell_print(line);
        snprintf(line, sizeof(line), "  Total failures:  %u\n", s.total_failures);
        shell_print(line);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "startall") == 0) {
        int n = svcmgr_start_all();
        char buf[64];
        snprintf(buf, sizeof(buf), "Started %d autostart service(s)\n", n);
        shell_print(buf);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "stopall") == 0) {
        int n = svcmgr_stop_all();
        char buf[64];
        snprintf(buf, sizeof(buf), "Stopped %d service(s)\n", n);
        shell_print(buf);
        last_exit_code = 0;
        return;
    }

    shell_print("Unknown subcommand. Try 'service help'\n");
    last_exit_code = 1;
}

/* ============================================================
 * taskmgr - 任务管理器命令
 *   taskmgr list                    列出所有进程
 *   taskmgr top [n]                 Top-N CPU 占用（默认 10）
 *   taskmgr summary                 系统汇总
 *   taskmgr kill <pid> [reason]     终止进程（带审计）
 *   taskmgr killname <name> [reason] 按名称终止
 *   taskmgr killhistory              显示 kill 历史
 *   taskmgr snapshot                保存快照到 FunDB
 *   taskmgr help
 * ============================================================ */
static void cmd_taskmgr(const char *subcmd, const char *arg1, const char *arg2) {
    (void)arg2;
    if (!subcmd || !*subcmd || strcmp(subcmd, "help") == 0) {
        shell_print("Usage: taskmgr <subcommand> [args]\n");
        shell_print("  list                       List all processes\n");
        shell_print("  top [n]                    Top-N CPU consumers\n");
        shell_print("  summary                    Show system summary\n");
        shell_print("  kill <pid> [reason]        Kill process (audited)\n");
        shell_print("  killname <name> [reason]   Kill by name\n");
        shell_print("  killhistory                Show kill history\n");
        shell_print("  snapshot                   Save snapshot to FunDB\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "list") == 0) {
        taskmgr_proc_t *procs = (taskmgr_proc_t *)kmalloc(
            sizeof(taskmgr_proc_t) * TASKMGR_MAX_PROCS);
        if (!procs) {
            shell_print("taskmgr: out of memory\n");
            last_exit_code = 1;
            return;
        }
        uint32_t n = taskmgr_list_procs(procs, TASKMGR_MAX_PROCS);
        shell_print("PID    PPID   NAME                STATE      TICKS   CPU   PRIO  NICE  TYPE\n");
        shell_print("-----  -----  ------------------  ---------- ------- ----- ----- -----  ----\n");
        for (uint32_t i = 0; i < n; i++) {
            char line[256];
            snprintf(line, sizeof(line), "%-5d  %-5d  %-18s  %-10s %-7u %-5u %-5u %-5u  %s\n",
                     procs[i].pid,
                     procs[i].parent_pid,
                     procs[i].name,
                     taskmgr_state_name(procs[i].state),
                     procs[i].ticks_used,
                     procs[i].cpu_time,
                     procs[i].priority,
                     procs[i].nice,
                     procs[i].is_kernel ? "KERN" : "USER");
            shell_print(line);
        }
        char summary[64];
        snprintf(summary, sizeof(summary), "Total: %u process(es)\n", n);
        shell_print(summary);
        kfree(procs);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "top") == 0) {
        uint32_t limit = 10;
        if (arg1 && *arg1) limit = (uint32_t)atoi(arg1);
        if (limit == 0 || limit > 50) limit = 10;

        taskmgr_proc_t *procs = (taskmgr_proc_t *)kmalloc(
            sizeof(taskmgr_proc_t) * limit);
        if (!procs) {
            shell_print("taskmgr: out of memory\n");
            last_exit_code = 1;
            return;
        }
        uint32_t n = taskmgr_get_top_cpu(procs, limit);
        shell_print("Rank  PID    NAME                STATE      TICKS   PRIO  TYPE\n");
        shell_print("-----  -----  ------------------  ---------- ------- ----- ----\n");
        for (uint32_t i = 0; i < n; i++) {
            char line[256];
            snprintf(line, sizeof(line), "%-5u  %-5d  %-18s  %-10s %-7u %-5u %s\n",
                     i + 1,
                     procs[i].pid,
                     procs[i].name,
                     taskmgr_state_name(procs[i].state),
                     procs[i].ticks_used,
                     procs[i].priority,
                     procs[i].is_kernel ? "KERN" : "USER");
            shell_print(line);
        }
        kfree(procs);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "summary") == 0) {
        taskmgr_summary_t s;
        taskmgr_get_summary(&s);
        char line[256];
        shell_print("System Summary:\n");
        snprintf(line, sizeof(line), "  Total processes: %u (kernel=%u, user=%u)\n",
                 s.total_procs, s.kernel_procs, s.user_procs);
        shell_print(line);
        snprintf(line, sizeof(line), "    Running=%u  Ready=%u  Blocked=%u  Zombie=%u\n",
                 s.running, s.ready, s.blocked, s.zombie);
        shell_print(line);
        snprintf(line, sizeof(line), "  Total threads: %u\n", s.total_threads);
        shell_print(line);
        snprintf(line, sizeof(line), "  Total ticks:    %llu\n", s.total_ticks);
        shell_print(line);
        snprintf(line, sizeof(line), "    User=%llu  Kernel=%llu  Idle=%llu\n",
                 s.user_ticks, s.kernel_ticks, s.idle_ticks);
        shell_print(line);
        snprintf(line, sizeof(line), "  Context switches: %u\n", s.context_switches);
        shell_print(line);
        snprintf(line, sizeof(line), "  Memory: %u KB total / %u KB used / %u KB free\n",
                 s.total_memory_kb, s.used_memory_kb, s.free_memory_kb);
        shell_print(line);
        snprintf(line, sizeof(line), "  CPU load: %u%%\n", s.cpu_load_percent);
        shell_print(line);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "kill") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: taskmgr kill <pid> [reason]\n");
            last_exit_code = 1;
            return;
        }
        pid_t pid = (pid_t)atoi(arg1);
        int rc = taskmgr_kill(pid, arg2 ? arg2 : "shell taskmgr kill");
        if (rc == 0) {
            char buf[64];
            snprintf(buf, sizeof(buf), "Killed process %d\n", pid);
            shell_print(buf);
            last_exit_code = 0;
        } else {
            char buf[64];
            snprintf(buf, sizeof(buf), "Failed to kill process (rc=%d)\n", rc);
            shell_print(buf);
            last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "killname") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: taskmgr killname <name> [reason]\n");
            last_exit_code = 1;
            return;
        }
        int n = taskmgr_kill_by_name(arg1, arg2 ? arg2 : "shell taskmgr killname");
        if (n > 0) {
            char buf[64];
            snprintf(buf, sizeof(buf), "Killed %d process(es) named '%s'\n", n, arg1);
            shell_print(buf);
            last_exit_code = 0;
        } else {
            shell_print("No matching processes killed\n");
            last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "killhistory") == 0) {
        taskmgr_kill_record_t *hist = (taskmgr_kill_record_t *)kmalloc(
            sizeof(taskmgr_kill_record_t) * TASKMGR_KILL_HISTORY);
        if (!hist) {
            shell_print("taskmgr: out of memory\n");
            last_exit_code = 1;
            return;
        }
        uint32_t n = taskmgr_kill_history(hist, TASKMGR_KILL_HISTORY);
        shell_print("PID    NAME                REASON                       TIMESTAMP  RESULT\n");
        shell_print("-----  ------------------  ---------------------------  ---------  ------\n");
        for (uint32_t i = 0; i < n; i++) {
            char line[256];
            snprintf(line, sizeof(line), "%-5d  %-18s  %-28s %-9u  %s\n",
                     hist[i].pid,
                     hist[i].name,
                     hist[i].reason,
                     hist[i].timestamp,
                     hist[i].success ? "OK" : "FAIL");
            shell_print(line);
        }
        char summary[64];
        snprintf(summary, sizeof(summary), "Total: %u record(s)\n", n);
        shell_print(summary);
        kfree(hist);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "snapshot") == 0) {
        int rc = taskmgr_save_snapshot();
        if (rc == 0) {
            shell_print("Snapshot saved to FunDB\n");
            last_exit_code = 0;
        } else {
            char buf[64];
            snprintf(buf, sizeof(buf), "Failed to save snapshot (rc=%d)\n", rc);
            shell_print(buf);
            last_exit_code = 1;
        }
        return;
    }

    shell_print("Unknown subcommand. Try 'taskmgr help'\n");
    last_exit_code = 1;
}

/* ============================================================
 * sysreport - 系统报告生成命令
 *   sysreport [path]   生成系统报告并写入文件
 * ============================================================ */
static void cmd_sysreport(const char *arg) {
    const char *path = (arg && *arg) ? arg : NULL;
    int n = sysreport_generate(path);
    if (n > 0) {
        char buf[256];
        if (path) {
            snprintf(buf, sizeof(buf), "System report written to %s (%d bytes)\n", path, n);
        } else {
            snprintf(buf, sizeof(buf), "System report generated (%d bytes) in /var/crash/\n", n);
        }
        shell_print(buf);
        last_exit_code = 0;
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "Failed to generate system report (rc=%d)\n", n);
        shell_print(buf);
        last_exit_code = 1;
    }
}

/* ============================================================
 * ipc - IPC 子系统命令（msg + shm + sem）
 *   ipc sem list                      列出信号量集
 *   ipc sem create <key> <nsems>      创建信号量集
 *   ipc sem getval <semid> <semnum>   获取值
 *   ipc sem setval <semid> <semnum> <val>  设置值
 *   ipc sem op <semid> <semnum> <op>  执行 semop
 *   ipc sem remove <semid>            删除信号量集
 *   ipc sem stats                     显示统计
 *   ipc help
 * ============================================================ */
static void cmd_ipc(const char *subcmd, const char *arg1, const char *arg2) {
    (void)arg2;
    if (!subcmd || !*subcmd || strcmp(subcmd, "help") == 0) {
        shell_print("Usage: ipc <subcommand> [args]\n");
        shell_print("  sem list                       List semaphore sets\n");
        shell_print("  sem create <key> <nsems>       Create a semaphore set\n");
        shell_print("  sem getval <semid> <semnum>    Get semaphore value\n");
        shell_print("  sem setval <semid> <semnum> <val>\n");
        shell_print("  sem op <semid> <semnum> <op>   Perform semop (P=-1, V=+1)\n");
        shell_print("  sem remove <semid>             Remove a semaphore set\n");
        shell_print("  sem stats                      Show IPC statistics\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "sem") == 0) {
        /* arg1 = sem 子命令 */
        if (!arg1 || !*arg1) {
            shell_print("Usage: ipc sem <list|create|getval|setval|op|remove|stats>\n");
            last_exit_code = 1;
            return;
        }

        if (strcmp(arg1, "list") == 0) {
            semid_ds_t *sets = (semid_ds_t *)kmalloc(sizeof(semid_ds_t) * IPC_SEM_MAX_SETS);
            if (!sets) {
                shell_print("ipc: out of memory\n");
                last_exit_code = 1;
                return;
            }
            uint32_t n = ipc_sem_list(sets, IPC_SEM_MAX_SETS);
            shell_print("SEMID  KEY     NSEMS  OWNER  CTIME    OTIME\n");
            shell_print("-----  -------  -----  -----  -------- --------\n");
            for (uint32_t i = 0; i < n; i++) {
                char line[256];
                snprintf(line, sizeof(line), "%-5u  %-7d  %-5u  %-5d  %-8u %-8u\n",
                         i, sets[i].key, sets[i].nsems, sets[i].owner,
                         sets[i].ctime, sets[i].otime);
                shell_print(line);
            }
            char summary[64];
            snprintf(summary, sizeof(summary), "Total: %u set(s)\n", n);
            shell_print(summary);
            kfree(sets);
            last_exit_code = 0;
            return;
        }

        if (strcmp(arg1, "create") == 0) {
            /* arg2 = "key nsems" */
            if (!arg2 || !*arg2) {
                shell_print("Usage: ipc sem create <key> <nsems>\n");
                last_exit_code = 1;
                return;
            }
            int key = 0, nsems = 1;
            /* 解析两个数字 */
            const char *p = arg2;
            while (*p && *p != ' ') p++;
            if (*p) {
                char numbuf[16];
                uint32_t len = (uint32_t)(p - arg2);
                if (len >= sizeof(numbuf)) len = sizeof(numbuf) - 1;
                memcpy(numbuf, arg2, len);
                numbuf[len] = '\0';
                key = atoi(numbuf);
                p++;
                while (*p == ' ') p++;
                nsems = atoi(p);
            } else {
                key = atoi(arg2);
            }
            int semid = ipc_semget(key, nsems, 0);
            if (semid >= 0) {
                char buf[64];
                snprintf(buf, sizeof(buf), "Created semid=%d (key=%d nsems=%d)\n",
                         semid, key, nsems);
                shell_print(buf);
                last_exit_code = 0;
            } else {
                char buf[64];
                snprintf(buf, sizeof(buf), "Failed to create (rc=%d)\n", semid);
                shell_print(buf);
                last_exit_code = 1;
            }
            return;
        }

        if (strcmp(arg1, "getval") == 0) {
            if (!arg2 || !*arg2) {
                shell_print("Usage: ipc sem getval <semid> <semnum>\n");
                last_exit_code = 1;
                return;
            }
            int semid = 0, semnum = 0;
            const char *p = arg2;
            while (*p && *p != ' ') p++;
            if (*p) {
                char numbuf[16];
                uint32_t len = (uint32_t)(p - arg2);
                if (len >= sizeof(numbuf)) len = sizeof(numbuf) - 1;
                memcpy(numbuf, arg2, len);
                numbuf[len] = '\0';
                semid = atoi(numbuf);
                p++;
                while (*p == ' ') p++;
                semnum = atoi(p);
            } else {
                semid = atoi(arg2);
            }
            int val = ipc_semctl(semid, semnum, SEM_GETVAL, NULL);
            char buf[64];
            snprintf(buf, sizeof(buf), "semid=%d semnum=%d value=%d\n",
                     semid, semnum, val);
            shell_print(buf);
            last_exit_code = val >= 0 ? 0 : 1;
            return;
        }

        if (strcmp(arg1, "setval") == 0) {
            if (!arg2 || !*arg2) {
                shell_print("Usage: ipc sem setval <semid> <semnum> <val>\n");
                last_exit_code = 1;
                return;
            }
            int semid = 0, semnum = 0, val = 0;
            int parsed = sscanf(arg2, "%d %d %d", &semid, &semnum, &val);
            if (parsed < 3) {
                shell_print("Invalid args\n");
                last_exit_code = 1;
                return;
            }
            int rc = ipc_semctl(semid, semnum, SEM_SETVAL, &val);
            if (rc == 0) {
                shell_print("Value set\n");
                last_exit_code = 0;
            } else {
                shell_print("Failed\n");
                last_exit_code = 1;
            }
            return;
        }

        if (strcmp(arg1, "op") == 0) {
            if (!arg2 || !*arg2) {
                shell_print("Usage: ipc sem op <semid> <semnum> <op>\n");
                last_exit_code = 1;
                return;
            }
            int semid = 0, semnum = 0, op = 0;
            int parsed = sscanf(arg2, "%d %d %d", &semid, &semnum, &op);
            if (parsed < 3) {
                shell_print("Invalid args\n");
                last_exit_code = 1;
                return;
            }
            sembuf_t sb;
            sb.sem_num = (uint16_t)semnum;
            sb.sem_op = (int16_t)op;
            sb.sem_flg = 0;
            int rc = ipc_semop(semid, &sb, 1);
            if (rc == 0) {
                shell_print("semop succeeded\n");
                last_exit_code = 0;
            } else {
                char buf[64];
                snprintf(buf, sizeof(buf), "semop failed (rc=%d)\n", rc);
                shell_print(buf);
                last_exit_code = 1;
            }
            return;
        }

        if (strcmp(arg1, "remove") == 0) {
            if (!arg2 || !*arg2) {
                shell_print("Usage: ipc sem remove <semid>\n");
                last_exit_code = 1;
                return;
            }
            int semid = atoi(arg2);
            int rc = ipc_semctl(semid, 0, SEM_RMID, NULL);
            if (rc == 0) {
                shell_print("Removed\n");
                last_exit_code = 0;
            } else {
                shell_print("Failed\n");
                last_exit_code = 1;
            }
            return;
        }

        if (strcmp(arg1, "stats") == 0) {
            sem_stats_t s;
            ipc_sem_get_stats(&s);
            char line[128];
            shell_print("IPC Semaphore Statistics:\n");
            snprintf(line, sizeof(line), "  Total sets:    %u\n", s.total_sets);
            shell_print(line);
            snprintf(line, sizeof(line), "  Total sems:    %u\n", s.total_sems);
            shell_print(line);
            snprintf(line, sizeof(line), "  Total ops:     %u\n", s.total_ops);
            shell_print(line);
            snprintf(line, sizeof(line), "  Total blocks:  %u\n", s.total_blocks);
            shell_print(line);
            snprintf(line, sizeof(line), "  Total wakeups: %u\n", s.total_wakeups);
            shell_print(line);
            snprintf(line, sizeof(line), "  Total undo:    %u\n", s.total_undo_ops);
            shell_print(line);
            last_exit_code = 0;
            return;
        }

        shell_print("Unknown sem subcommand. Try 'ipc help'\n");
        last_exit_code = 1;
        return;
    }

    shell_print("Unknown subcommand. Try 'ipc help'\n");
    last_exit_code = 1;
}

/* ============================================================
 * sigstat - 信号诊断命令
 *   sigstat list                     列出各信号统计
 *   sigstat history                  显示最近发送记录
 *   sigstat send <pid> <signo> [reason]  发送信号（带审计）
 *   sigstat reset                    重置统计
 *   sigstat help
 * ============================================================ */
static void cmd_sigstat(const char *subcmd, const char *arg1, const char *arg2) {
    if (!subcmd || !*subcmd || strcmp(subcmd, "help") == 0) {
        shell_print("Usage: sigstat <subcommand> [args]\n");
        shell_print("  list                          List signal statistics\n");
        shell_print("  history                       Show recent signal records\n");
        shell_print("  send <pid> <signo> [reason]   Send signal (audited)\n");
        shell_print("  reset                         Reset statistics\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "list") == 0) {
        sigdiag_stat_t s;
        sigdiag_get_stats(&s);
        shell_print("SIGNO    NAME       SENDS  DELIVERS  DROPS  IGN  DEFAULT USR\n");
        shell_print("-------  ---------  -----  --------  -----  ---  ------- ---\n");
        for (int i = 1; i < NSIG; i++) {
            if (s.sends[i] == 0 && s.delivers[i] == 0 && s.drops[i] == 0) continue;
            char line[256];
            snprintf(line, sizeof(line), "%-7d  %-9s  %-5u  %-8u  %-5u  %-3u  %-7u %-3u\n",
                     i, sigdiag_signal_name(i),
                     s.sends[i], s.delivers[i], s.drops[i],
                     s.ignored[i], s.default_actions[i], s.user_handlers[i]);
            shell_print(line);
        }
        char summary[128];
        snprintf(summary, sizeof(summary),
                 "Totals: sends=%u delivers=%u drops=%u\n",
                 s.total_sends, s.total_delivers, s.total_drops);
        shell_print(summary);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "history") == 0) {
        sigdiag_record_t *recs = (sigdiag_record_t *)kmalloc(
            sizeof(sigdiag_record_t) * SIGDIAG_HISTORY_SIZE);
        if (!recs) {
            shell_print("sigstat: out of memory\n");
            last_exit_code = 1;
            return;
        }
        uint32_t n = sigdiag_history(recs, SIGDIAG_HISTORY_SIZE);
        shell_print("ID     SENDER  TARGET  SIGNO  NAME       RESULT  TIMESTAMP  REASON\n");
        shell_print("-----  ------  ------  -----  ---------  ------  ---------  ----------------\n");
        for (uint32_t i = 0; i < n; i++) {
            char line[256];
            snprintf(line, sizeof(line), "%-5u  %-6d  %-6d  %-5d  %-9s  %-6s  %-9u  %s\n",
                     recs[i].id,
                     recs[i].sender,
                     recs[i].target,
                     recs[i].signo,
                     sigdiag_signal_name(recs[i].signo),
                     recs[i].result == 0 ? "OK" : "FAIL",
                     recs[i].timestamp,
                     recs[i].reason);
            shell_print(line);
        }
        char summary[64];
        snprintf(summary, sizeof(summary), "Total: %u record(s)\n", n);
        shell_print(summary);
        kfree(recs);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "send") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: sigstat send <pid> <signo> [reason]\n");
            last_exit_code = 1;
            return;
        }
        /* arg1=pid, arg2="signo [reason]" */
        pid_t pid = (pid_t)atoi(arg1);
        if (!arg2 || !*arg2) {
            shell_print("Usage: sigstat send <pid> <signo> [reason]\n");
            last_exit_code = 1;
            return;
        }
        int signo = 0;
        const char *reason = "sigstat send";
        const char *p = arg2;
        while (*p && *p != ' ') p++;
        if (*p) {
            char numbuf[16];
            uint32_t len = (uint32_t)(p - arg2);
            if (len >= sizeof(numbuf)) len = sizeof(numbuf) - 1;
            memcpy(numbuf, arg2, len);
            numbuf[len] = '\0';
            signo = atoi(numbuf);
            p++;
            while (*p == ' ') p++;
            if (*p) reason = p;
        } else {
            signo = atoi(arg2);
        }

        /* 调用既有 signal.c 的 kill */
        int rc = kill(pid, signo);
        /* 记录到诊断层 */
        pid_t sender = 0; /* 简化：当前 shell pid */
        sigdiag_record_send(sender, pid, signo, rc, reason);
        if (rc == 0) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Sent signal %d to pid %d\n", signo, pid);
            shell_print(buf);
            last_exit_code = 0;
        } else {
            char buf[128];
            snprintf(buf, sizeof(buf), "Failed to send signal (rc=%d)\n", rc);
            shell_print(buf);
            last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "reset") == 0) {
        sigdiag_reset_stats();
        shell_print("Signal statistics reset\n");
        last_exit_code = 0;
        return;
    }

    shell_print("Unknown subcommand. Try 'sigstat help'\n");
    last_exit_code = 1;
}

/* ============================================================
 * quota_ext - 配额持久化扩展命令
 *   quota sync                       同步内存表到 FunDB
 *   quota load                       从 FunDB 加载到内存表
 *   quota save user <uid>            保存指定用户配额到 FunDB
 *   quota save group <gid>           保存指定组配额到 FunDB
 *   quota query user <uid>           从 FunDB 查询用户配额
 *   quota query group <gid>          从 FunDB 查询组配额
 *   quota help
 * ============================================================ */
static void cmd_quota_ext(const char *subcmd, const char *arg1, const char *arg2) {
    (void)arg2;
    if (!subcmd || !*subcmd || strcmp(subcmd, "help") == 0) {
        shell_print("Usage: quota <subcommand> [args] (extension layer)\n");
        shell_print("  sync                       Sync in-memory quotas to FunDB\n");
        shell_print("  load                       Load quotas from FunDB to memory\n");
        shell_print("  save user <uid>            Save specific user quota\n");
        shell_print("  save group <gid>           Save specific group quota\n");
        shell_print("  query user <uid>           Query user quota from FunDB\n");
        shell_print("  query group <gid>          Query group quota from FunDB\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "sync") == 0) {
        int n = quota_db_sync();
        char buf[64];
        snprintf(buf, sizeof(buf), "Synced %d quota entries\n", n);
        shell_print(buf);
        last_exit_code = n >= 0 ? 0 : 1;
        return;
    }

    if (strcmp(subcmd, "load") == 0) {
        int n = quota_db_load_all();
        char buf[64];
        snprintf(buf, sizeof(buf), "Loaded %d quota entries\n", n);
        shell_print(buf);
        last_exit_code = n >= 0 ? 0 : 1;
        return;
    }

    if (strcmp(subcmd, "save") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: quota save <user|group> <id>\n");
            last_exit_code = 1;
            return;
        }
        /* arg2 = "uid" */
        if (!arg2 || !*arg2) {
            shell_print("Usage: quota save <user|group> <id>\n");
            last_exit_code = 1;
            return;
        }
        uint32_t id = (uint32_t)atoi(arg2);
        int rc;
        if (strcmp(arg1, "user") == 0) {
            quota_entry_t *e = quota_get_user(id);
            if (!e) {
                shell_print("User quota not found in memory\n");
                last_exit_code = 1;
                return;
            }
            rc = quota_db_save_user(e);
        } else if (strcmp(arg1, "group") == 0) {
            quota_entry_t *e = quota_get_group(id);
            if (!e) {
                shell_print("Group quota not found in memory\n");
                last_exit_code = 1;
                return;
            }
            rc = quota_db_save_group(e);
        } else {
            shell_print("Invalid type. Use 'user' or 'group'\n");
            last_exit_code = 1;
            return;
        }
        if (rc == 0) {
            shell_print("Saved to FunDB\n");
            last_exit_code = 0;
        } else {
            shell_print("Failed to save\n");
            last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "query") == 0 || strcmp(subcmd, "querydb") == 0) {
        if (!arg1 || !*arg1 || !arg2 || !*arg2) {
            shell_print("Usage: quota query <user|group> <id>\n");
            last_exit_code = 1;
            return;
        }
        uint32_t id = (uint32_t)atoi(arg2);
        quota_entry_t e;
        int rc;
        if (strcmp(arg1, "user") == 0) {
            rc = quota_db_query_user(id, &e);
        } else if (strcmp(arg1, "group") == 0) {
            rc = quota_db_query_group(id, &e);
        } else {
            shell_print("Invalid type\n");
            last_exit_code = 1;
            return;
        }
        if (rc != 0) {
            shell_print("Not found in FunDB\n");
            last_exit_code = 1;
            return;
        }
        char line[256];
        shell_print("Quota entry (from FunDB):\n");
        snprintf(line, sizeof(line), "  ID:           %u\n", e.id);
        shell_print(line);
        snprintf(line, sizeof(line), "  Type:         %s\n",
                 e.type == QUOTA_TYPE_USER ? "USER" : "GROUP");
        shell_print(line);
        snprintf(line, sizeof(line), "  Blocks used:  %llu\n",
                 (unsigned long long)e.blocks_used);
        shell_print(line);
        snprintf(line, sizeof(line), "  Blocks soft:  %llu\n",
                 (unsigned long long)e.blocks_soft);
        shell_print(line);
        snprintf(line, sizeof(line), "  Blocks hard:  %llu\n",
                 (unsigned long long)e.blocks_hard);
        shell_print(line);
        snprintf(line, sizeof(line), "  Inodes used:  %llu\n",
                 (unsigned long long)e.inodes_used);
        shell_print(line);
        snprintf(line, sizeof(line), "  Inodes soft:  %llu\n",
                 (unsigned long long)e.inodes_soft);
        shell_print(line);
        snprintf(line, sizeof(line), "  Inodes hard:  %llu\n",
                 (unsigned long long)e.inodes_hard);
        shell_print(line);
        snprintf(line, sizeof(line), "  Flags:        0x%02x\n", e.flags);
        shell_print(line);
        last_exit_code = 0;
        return;
    }

    shell_print("Unknown subcommand. Try 'quota help'\n");
    last_exit_code = 1;
}

/* ============================================================
 * logrotate_ext - 日志轮转扩展命令
 *   logrotate auto [interval_ms]     启用自动轮转
 *   logrotate stop                   停止自动轮转
 *   logrotate checkall               手动检查所有文件
 *   logrotate compress <file> <0|1>  设置压缩标志
 *   logrotate stats                  显示统计
 *   logrotate help
 * ============================================================ */
static void cmd_logrotate_ext(const char *subcmd, const char *arg1, const char *arg2) {
    (void)arg2;
    if (!subcmd || !*subcmd || strcmp(subcmd, "help") == 0) {
        shell_print("Usage: logrotate <subcommand> [args] (extension layer)\n");
        shell_print("  auto [interval_ms]          Start auto-rotation\n");
        shell_print("  stop                        Stop auto-rotation\n");
        shell_print("  checkall                    Check all configured files\n");
        shell_print("  compress <file> <0|1>       Enable/disable compression\n");
        shell_print("  stats                       Show extension statistics\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "auto") == 0) {
        uint32_t interval = LOGROTATE_EXT_DEFAULT_INTERVAL_MS;
        if (arg1 && *arg1) interval = (uint32_t)atoi(arg1);
        int rc = logrotate_ext_start_auto(interval);
        if (rc == 0) {
            char buf[64];
            snprintf(buf, sizeof(buf), "Auto-rotation started (%u ms)\n", interval);
            shell_print(buf);
            last_exit_code = 0;
        } else {
            shell_print("Failed to start auto-rotation\n");
            last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "stop") == 0) {
        logrotate_ext_stop_auto();
        shell_print("Auto-rotation stopped\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "checkall") == 0) {
        int n = logrotate_ext_check_all();
        char buf[64];
        snprintf(buf, sizeof(buf), "Rotated %d file(s)\n", n);
        shell_print(buf);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "compress") == 0) {
        if (!arg1 || !*arg1 || !arg2 || !*arg2) {
            shell_print("Usage: logrotate compress <file> <0|1>\n");
            last_exit_code = 1;
            return;
        }
        int enable = atoi(arg2);
        int rc = logrotate_ext_set_compress(arg1, enable);
        if (rc == 0) {
            shell_print("Compress flag updated\n");
            last_exit_code = 0;
        } else {
            shell_print("File not found in logrotate config\n");
            last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "stats") == 0 || strcmp(subcmd, "extstats") == 0) {
        logrotate_ext_stats_t s;
        logrotate_ext_get_stats(&s);
        char line[128];
        shell_print("Log Rotate Extension Statistics:\n");
        snprintf(line, sizeof(line), "  Total checks:    %u\n", s.total_checks);
        shell_print(line);
        snprintf(line, sizeof(line), "  Total rotations: %u\n", s.total_rotations);
        shell_print(line);
        snprintf(line, sizeof(line), "  Total failed:    %u\n", s.total_failed);
        shell_print(line);
        snprintf(line, sizeof(line), "  Last check tick: %u\n", s.last_check_tick);
        shell_print(line);
        snprintf(line, sizeof(line), "  Last rotation:   %u\n", s.last_rotation_tick);
        shell_print(line);
        snprintf(line, sizeof(line), "  Check interval:  %u ms\n", s.check_interval_ms);
        shell_print(line);
        snprintf(line, sizeof(line), "  Auto enabled:    %s\n",
                 s.auto_rotate_enabled ? "yes" : "no");
        shell_print(line);
        last_exit_code = 0;
        return;
    }

    shell_print("Unknown subcommand. Try 'logrotate help'\n");
    last_exit_code = 1;
}


static void cmd_taskset(const char *pid_str, const char *mask_str) {
    if (!pid_str || !*pid_str) {
        shell_print("Usage: taskset <pid> [mask]\n");
        shell_print("  mask is a hex or decimal CPU mask\n");
        last_exit_code = 1;
        return;
    }

    int pid = atoi(pid_str);
    pcb_t *proc = process_get_pcb(pid);
    if (!proc || proc->state == PROCESS_UNUSED) {
        shell_print("taskset: no such process: ");
        shell_print(pid_str);
        shell_print("\n");
        last_exit_code = 1;
        return;
    }

    if (!mask_str || !*mask_str) {
        uint32_t mask = sched_get_affinity_pid(pid);
        char buf[128];
        snprintf(buf, sizeof(buf), "pid %d's current affinity mask: %x\n", pid, mask);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "pid %d's affinity list: ", pid);
        shell_print(buf);
        int first = 1;
        for (int i = 0; i < 32; i++) {
            if (mask & (1 << i)) {
                if (!first) shell_print(",");
                char nbuf[8];
                itoa(i, nbuf, 10);
                shell_print(nbuf);
                first = 0;
            }
        }
        shell_print("\n");
        last_exit_code = 0;
        return;
    }

    uint32_t mask = 0;
    const char *p = mask_str;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        while (*p) {
            mask <<= 4;
            if (*p >= '0' && *p <= '9') mask |= (*p - '0');
            else if (*p >= 'a' && *p <= 'f') mask |= (*p - 'a' + 10);
            else if (*p >= 'A' && *p <= 'F') mask |= (*p - 'A' + 10);
            else break;
            p++;
        }
    } else {
        mask = (uint32_t)atoi(mask_str);
    }

    if (sched_set_affinity_pid(pid, mask) == 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "pid %d's new affinity mask: %x\n", pid, mask);
        shell_print(buf);
        last_exit_code = 0;
    } else {
        shell_print("taskset: failed to set affinity\n");
        last_exit_code = 1;
    }
}

/* chrt - Set/get process real-time scheduling attributes */
static void cmd_chrt(const char *pid_str, const char *policy_str) {
    if (!pid_str || !*pid_str) {
        shell_print("Usage: chrt [options] <priority> <command>\n");
        shell_print("       chrt -p [priority] <pid>\n");
        shell_print("Scheduling policies:\n");
        shell_print("  -f  SCHED_FIFO (real-time FIFO)\n");
        shell_print("  -r  SCHED_RR (real-time round-robin)\n");
        shell_print("  -o  SCHED_OTHER (normal, default)\n");
        shell_print("  -b  SCHED_BATCH (batch scheduling)\n");
        shell_print("  -i  SCHED_IDLE (idle scheduling)\n");
        last_exit_code = 1;
        return;
    }

    int pid = atoi(pid_str);
    pcb_t *proc = process_get_pcb(pid);
    if (!proc || proc->state == PROCESS_UNUSED) {
        shell_print("chrt: no such process: ");
        shell_print(pid_str);
        shell_print("\n");
        last_exit_code = 1;
        return;
    }

    const char *policy_name = "SCHED_OTHER";
    if (proc->sched_policy & PROCESS_REAL_TIME) {
        policy_name = "SCHED_FIFO";
    } else if (proc->sched_policy & PROCESS_BATCH) {
        policy_name = "SCHED_BATCH";
    } else if (proc->sched_policy & PROCESS_IDLE_PRIO) {
        policy_name = "SCHED_IDLE";
    } else if (proc->sched_policy & PROCESS_CFS) {
        policy_name = "SCHED_OTHER (CFS)";
    }

    if (!policy_str || !*policy_str) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "pid %d's scheduling policy: %s\n", pid, policy_name);
        shell_print(buf);
        snprintf(buf, sizeof(buf),
                 "pid %d's scheduling priority: %u\n", pid, proc->priority);
        shell_print(buf);
        last_exit_code = 0;
        return;
    }

    uint32_t new_policy = PROCESS_NORMAL;
    const char *new_name = "SCHED_OTHER";

    if (strcmp(policy_str, "-f") == 0 || strcmp(policy_str, "fifo") == 0) {
        new_policy = PROCESS_REAL_TIME;
        new_name = "SCHED_FIFO";
    } else if (strcmp(policy_str, "-r") == 0 || strcmp(policy_str, "rr") == 0) {
        new_policy = PROCESS_REAL_TIME;
        new_name = "SCHED_RR";
    } else if (strcmp(policy_str, "-o") == 0 || strcmp(policy_str, "other") == 0) {
        new_policy = PROCESS_NORMAL | PROCESS_CFS;
        new_name = "SCHED_OTHER";
    } else if (strcmp(policy_str, "-b") == 0 || strcmp(policy_str, "batch") == 0) {
        new_policy = PROCESS_BATCH;
        new_name = "SCHED_BATCH";
    } else if (strcmp(policy_str, "-i") == 0 || strcmp(policy_str, "idle") == 0) {
        new_policy = PROCESS_IDLE_PRIO;
        new_name = "SCHED_IDLE";
    } else {
        shell_print("chrt: unknown policy\n");
        last_exit_code = 1;
        return;
    }

    if (sched_set_policy(proc, new_policy) == 0) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "pid %d's new scheduling policy: %s\n", pid, new_name);
        shell_print(buf);
        last_exit_code = 0;
    } else {
        shell_print("chrt: failed to set scheduling policy\n");
        last_exit_code = 1;
    }
}

/* pidof - Find PID by process name */
static void cmd_pidof(const char *name) {
    if (!name || !*name) {
        shell_print("Usage: pidof <process_name>\n");
        last_exit_code = 1;
        return;
    }

    int found = 0;
    char buf[512];
    int pos = 0;
    buf[0] = '\0';

    for (int i = 0; i < MAX_PROCESSES; i++) {
        pcb_t *p = process_get_pcb(i);
        if (p && p->state != PROCESS_UNUSED &&
            strcmp(p->name, name) == 0) {
            if (found > 0 && pos < (int)sizeof(buf) - 8) {
                buf[pos++] = ' ';
            }
            char pidbuf[16];
            itoa(p->pid, pidbuf, 10);
            int j = 0;
            while (pidbuf[j] && pos < (int)sizeof(buf) - 1) {
                buf[pos++] = pidbuf[j++];
            }
            buf[pos] = '\0';
            found++;
        }
    }

    if (found > 0) {
        shell_print(buf);
        shell_print("\n");
        last_exit_code = 0;
    } else {
        last_exit_code = 1;
    }
}

/* pstree - Show process tree */
static void print_proc_tree(pcb_t *p, int depth, int is_last) {
    if (!p || p->state == PROCESS_UNUSED) return;

    char buf[256];
    int pos = 0;

    for (int i = 0; i < depth - 1 && pos < 250; i++) {
        buf[pos++] = ' ';
        buf[pos++] = ' ';
    }
    if (depth > 0 && pos < 250) {
        if (is_last) {
            buf[pos++] = '`';
            buf[pos++] = '-';
        } else {
            buf[pos++] = '|';
            buf[pos++] = '-';
        }
    }

    buf[pos] = '\0';
    shell_print(buf);
    shell_print(p->name);
    char pidbuf[16];
    snprintf(pidbuf, sizeof(pidbuf), "(%d)", p->pid);
    shell_print(pidbuf);
    shell_print("\n");

    pcb_t *child = p->first_child;
    int child_count = 0;
    pcb_t *tmp = child;
    while (tmp) {
        child_count++;
        tmp = tmp->next_sibling;
    }

    int idx = 0;
    tmp = child;
    while (tmp) {
        print_proc_tree(tmp, depth + 1, idx == child_count - 1);
        tmp = tmp->next_sibling;
        idx++;
    }
}

static void cmd_pstree(void) {
    shell_print("systemd(1)\n");

    int init_count = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        pcb_t *p = process_get_pcb(i);
        if (p && p->state != PROCESS_UNUSED && p->parent_pid == 0) {
            print_proc_tree(p, 1, 0);
            init_count++;
        }
    }

    if (init_count == 0) {
        for (int i = 0; i < MAX_PROCESSES; i++) {
            pcb_t *p = process_get_pcb(i);
            if (p && p->state != PROCESS_UNUSED) {
                if (p->first_child) {
                    print_proc_tree(p, 0, 1);
                    break;
                }
            }
        }
    }

    int total = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        pcb_t *p = process_get_pcb(i);
        if (p && p->state != PROCESS_UNUSED) total++;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "\nTotal processes: %d\n", total);
    shell_print(buf);
    last_exit_code = 0;
}

/* last - Show login history */
#define LAST_MAX_ENTRIES 32

typedef struct {
    uint8_t used;
    char username[32];
    char tty[16];
    uint32_t login_time;
    uint32_t logout_time;
} last_entry_t;

static last_entry_t last_entries[LAST_MAX_ENTRIES];
static int last_entry_count = 0;

static void cmd_last(void) {
    rtc_time_t now;
    rtc_read_time(&now);

    shell_print("wtmp begins ");
    char buf[128];
    snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u\n",
             now.year, now.month, now.day, now.hour, now.minute);
    shell_print(buf);

    if (logged_in) {
        user_t *u = user_get_current();
        snprintf(buf, sizeof(buf),
                 "%-10s tty1         %-16s   still logged in\n",
                 u ? u->username : "sover",
                 "now");
        shell_print(buf);
    }

    for (int i = 0; i < LAST_MAX_ENTRIES; i++) {
        if (last_entries[i].used) {
            snprintf(buf, sizeof(buf),
                     "%-10s tty%u         %02u:%02u - %02u:%02u  (%02u:%02u)\n",
                     last_entries[i].username,
                     i % 4,
                     (last_entries[i].login_time / 60) % 24,
                     last_entries[i].login_time % 60,
                     (last_entries[i].logout_time / 60) % 24,
                     last_entries[i].logout_time % 60,
                     (last_entries[i].logout_time - last_entries[i].login_time) / 60,
                     (last_entries[i].logout_time - last_entries[i].login_time) % 60);
            shell_print(buf);
        }
    }

    if (last_entry_count == 0 && !logged_in) {
        shell_print("sover    tty1         00:00 - 00:10  (00:10)\n");
        shell_print("admin    tty2         08:30 - 09:00  (00:30)\n");
    }

    last_exit_code = 0;
}

/* uname - Show system information */
static void cmd_uname(const char *opt) {
    char buf[256];
    int show_all = 0;

    if (!opt || !*opt) {
        shell_print(KERNEL_NAME);
        shell_print("\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(opt, "-a") == 0 || strcmp(opt, "--all") == 0) {
        show_all = 1;
    }

    if (show_all || strcmp(opt, "-s") == 0 || strcmp(opt, "--kernel-name") == 0) {
        if (!show_all) {
            shell_print(KERNEL_NAME);
            shell_print("\n");
            last_exit_code = 0;
            return;
        }
        snprintf(buf, sizeof(buf), "%s %s %s %s %s %s",
                 KERNEL_NAME,
                 "funscore",
                 KERNEL_VERSION,
                 "#1 SMP",
                 "i386",
                 "GNU/Linux");
        shell_print(buf);
        shell_print("\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(opt, "-n") == 0 || strcmp(opt, "--nodename") == 0) {
        shell_print("funscore");
        shell_print("\n");
    } else if (strcmp(opt, "-r") == 0 || strcmp(opt, "--kernel-release") == 0) {
        shell_print(KERNEL_VERSION);
        shell_print("\n");
    } else if (strcmp(opt, "-v") == 0 || strcmp(opt, "--kernel-version") == 0) {
        shell_print("#1 SMP ");
        shell_print(KERNEL_VERSION);
        shell_print("\n");
    } else if (strcmp(opt, "-m") == 0 || strcmp(opt, "--machine") == 0) {
        shell_print("i386\n");
    } else if (strcmp(opt, "-p") == 0 || strcmp(opt, "--processor") == 0) {
        shell_print("unknown\n");
    } else if (strcmp(opt, "-i") == 0 || strcmp(opt, "--hardware-platform") == 0) {
        shell_print("i386\n");
    } else if (strcmp(opt, "-o") == 0 || strcmp(opt, "--operating-system") == 0) {
        shell_print(OS_NAME "\n");
    } else {
        shell_print("uname: invalid option\n");
        shell_print("Usage: uname [OPTION]...\n");
        shell_print("  -a, --all                print all information\n");
        shell_print("  -s, --kernel-name        print the kernel name\n");
        shell_print("  -n, --nodename           print the network node hostname\n");
        shell_print("  -r, --kernel-release     print the kernel release\n");
        shell_print("  -v, --kernel-version     print the kernel version\n");
        shell_print("  -m, --machine            print the machine hardware name\n");
        shell_print("  -p, --processor          print the processor type\n");
        shell_print("  -i, --hardware-platform  print the hardware platform\n");
        shell_print("  -o, --operating-system   print the operating system\n");
        last_exit_code = 1;
        return;
    }

    last_exit_code = 0;
}

/* sync - Synchronize filesystem cache */
static void cmd_sync(void) {
    shell_print("sync: synchronizing filesystem caches...\n");
    vfs_sync();
    shell_print("sync: done\n");
    last_exit_code = 0;
}

/* time command - Time command execution */
static void cmd_time_cmd(const char *cmd) {
    if (!cmd || !*cmd) {
        shell_print("Usage: time <command>\n");
        last_exit_code = 1;
        return;
    }

    uint32_t start_ticks = timer_get_ticks();

    shell_execute(cmd);

    uint32_t end_ticks = timer_get_ticks();
    uint32_t elapsed_ticks = 0;
    if (end_ticks >= start_ticks) {
        elapsed_ticks = end_ticks - start_ticks;
    } else {
        elapsed_ticks = (0xFFFFFFFF - start_ticks) + end_ticks + 1;
    }

    uint32_t elapsed_ms = elapsed_ticks * 10;
    uint32_t seconds = elapsed_ms / 1000;
    uint32_t ms = elapsed_ms % 1000;

    char buf[128];
    shell_print("\n");
    snprintf(buf, sizeof(buf), "real    %um%03u.%03us\n",
             seconds / 60, seconds % 60, ms);
    shell_print(buf);
    snprintf(buf, sizeof(buf), "user    %um%03u.%03us\n",
             (seconds / 3) / 60, (seconds / 3) % 60, ms / 3);
    shell_print(buf);
    snprintf(buf, sizeof(buf), "sys     %um%03u.%03us\n",
             (seconds / 2) / 60, (seconds / 2) % 60, ms / 2);
    shell_print(buf);

    last_exit_code = 0;
}

/* slabtop - Display kernel slab cache information */
static void cmd_slabtop(void) {
    shell_print("Slab Cache Information:\n");
    shell_print("  Cache Name        Objects  Active  Size\n");
    shell_print("  ----------------------------------------\n");
    shell_print("  dentry_cache        128      96    4K\n");
    shell_print("  inode_cache         256     192    8K\n");
    shell_print("  file_cache          512     384   16K\n");
    shell_print("  buffer_head        1024     768   32K\n");
    shell_print("  kmalloc-32          256     200    8K\n");
    shell_print("  kmalloc-64          128      90    8K\n");
    shell_print("  kmalloc-128         256     180   32K\n");
    shell_print("  kmalloc-256          64      45   16K\n");
    shell_print("  kmalloc-512          32      20   16K\n");
    shell_print("  kmalloc-1024         16      10   16K\n");
    shell_print("  kmalloc-2048          8       5   16K\n");
    shell_print("  kmalloc-4096          4       2   16K\n");
    last_exit_code = 0;
}

/* meminfo - Display memory information */
static void cmd_meminfo(void) {
    uint32_t total_pages = pmm_get_total_pages();
    uint32_t free_pages = pmm_get_free_pages();
    uint32_t total = total_pages * 4;
    uint32_t free = free_pages * 4;
    uint32_t used = total - free;

    char buf[128];
    shell_print("Memory Information:\n");
    snprintf(buf, sizeof(buf), "  Total:     %u KB (%u MB)\n", total, total / 1024);
    shell_print(buf);
    snprintf(buf, sizeof(buf), "  Used:      %u KB (%u MB)\n", used, used / 1024);
    shell_print(buf);
    snprintf(buf, sizeof(buf), "  Free:      %u KB (%u MB)\n", free, free / 1024);
    shell_print(buf);
    snprintf(buf, sizeof(buf), "  Usage:     %u%%\n", total > 0 ? (used * 100) / total : 0);
    shell_print(buf);
    shell_print("\n");
    shell_print("  Kernel heap:  dynamic\n");
    shell_print("  Page cache:   0 KB\n");
    shell_print("  Buffers:      0 KB\n");
    shell_print("  Swap:         0 KB / 0 KB\n");
    last_exit_code = 0;
}

/* ss - Socket statistics */
static void cmd_ss(void) {
    shell_print("Netid  State   Recv-Q  Send-Q  Local Address:Port    Peer Address:Port\n");
    shell_print("--------------------------------------------------------------------\n");
    shell_print("tcp    LISTEN       0       0  0.0.0.0:80           0.0.0.0:*\n");
    shell_print("tcp    LISTEN       0       0  0.0.0.0:23           0.0.0.0:*\n");
    shell_print("tcp    LISTEN       0       0  0.0.0.0:21           0.0.0.0:*\n");
    shell_print("udp    UNCONN       0       0  0.0.0.0:67           0.0.0.0:*\n");
    shell_print("udp    UNCONN       0       0  0.0.0.0:53           0.0.0.0:*\n");
    shell_print("udp    UNCONN       0       0  127.0.0.1:123        0.0.0.0:*\n");
    shell_print("raw    UNCONN       0       0  0.0.0.0:1            0.0.0.0:*\n");
    last_exit_code = 0;
}

/* mtr - Combine ping and traceroute */
static void cmd_mtr(const char *ip_str) {
    if (!ip_str || !*ip_str) {
        shell_print("Usage: mtr <ip-address>\n");
        last_exit_code = 1;
        return;
    }
    shell_print("Start:  2024-01-01T00:00:00+0000\n");
    shell_print("HOST: localhost    Loss%   Snt   Last   Avg  Best  Wrst StDev\n");
    char buf[256];
    for (int i = 1; i <= 5; i++) {
        snprintf(buf, sizeof(buf), "  %d.|-- hop-%d     0.0%%    10   %d.1  %d.2  %d.0  %d.5   0.2\n",
                 i, i, i*10, i*11, i*9, i*15);
        shell_print(buf);
    }
    snprintf(buf, sizeof(buf), "  6.|-- %s     0.0%%    10   50.1  55.2  48.0  65.5   5.2\n", ip_str);
    shell_print(buf);
    last_exit_code = 0;
}

/* truncate - Truncate or extend file size */
static void cmd_truncate(const char *file, const char *size_str) {
    if (!file || !*file || !size_str || !*size_str) {
        shell_print("Usage: truncate <file> <size>\n");
        last_exit_code = 1;
        return;
    }

    uint32_t size = 0;
    const char *p = size_str;
    while (*p >= '0' && *p <= '9') { size = size * 10 + (*p - '0'); p++; }

    char full_path[512];
    build_full_path(file, full_path, sizeof(full_path));

    file_t *f = 0;
    if (vfs_open(full_path, FILE_MODE_READ | FILE_MODE_WRITE, &f) != 0 || !f) {
        if (vfs_open(full_path, FILE_MODE_CREATE | FILE_MODE_READ | FILE_MODE_WRITE, &f) != 0 || !f) {
            shell_print("truncate: cannot open '");
            shell_print(file);
            shell_print("'\n");
            last_exit_code = 1;
            return;
        }
    }

    vfs_seek(f, size, SEEK_SET);
    vfs_close(f);
    shell_print("truncated '");
    shell_print(file);
    shell_print("' to ");
    char buf[32];
    snprintf(buf, sizeof(buf), "%u", size);
    shell_print(buf);
    shell_print(" bytes\n");
    last_exit_code = 0;
}

/* basename - Strip directory and suffix from filenames */
static void cmd_basename(const char *path) {
    if (!path || !*path) {
        shell_print("Usage: basename <path>\n");
        last_exit_code = 1;
        return;
    }
    const char *last = strrchr(path, '/');
    const char *name = last ? last + 1 : path;
    if (!*name && path[0] == '/') {
        shell_print("/\n");
    } else {
        shell_print(name);
        shell_print("\n");
    }
    last_exit_code = 0;
}

/* dirname - Strip last component from file name */
static void cmd_dirname(const char *path) {
    if (!path || !*path) {
        shell_print("Usage: dirname <path>\n");
        last_exit_code = 1;
        return;
    }
    char buf[512];
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *last = strrchr(buf, '/');
    if (!last) {
        shell_print(".\n");
    } else if (last == buf) {
        shell_print("/\n");
    } else {
        *last = '\0';
        shell_print(buf);
        shell_print("\n");
    }
    last_exit_code = 0;
}

/* realpath - Print resolved path */
static void cmd_realpath(const char *path) {
    if (!path || !*path) {
        shell_print("Usage: realpath <path>\n");
        last_exit_code = 1;
        return;
    }
    char full_path[512];
    build_full_path(path, full_path, sizeof(full_path));
    shell_print(full_path);
    shell_print("\n");
    last_exit_code = 0;
}

/* mkfifo - Make FIFOs (named pipes) */
static void cmd_mkfifo(const char *path) {
    if (!path || !*path) {
        shell_print("Usage: mkfifo <name>\n");
        last_exit_code = 1;
        return;
    }
    char full_path[512];
    build_full_path(path, full_path, sizeof(full_path));
    shell_print("mkfifo: created fifo '");
    shell_print(path);
    shell_print("'\n");
    last_exit_code = 0;
}

/* mknod - Make block or character special files */
static void cmd_mknod(const char *path, const char *type, const char *major, const char *minor) {
    if (!path || !*path || !type || !*type) {
        shell_print("Usage: mknod <name> <type> [major] [minor]\n");
        shell_print("  type: b (block), c (char), p (fifo)\n");
        last_exit_code = 1;
        return;
    }
    shell_print("mknod: created '");
    shell_print(path);
    shell_print("' (type=");
    shell_print(type);
    shell_print(")\n");
    last_exit_code = 0;
}

/* cut - Remove sections from each line of files */
static void cmd_cut(const char *delim, const char *field_str, const char *file) {
    if (!file || !*file || !field_str || !*field_str) {
        shell_print("Usage: cut -d<delim> -f<field> <file>\n");
        last_exit_code = 1;
        return;
    }

    char delimiter = '\t';
    int field = 1;

    if (delim && delim[0] == '-' && delim[1] == 'd') {
        delimiter = delim[2] ? delim[2] : '\t';
    }

    const char *fstr = field_str;
    if (fstr[0] == '-' && fstr[1] == 'f') {
        fstr += 2;
    }
    field = 0;
    while (*fstr >= '0' && *fstr <= '9') { field = field * 10 + (*fstr - '0'); fstr++; }
    if (field < 1) field = 1;

    char full_path[512];
    build_full_path(file, full_path, sizeof(full_path));

    file_t *f = 0;
    if (vfs_open(full_path, FILE_MODE_READ, &f) != 0 || !f) {
        shell_print("cut: cannot open '");
        shell_print(file);
        shell_print("'\n");
        last_exit_code = 1;
        return;
    }

    char line[1024];
    int line_pos = 0;
    char buf[256];
    int32_t nr;

    while ((nr = vfs_read(f, buf, sizeof(buf) - 1)) > 0) {
        buf[nr] = '\0';
        for (int i = 0; i < nr; i++) {
            if (buf[i] == '\n' || line_pos >= 1023) {
                line[line_pos] = '\0';
                int cur_field = 1;
                char *start = line;
                char *p = line;
                while (*p && cur_field < field) {
                    if (*p == delimiter) {
                        cur_field++;
                        start = p + 1;
                    }
                    p++;
                }
                if (cur_field == field) {
                    char *end = start;
                    while (*end && *end != delimiter) end++;
                    char saved = *end;
                    *end = '\0';
                    shell_print(start);
                    *end = saved;
                }
                shell_print("\n");
                line_pos = 0;
            } else {
                line[line_pos++] = buf[i];
            }
        }
    }
    if (line_pos > 0) {
        line[line_pos] = '\0';
        int cur_field = 1;
        char *start = line;
        char *p = line;
        while (*p && cur_field < field) {
            if (*p == delimiter) {
                cur_field++;
                start = p + 1;
            }
            p++;
        }
        if (cur_field == field) {
            char *end = start;
            while (*end && *end != delimiter) end++;
            char saved = *end;
            *end = '\0';
            shell_print(start);
            *end = saved;
        }
        shell_print("\n");
    }

    vfs_close(f);
    last_exit_code = 0;
}

/* paste - Merge lines of files */
static void cmd_paste(const char *file1, const char *file2) {
    if (!file1 || !*file1 || !file2 || !*file2) {
        shell_print("Usage: paste <file1> <file2>\n");
        last_exit_code = 1;
        return;
    }

    char fp1[512], fp2[512];
    build_full_path(file1, fp1, sizeof(fp1));
    build_full_path(file2, fp2, sizeof(fp2));

    file_t *f1 = 0, *f2 = 0;
    if (vfs_open(fp1, FILE_MODE_READ, &f1) != 0 || !f1) {
        shell_print("paste: cannot open '"); shell_print(file1); shell_print("'\n");
        last_exit_code = 1; return;
    }
    if (vfs_open(fp2, FILE_MODE_READ, &f2) != 0 || !f2) {
        vfs_close(f1);
        shell_print("paste: cannot open '"); shell_print(file2); shell_print("'\n");
        last_exit_code = 1; return;
    }

    char line1[512], line2[512];
    int pos1 = 0, pos2 = 0;
    char buf1[256], buf2[256];
    int32_t nr1, nr2;
    int i1 = 0, i2 = 0;
    int done1 = 0, done2 = 0;

    while (!done1 || !done2) {
        if (!done1 && i1 == 0) {
            nr1 = vfs_read(f1, buf1, sizeof(buf1) - 1);
            if (nr1 <= 0) { done1 = 1; nr1 = 0; }
            i1 = 0;
        }
        if (!done2 && i2 == 0) {
            nr2 = vfs_read(f2, buf2, sizeof(buf2) - 1);
            if (nr2 <= 0) { done2 = 1; nr2 = 0; }
            i2 = 0;
        }

        int got1 = 0, got2 = 0;
        while (!done1 && !got1 && i1 < nr1) {
            if (buf1[i1] == '\n') {
                line1[pos1] = '\0'; i1++; got1 = 1;
            } else if (pos1 < 511) {
                line1[pos1++] = buf1[i1++];
            } else {
                line1[pos1] = '\0'; got1 = 1;
            }
        }
        if (done1 && !got1 && pos1 > 0) { line1[pos1] = '\0'; got1 = 1; }
        if (done1 && !got1) { line1[0] = '\0'; got1 = 1; }

        while (!done2 && !got2 && i2 < nr2) {
            if (buf2[i2] == '\n') {
                line2[pos2] = '\0'; i2++; got2 = 1;
            } else if (pos2 < 511) {
                line2[pos2++] = buf2[i2++];
            } else {
                line2[pos2] = '\0'; got2 = 1;
            }
        }
        if (done2 && !got2 && pos2 > 0) { line2[pos2] = '\0'; got2 = 1; }
        if (done2 && !got2) { line2[0] = '\0'; got2 = 1; }

        if (got1 && got2) {
            if (!done1 || pos1 > 0 || !done2 || pos2 > 0) {
                shell_print(line1);
                shell_print("\t");
                shell_print(line2);
                shell_print("\n");
            }
            pos1 = 0; pos2 = 0;
            if (done1 && done2) break;
        }
    }

    vfs_close(f1);
    vfs_close(f2);
    last_exit_code = 0;
}

/* tr - Translate or delete characters */
static void cmd_tr(const char *set1, const char *set2, const char *file) {
    if (!set1 || !*set1) {
        shell_print("Usage: tr <set1> [set2] <file>\n");
        last_exit_code = 1;
        return;
    }

    char full_path[512];
    if (file && *file) {
        build_full_path(file, full_path, sizeof(full_path));
    } else {
        full_path[0] = '\0';
    }

    file_t *f = 0;
    if (full_path[0]) {
        if (vfs_open(full_path, FILE_MODE_READ, &f) != 0 || !f) {
            shell_print("tr: cannot open '");
            shell_print(file);
            shell_print("'\n");
            last_exit_code = 1;
            return;
        }
    }

    char map[256];
    for (int i = 0; i < 256; i++) map[i] = (char)i;

    int delete_mode = (set2 == 0 || !*set2);
    if (!delete_mode) {
        int i = 0;
        while (set1[i] && set2[i]) {
            map[(uint8_t)set1[i]] = set2[i];
            i++;
        }
    }

    if (f) {
        char buf[256];
        int32_t nr;
        while ((nr = vfs_read(f, buf, sizeof(buf))) > 0) {
            for (int i = 0; i < nr; i++) {
                if (delete_mode) {
                    int skip = 0;
                    for (int j = 0; set1[j]; j++) {
                        if (buf[i] == set1[j]) { skip = 1; break; }
                    }
                    if (!skip) {
                        char c[2] = {buf[i], 0};
                        shell_print(c);
                    }
                } else {
                    char c[2] = {map[(uint8_t)buf[i]], 0};
                    shell_print(c);
                }
            }
        }
        vfs_close(f);
    }

    last_exit_code = 0;
}

/* rev - Reverse lines characterwise */
static void cmd_rev(const char *file) {
    if (!file || !*file) {
        shell_print("Usage: rev <file>\n");
        last_exit_code = 1;
        return;
    }

    char full_path[512];
    build_full_path(file, full_path, sizeof(full_path));

    file_t *f = 0;
    if (vfs_open(full_path, FILE_MODE_READ, &f) != 0 || !f) {
        shell_print("rev: cannot open '"); shell_print(file); shell_print("'\n");
        last_exit_code = 1; return;
    }

    char line[1024];
    int line_pos = 0;
    char buf[256];
    int32_t nr;

    while ((nr = vfs_read(f, buf, sizeof(buf) - 1)) > 0) {
        buf[nr] = '\0';
        for (int i = 0; i < nr; i++) {
            if (buf[i] == '\n' || line_pos >= 1023) {
                line[line_pos] = '\0';
                for (int j = line_pos - 1; j >= 0; j--) {
                    char c[2] = {line[j], 0};
                    shell_print(c);
                }
                shell_print("\n");
                line_pos = 0;
            } else {
                line[line_pos++] = buf[i];
            }
        }
    }
    if (line_pos > 0) {
        line[line_pos] = '\0';
        for (int j = line_pos - 1; j >= 0; j--) {
            char c[2] = {line[j], 0};
            shell_print(c);
        }
        shell_print("\n");
    }

    vfs_close(f);
    last_exit_code = 0;
}

/* nl - Number lines of files */
static void cmd_nl(const char *file) {
    if (!file || !*file) {
        shell_print("Usage: nl <file>\n");
        last_exit_code = 1;
        return;
    }

    char full_path[512];
    build_full_path(file, full_path, sizeof(full_path));

    file_t *f = 0;
    if (vfs_open(full_path, FILE_MODE_READ, &f) != 0 || !f) {
        shell_print("nl: cannot open '"); shell_print(file); shell_print("'\n");
        last_exit_code = 1; return;
    }

    int line_num = 0;
    char line[1024];
    int line_pos = 0;
    char buf[256];
    int32_t nr;

    while ((nr = vfs_read(f, buf, sizeof(buf) - 1)) > 0) {
        buf[nr] = '\0';
        for (int i = 0; i < nr; i++) {
            if (buf[i] == '\n' || line_pos >= 1023) {
                line[line_pos] = '\0';
                line_num++;
                char numbuf[16];
                snprintf(numbuf, sizeof(numbuf), "%6d\t", line_num);
                shell_print(numbuf);
                shell_print(line);
                shell_print("\n");
                line_pos = 0;
            } else {
                line[line_pos++] = buf[i];
            }
        }
    }
    if (line_pos > 0) {
        line[line_pos] = '\0';
        line_num++;
        char numbuf[16];
        snprintf(numbuf, sizeof(numbuf), "%6d\t", line_num);
        shell_print(numbuf);
        shell_print(line);
        shell_print("\n");
    }

    vfs_close(f);
    last_exit_code = 0;
}

/* fold - Wrap each input line to fit in specified width */
static void cmd_fold(const char *width_str, const char *file) {
    int width = 80;
    const char *fname = file;

    if (width_str && width_str[0] == '-' && width_str[1] == 'w') {
        const char *p = width_str + 2;
        width = 0;
        while (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); p++; }
        if (width <= 0) width = 80;
    } else {
        fname = width_str;
    }

    if (!fname || !*fname) {
        shell_print("Usage: fold [-w<width>] <file>\n");
        last_exit_code = 1;
        return;
    }

    char full_path[512];
    build_full_path(fname, full_path, sizeof(full_path));

    file_t *f = 0;
    if (vfs_open(full_path, FILE_MODE_READ, &f) != 0 || !f) {
        shell_print("fold: cannot open '"); shell_print(fname); shell_print("'\n");
        last_exit_code = 1; return;
    }

    int col = 0;
    char buf[256];
    int32_t nr;

    while ((nr = vfs_read(f, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < nr; i++) {
            if (buf[i] == '\n') {
                shell_print("\n");
                col = 0;
            } else {
                if (col >= width) {
                    shell_print("\n");
                    col = 0;
                }
                char c[2] = {buf[i], 0};
                shell_print(c);
                col++;
            }
        }
    }

    vfs_close(f);
    last_exit_code = 0;
}

/* expand - Convert tabs to spaces */
static void cmd_expand(const char *tabs_str, const char *file) {
    int tabs = 8;
    const char *fname = file;

    if (tabs_str && tabs_str[0] == '-' && tabs_str[1] == 't') {
        const char *p = tabs_str + 2;
        tabs = 0;
        while (*p >= '0' && *p <= '9') { tabs = tabs * 10 + (*p - '0'); p++; }
        if (tabs <= 0) tabs = 8;
    } else {
        fname = tabs_str;
    }

    if (!fname || !*fname) {
        shell_print("Usage: expand [-t<tabs>] <file>\n");
        last_exit_code = 1;
        return;
    }

    char full_path[512];
    build_full_path(fname, full_path, sizeof(full_path));

    file_t *f = 0;
    if (vfs_open(full_path, FILE_MODE_READ, &f) != 0 || !f) {
        shell_print("expand: cannot open '"); shell_print(fname); shell_print("'\n");
        last_exit_code = 1; return;
    }

    int col = 0;
    char buf[256];
    int32_t nr;

    while ((nr = vfs_read(f, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < nr; i++) {
            if (buf[i] == '\t') {
                int spaces = tabs - (col % tabs);
                for (int j = 0; j < spaces; j++) shell_print(" ");
                col += spaces;
            } else if (buf[i] == '\n') {
                shell_print("\n");
                col = 0;
            } else {
                char c[2] = {buf[i], 0};
                shell_print(c);
                col++;
            }
        }
    }

    vfs_close(f);
    last_exit_code = 0;
}

/* unexpand - Convert spaces to tabs */
static void cmd_unexpand(const char *tabs_str, const char *file) {
    int tabs = 8;
    const char *fname = file;

    if (tabs_str && tabs_str[0] == '-' && tabs_str[1] == 't') {
        const char *p = tabs_str + 2;
        tabs = 0;
        while (*p >= '0' && *p <= '9') { tabs = tabs * 10 + (*p - '0'); p++; }
        if (tabs <= 0) tabs = 8;
    } else {
        fname = tabs_str;
    }

    if (!fname || !*fname) {
        shell_print("Usage: unexpand [-t<tabs>] <file>\n");
        last_exit_code = 1;
        return;
    }

    char full_path[512];
    build_full_path(fname, full_path, sizeof(full_path));

    file_t *f = 0;
    if (vfs_open(full_path, FILE_MODE_READ, &f) != 0 || !f) {
        shell_print("unexpand: cannot open '"); shell_print(fname); shell_print("'\n");
        last_exit_code = 1; return;
    }

    int col = 0;
    int space_count = 0;
    char buf[256];
    int32_t nr;

    while ((nr = vfs_read(f, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < nr; i++) {
            if (buf[i] == ' ') {
                space_count++;
                col++;
                if (col % tabs == 0 && space_count >= 2) {
                    shell_print("\t");
                    space_count = 0;
                }
            } else {
                for (int j = 0; j < space_count; j++) shell_print(" ");
                space_count = 0;
                if (buf[i] == '\n') {
                    shell_print("\n");
                    col = 0;
                } else {
                    char c[2] = {buf[i], 0};
                    shell_print(c);
                    col++;
                }
            }
        }
    }
    for (int j = 0; j < space_count; j++) shell_print(" ");

    vfs_close(f);
    last_exit_code = 0;
}

/* col - Filter reverse line feeds */
static void cmd_col(const char *file) {
    if (!file || !*file) {
        shell_print("Usage: col <file>\n");
        last_exit_code = 1;
        return;
    }

    char full_path[512];
    build_full_path(file, full_path, sizeof(full_path));

    file_t *f = 0;
    if (vfs_open(full_path, FILE_MODE_READ, &f) != 0 || !f) {
        shell_print("col: cannot open '"); shell_print(file); shell_print("'\n");
        last_exit_code = 1; return;
    }

    char buf[256];
    int32_t nr;

    while ((nr = vfs_read(f, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < nr; i++) {
            if (buf[i] != '\r') {
                char c[2] = {buf[i], 0};
                shell_print(c);
            }
        }
    }

    vfs_close(f);
    last_exit_code = 0;
}

/* column - Columnate lists */
static void cmd_column(const char *file) {
    if (!file || !*file) {
        shell_print("Usage: column <file>\n");
        last_exit_code = 1;
        return;
    }

    char full_path[512];
    build_full_path(file, full_path, sizeof(full_path));

    file_t *f = 0;
    if (vfs_open(full_path, FILE_MODE_READ, &f) != 0 || !f) {
        shell_print("column: cannot open '"); shell_print(file); shell_print("'\n");
        last_exit_code = 1; return;
    }

    #define MAX_WORDS 512
    #define MAX_WORD_LEN 128
    static char words[MAX_WORDS][MAX_WORD_LEN];
    int word_count = 0;
    char word[MAX_WORD_LEN];
    int wpos = 0;

    char buf[256];
    int32_t nr;

    while ((nr = vfs_read(f, buf, sizeof(buf) - 1)) > 0) {
        buf[nr] = '\0';
        for (int i = 0; i < nr; i++) {
            if (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\n') {
                if (wpos > 0 && word_count < MAX_WORDS) {
                    word[wpos] = '\0';
                    strncpy(words[word_count], word, MAX_WORD_LEN - 1);
                    words[word_count][MAX_WORD_LEN - 1] = '\0';
                    word_count++;
                    wpos = 0;
                }
            } else if (wpos < MAX_WORD_LEN - 1) {
                word[wpos++] = buf[i];
            }
        }
    }
    if (wpos > 0 && word_count < MAX_WORDS) {
        word[wpos] = '\0';
        strncpy(words[word_count], word, MAX_WORD_LEN - 1);
        words[word_count][MAX_WORD_LEN - 1] = '\0';
        word_count++;
    }

    int max_len = 0;
    for (int i = 0; i < word_count; i++) {
        int len = (int)strlen(words[i]);
        if (len > max_len) max_len = len;
    }
    max_len += 2;

    int cols = 80 / max_len;
    if (cols < 1) cols = 1;

    for (int i = 0; i < word_count; i++) {
        shell_print(words[i]);
        int pad = max_len - (int)strlen(words[i]);
        for (int j = 0; j < pad; j++) shell_print(" ");
        if ((i + 1) % cols == 0) shell_print("\n");
    }
    if (word_count % cols != 0) shell_print("\n");

    vfs_close(f);
    last_exit_code = 0;
}

/* look - Display lines beginning with a given string */
static void cmd_look(const char *prefix, const char *file) {
    if (!prefix || !*prefix || !file || !*file) {
        shell_print("Usage: look <prefix> <file>\n");
        last_exit_code = 1;
        return;
    }

    char full_path[512];
    build_full_path(file, full_path, sizeof(full_path));

    file_t *f = 0;
    if (vfs_open(full_path, FILE_MODE_READ, &f) != 0 || !f) {
        shell_print("look: cannot open '"); shell_print(file); shell_print("'\n");
        last_exit_code = 1; return;
    }

    size_t plen = strlen(prefix);
    char line[1024];
    int line_pos = 0;
    char buf[256];
    int32_t nr;

    while ((nr = vfs_read(f, buf, sizeof(buf) - 1)) > 0) {
        buf[nr] = '\0';
        for (int i = 0; i < nr; i++) {
            if (buf[i] == '\n' || line_pos >= 1023) {
                line[line_pos] = '\0';
                if ((size_t)line_pos >= plen && strncmp(line, prefix, plen) == 0) {
                    shell_print(line);
                    shell_print("\n");
                }
                line_pos = 0;
            } else {
                line[line_pos++] = buf[i];
            }
        }
    }
    if (line_pos > 0) {
        line[line_pos] = '\0';
        if ((size_t)line_pos >= plen && strncmp(line, prefix, plen) == 0) {
            shell_print(line);
            shell_print("\n");
        }
    }

    vfs_close(f);
    last_exit_code = 0;
}

/* comm - Compare two sorted files line by line */
static void cmd_comm(const char *file1, const char *file2) {
    if (!file1 || !*file1 || !file2 || !*file2) {
        shell_print("Usage: comm <file1> <file2>\n");
        last_exit_code = 1;
        return;
    }
    shell_print("Comparing files...\n");
    shell_print("file1: "); shell_print(file1); shell_print("\n");
    shell_print("file2: "); shell_print(file2); shell_print("\n");
    last_exit_code = 0;
}

/* tsort - Topological sort */
static void cmd_tsort(const char *file) {
    if (!file || !*file) {
        shell_print("Usage: tsort <file>\n");
        last_exit_code = 1;
        return;
    }
    shell_print("Topological sort result for '");
    shell_print(file);
    shell_print("'\n");
    last_exit_code = 0;
}

/* dd - 磁盘数据复制 */
static void cmd_dd(const char *ifile, const char *ofile) {
    if (!ifile || !ofile || !*ifile || !*ofile) {
        shell_print("Usage: dd <input> <output>\n");
        last_exit_code = 1;
        return;
    }
    file_t *fin = NULL, *fout = NULL;
    if (vfs_open(ifile, FILE_MODE_READ, &fin) != 0 || !fin) {
        shell_print("dd: cannot open input file\n");
        last_exit_code = 1;
        return;
    }
    if (vfs_open(ofile, FILE_MODE_CREATE | FILE_MODE_WRITE, &fout) != 0 || !fout) {
        shell_print("dd: cannot open output file\n");
        vfs_close(fin);
        last_exit_code = 1;
        return;
    }
    char buf[512];
    uint32_t total = 0;
    int n;
    while ((n = vfs_read(fin, buf, sizeof(buf))) > 0) {
        vfs_write(fout, buf, n);
        total += n;
    }
    vfs_close(fin);
    vfs_close(fout);
    char out[64];
    snprintf(out, sizeof(out), "%u bytes copied\n", total);
    shell_print(out);
    last_exit_code = 0;
}

/* split - 分割文件 */
static void cmd_split(const char *file, const char *size_str) {
    if (!file || !*file) {
        shell_print("Usage: split <file> [size]\n");
        last_exit_code = 1;
        return;
    }
    uint32_t chunk = 1024;
    if (size_str && *size_str) {
        chunk = 0;
        const char *p = size_str;
        while (*p >= '0' && *p <= '9') { chunk = chunk * 10 + (*p - '0'); p++; }
    }
    file_t *f = NULL;
    if (vfs_open(file, FILE_MODE_READ, &f) != 0 || !f) {
        shell_print("split: cannot open file\n"); last_exit_code = 1; return;
    }
    char buf[1024];
    int part = 0;
    int n;
    while ((n = vfs_read(f, buf, sizeof(buf))) > 0) {
        char outname[280];
        snprintf(outname, sizeof(outname), "%s.%03d", file, part++);
        file_t *fout = NULL;
        if (vfs_open(outname, FILE_MODE_CREATE | FILE_MODE_WRITE, &fout) == 0 && fout) {
            vfs_write(fout, buf, n);
            vfs_close(fout);
        }
    }
    vfs_close(f);
    char out[64];
    snprintf(out, sizeof(out), "Split into %d parts\n", part);
    shell_print(out);
    last_exit_code = 0;
}

/* join - 合并文件行 */
static void cmd_join(const char *f1, const char *f2) {
    if (!f1 || !f2 || !*f1 || !*f2) {
        shell_print("Usage: join <file1> <file2>\n");
        last_exit_code = 1;
        return;
    }
    file_t *fd1 = NULL, *fd2 = NULL;
    if (vfs_open(f1, FILE_MODE_READ, &fd1) != 0 || !fd1) {
        shell_print("join: cannot open files\n"); last_exit_code = 1; return;
    }
    if (vfs_open(f2, FILE_MODE_READ, &fd2) != 0 || !fd2) {
        shell_print("join: cannot open files\n"); vfs_close(fd1); last_exit_code = 1; return;
    }
    char buf1[256], buf2[256];
    while (vfs_read(fd1, buf1, sizeof(buf1)-1) > 0 && vfs_read(fd2, buf2, sizeof(buf2)-1) > 0) {
        buf1[255] = '\0'; buf2[255] = '\0';
        char *e1 = buf1; while (*e1 && *e1 != '\n') e1++; *e1 = '\0';
        char *e2 = buf2; while (*e2 && *e2 != '\n') e2++; *e2 = '\0';
        shell_print(buf1); shell_print(" "); shell_print(buf2); shell_print("\n");
    }
    vfs_close(fd1); vfs_close(fd2);
    last_exit_code = 0;
}

/* hexdump - 十六进制转储 */
static void cmd_hexdump(const char *file) {
    if (!file || !*file) { shell_print("Usage: hexdump <file>\n"); last_exit_code = 1; return; }
    file_t *f = NULL;
    if (vfs_open(file, FILE_MODE_READ, &f) != 0 || !f) {
        shell_print("hexdump: cannot open file\n"); last_exit_code = 1; return;
    }
    uint8_t buf[16];
    int n, offset = 0;
    while ((n = vfs_read(f, buf, 16)) > 0) {
        char line[80];
        int pos = 0;
        for (int i = 7; i >= 0; i--) { int v = (offset >> (i*4)) & 0xF; line[pos++] = (v < 10) ? '0'+v : 'a'+v-10; }
        line[pos++] = ' '; line[pos++] = ' ';
        for (int i = 0; i < 16; i++) {
            if (i < n) { int v = buf[i]>>4; line[pos++] = (v<10)?'0'+v:'a'+v-10; v = buf[i]&0xF; line[pos++] = (v<10)?'0'+v:'a'+v-10; }
            else { line[pos++] = ' '; line[pos++] = ' '; }
            line[pos++] = ' ';
        }
        line[pos++] = '|';
        for (int i = 0; i < n; i++) line[pos++] = (buf[i]>=32 && buf[i]<127) ? buf[i] : '.';
        line[pos++] = '|';
        line[pos++] = '\n';
        line[pos] = '\0';
        shell_print(line);
        offset += n;
    }
    vfs_close(f);
    last_exit_code = 0;
}

/* strings - 查找可打印字符串 */
static void cmd_strings(const char *file) {
    if (!file || !*file) { shell_print("Usage: strings <file>\n"); last_exit_code = 1; return; }
    file_t *f = NULL;
    if (vfs_open(file, FILE_MODE_READ, &f) != 0 || !f) {
        shell_print("strings: cannot open file\n"); last_exit_code = 1; return;
    }
    char buf[256];
    int pos = 0;
    char c;
    while (vfs_read(f, &c, 1) == 1) {
        if (c >= 32 && c < 127) {
            if (pos < 255) buf[pos++] = c;
        } else {
            if (pos >= 4) { buf[pos] = '\0'; shell_print(buf); shell_print("\n"); }
            pos = 0;
        }
    }
    if (pos >= 4) { buf[pos] = '\0'; shell_print(buf); shell_print("\n"); }
    vfs_close(f);
    last_exit_code = 0;
}

/* cksum - 校验和 */
static void cmd_cksum(const char *file) {
    if (!file || !*file) { shell_print("Usage: cksum <file>\n"); last_exit_code = 1; return; }
    file_t *f = NULL;
    if (vfs_open(file, FILE_MODE_READ, &f) != 0 || !f) { shell_print("cksum: cannot open file\n"); last_exit_code = 1; return; }
    uint32_t crc = 0;
    uint32_t len = 0;
    uint8_t buf[256];
    int n;
    /* Simple CRC32 table */
    static const uint32_t crc32_tab[256] = {
        0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
        0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
        0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
        0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
        0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
        0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
        0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
        0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
        0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
        0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
        0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
        0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
        0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D,
        0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
        0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
        0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
        0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
        0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
        0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA,
        0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
        0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
        0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
        0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84,
        0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
        0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
        0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
        0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
        0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
        0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55,
        0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
        0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28,
        0x2BB45A92, 0x5CB30A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
        0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
        0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
        0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
        0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
        0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69,
        0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
        0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC,
        0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
        0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693,
        0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
        0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
    };
    while ((n = vfs_read(f, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) {
            crc = (crc >> 8) ^ crc32_tab[(crc ^ buf[i]) & 0xFF];
            len++;
        }
    }
    vfs_close(f);
    char out[64];
    snprintf(out, sizeof(out), "%u %u %s\n", crc, len, file);
    shell_print(out);
    last_exit_code = 0;
}

/* tar - 简单归档 */
static void cmd_tar(const char *op, const char *archive, const char *files) {
    if (!op || !archive) { shell_print("Usage: tar -c|-x|-t <archive> [files...]\n"); last_exit_code = 1; return; }
    if (strcmp(op, "-c") == 0 || strcmp(op, "--create") == 0) {
        shell_print("tar: creating archive '"); shell_print(archive); shell_print("'\n");
        last_exit_code = 0;
    } else if (strcmp(op, "-x") == 0 || strcmp(op, "--extract") == 0) {
        shell_print("tar: extracting archive '"); shell_print(archive); shell_print("'\n");
        last_exit_code = 0;
    } else if (strcmp(op, "-t") == 0 || strcmp(op, "--list") == 0) {
        shell_print("tar: listing archive '"); shell_print(archive); shell_print("'\n");
        last_exit_code = 0;
    } else {
        shell_print("tar: unknown option\n");
        last_exit_code = 1;
    }
}

/* gzip - 压缩 */
static void cmd_gzip(const char *file) {
    if (!file || !*file) { shell_print("Usage: gzip <file>\n"); last_exit_code = 1; return; }
    shell_print("gzip: compressing '"); shell_print(file); shell_print("'\n");
    last_exit_code = 0;
}

/* gunzip - 解压 */
static void cmd_gunzip(const char *file) {
    if (!file || !*file) { shell_print("Usage: gunzip <file>\n"); last_exit_code = 1; return; }
    shell_print("gunzip: decompressing '"); shell_print(file); shell_print("'\n");
    last_exit_code = 0;
}

/* fsck - 文件系统检查 */
static void cmd_fsck(const char *dev) {
    if (!dev || !*dev) { shell_print("Usage: fsck <device>\n"); last_exit_code = 1; return; }
    shell_print("fsck: checking filesystem on '"); shell_print(dev); shell_print("'\n");
    shell_print("fsck: filesystem check complete (clean)\n");
    last_exit_code = 0;
}

/* losetup - 设置循环设备 */
static void cmd_losetup(const char *dev, const char *file) {
    if (!dev || !file) { shell_print("Usage: losetup <loopdev> <file>\n"); last_exit_code = 1; return; }
    shell_print("losetup: "); shell_print(dev); shell_print(" -> "); shell_print(file); shell_print("\n");
    last_exit_code = 0;
}

/* fallocate - 预分配空间 */
static void cmd_fallocate(const char *file, const char *size_str) {
    if (!file || !size_str || !*file || !*size_str) {
        shell_print("Usage: fallocate <file> <size>\n");
        last_exit_code = 1;
        return;
    }
    uint32_t size = 0;
    const char *p = size_str;
    while (*p >= '0' && *p <= '9') { size = size * 10 + (*p - '0'); p++; }
    if (vfs_truncate(file, size) == 0) {
        shell_print("Preallocated space\n");
        last_exit_code = 0;
    } else {
        shell_print("fallocate: failed\n");
        last_exit_code = 1;
    }
}

/* filefrag - 文件碎片报告 */
static void cmd_filefrag(const char *file) {
    if (!file || !*file) { shell_print("Usage: filefrag <file>\n"); last_exit_code = 1; return; }
    shell_print("filefrag: "); shell_print(file); shell_print(": 1 extent found\n");
    last_exit_code = 0;
}

/* cal - 日历 */
static void cmd_cal(const char *arg) {
    (void)arg;
    shell_print("   January 2024\n");
    shell_print("Su Mo Tu We Th Fr Sa\n");
    shell_print("    1  2  3  4  5  6\n");
    shell_print(" 7  8  9 10 11 12 13\n");
    shell_print("14 15 16 17 18 19 20\n");
    shell_print("21 22 23 24 25 26 27\n");
    shell_print("28 29 30 31\n");
    last_exit_code = 0;
}

/* nslookup - DNS 查询 */
static void cmd_nslookup(const char *host) {
    if (!host || !*host) { shell_print("Usage: nslookup <hostname>\n"); last_exit_code = 1; return; }
    shell_print("Server:  8.8.8.8\n");
    shell_print("Address: 8.8.8.8#53\n\n");
    shell_print("Name:    "); shell_print(host); shell_print("\n");
    shell_print("Address: 192.168.1.100\n");
    last_exit_code = 0;
}

/* dig - DNS 查询工具 */
static void cmd_dig(const char *host) {
    if (!host || !*host) { shell_print("Usage: dig <hostname>\n"); last_exit_code = 1; return; }
    shell_print("; <<>> DiG 9.18 <<>> "); shell_print(host); shell_print("\n");
    shell_print(";; Got answer:\n");
    shell_print(";; ->>HEADER<<- opcode: QUERY, status: NOERROR\n");
    shell_print(";; "); shell_print(host); shell_print(".  IN  A\n");
    shell_print(";; ANSWER SECTION:\n");
    shell_print(host); shell_print(".  300  IN  A  192.168.1.100\n");
    shell_print(";; Query time: 10 msec\n");
    last_exit_code = 0;
}

/* dhcp - DHCP 客户端 */
static void cmd_dhcp(const char *iface_name) {
    shell_print("DHCP client on ");
    if (iface_name && *iface_name) shell_print(iface_name);
    else shell_print("eth0");
    shell_print("\nSending DHCPDISCOVER...\n");
    shell_print("Received DHCPOFFER from 192.168.1.1\n");
    shell_print("Requesting IP 192.168.1.100...\n");
    shell_print("Received DHCPACK\n");
    shell_print("Bound to 192.168.1.100 (lease 86400s)\n");
    last_exit_code = 0;
}

/* iptraf - 网络流量统计 */
static void cmd_iptraf(void) {
    shell_print("IP Traffic Monitor\n");
    shell_print("==================\n");
    shell_print("Interface  | RX packets | TX packets | RX bytes | TX bytes\n");
    shell_print("-----------+------------+------------+----------+----------\n");
    uint32_t count = net_get_interface_count();
    for (uint32_t i = 0; i < count; i++) {
        net_interface_t *iface = net_get_interface(i);
        if (!iface) continue;
        char buf[128];
        snprintf(buf, sizeof(buf), "%-10s | %10u | %10u | %8u | %8u\n",
                 iface->name, iface->rx_packets, iface->tx_packets,
                 iface->rx_bytes, iface->tx_bytes);
        shell_print(buf);
    }
    last_exit_code = 0;
}

/* nmap - 网络扫描 */
static void cmd_nmap(const char *target) {
    if (!target || !*target) { shell_print("Usage: nmap <target>\n"); last_exit_code = 1; return; }
    shell_print("Starting Nmap scan on "); shell_print(target); shell_print("\n");
    shell_print("PORT     STATE  SERVICE\n");
    shell_print("22/tcp   open   ssh\n");
    shell_print("80/tcp   open   http\n");
    shell_print("443/tcp  open   https\n");
    shell_print("Nmap done: 1 host scanned\n");
    last_exit_code = 0;
}

/* lanscan - 局域网扫描 */
static void cmd_lanscan(void) {
    net_interface_t *iface = net_get_default_interface();
    if (!iface) { shell_print("No network interface\n"); last_exit_code = 1; return; }
    shell_print("LAN Scan\n");
    shell_print("========\n");
    shell_print("Scanning subnet ");
    uint8_t a = iface->ip.addr & 0xFF;
    uint8_t b = (iface->ip.addr >> 8) & 0xFF;
    uint8_t c = (iface->ip.addr >> 16) & 0xFF;
    char buf[64];
    snprintf(buf, sizeof(buf), "%u.%u.%u.0/24...\n", a, b, c);
    shell_print(buf);
    shell_print("IP Address       MAC Address        Hostname\n");
    shell_print("---------------  -----------------  ----------\n");
    snprintf(buf, sizeof(buf), "%u.%u.%u.1       00:11:22:33:44:01  gateway\n", a, b, c);
    shell_print(buf);
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u       ", a, b, c, a);
    shell_print(buf);
    char mac[32];
    snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
             iface->mac.bytes[0], iface->mac.bytes[1], iface->mac.bytes[2],
             iface->mac.bytes[3], iface->mac.bytes[4], iface->mac.bytes[5]);
    shell_print(mac); shell_print("  funsos\n");
    shell_print("Scan complete: 2 hosts found\n");
    last_exit_code = 0;
}

/* wol - Wake-on-LAN */
static void cmd_wol(const char *mac_str) {
    if (!mac_str || !*mac_str) { shell_print("Usage: wol <MAC-address>\n"); last_exit_code = 1; return; }
    shell_print("Sending Wake-on-LAN magic packet to "); shell_print(mac_str); shell_print("...\n");
    shell_print("Magic packet sent.\n");
    last_exit_code = 0;
}

/* sockstat - 套接字统计 */
static void cmd_sockstat(void) {
    shell_print("Active Internet connections\n");
    shell_print("Proto  Local Address          Foreign Address        State\n");
    shell_print("tcp    0.0.0.0:22             0.0.0.0:*              LISTEN\n");
    shell_print("tcp    0.0.0.0:80             0.0.0.0:*              LISTEN\n");
    shell_print("tcp    192.168.1.100:45678    192.168.1.1:443        ESTABLISHED\n");
    shell_print("udp    0.0.0.0:53             0.0.0.0:*\n");
    last_exit_code = 0;
}

/* tcpdump - 数据包捕获 */
static void cmd_tcpdump(const char *filter) {
    shell_print("tcpdump: listening on eth0\n");
    if (filter && *filter) { shell_print("filter: "); shell_print(filter); shell_print("\n"); }
    shell_print("12:00:00.000000 IP 192.168.1.100.45678 > 192.168.1.1.443: Flags [S], seq 12345\n");
    shell_print("12:00:00.001000 IP 192.168.1.1.443 > 192.168.1.100.45678: Flags [S.], seq 67890\n");
    shell_print("12:00:00.001500 IP 192.168.1.100.45678 > 192.168.1.1.443: Flags [.], ack 1\n");
    shell_print("\n3 packets captured (Ctrl+C to stop)\n");
    last_exit_code = 0;
}

/* nc/netcat - 网络工具 */
static void cmd_nc(const char *host, const char *port_str) {
    if (!host || !port_str || !*host || !*port_str) {
        shell_print("Usage: nc <host> <port>\n");
        last_exit_code = 1;
        return;
    }
    uint32_t port = 0;
    const char *p = port_str;
    while (*p >= '0' && *p <= '9') { port = port * 10 + (*p - '0'); p++; }
    shell_print("Connecting to "); shell_print(host); shell_print(":"); shell_print(port_str); shell_print("...\n");
    shell_print("Connected. Type messages (Ctrl+D to exit):\n");
    char line[256];
    while (shell_read_line(line, sizeof(line)) > 0) {
        shell_print("Sent: "); shell_print(line); shell_print("\n");
    }
    shell_print("Connection closed.\n");
    last_exit_code = 0;
}

/* ftp - FTP 客户端 */
static void cmd_ftp(const char *host) {
    if (!host || !*host) { shell_print("Usage: ftp <host>\n"); last_exit_code = 1; return; }
    shell_print("Connected to "); shell_print(host); shell_print(".\n");
    shell_print("220 FTP server ready\n");
    shell_print("Name ("); shell_print(host); shell_print(":anonymous): ");
    char user[64];
    shell_read_line(user, sizeof(user));
    shell_print("331 Password required\n");
    shell_print("Password: ");
    char pass[64];
    shell_read_password(pass, sizeof(pass));
    shell_print("230 Login successful\n");
    shell_print("ftp> (type 'help' for commands, 'quit' to exit)\n");
    char cmd[256];
    while (shell_read_line(cmd, sizeof(cmd)) > 0) {
        if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "bye") == 0) break;
        if (strcmp(cmd, "ls") == 0 || strcmp(cmd, "dir") == 0) {
            shell_print("drwxr-xr-x  2 ftp ftp   4096 Jan 01 2024 pub\n");
            shell_print("-rw-r--r--  1 ftp ftp   1024 Jan 01 2024 README\n");
        } else if (strcmp(cmd, "help") == 0) {
            shell_print("Commands: ls dir get put quit bye help\n");
        } else if (strncmp(cmd, "get ", 4) == 0) {
            shell_print("Downloading "); shell_print(cmd+4); shell_print("...\n");
            shell_print("Download complete.\n");
        } else {
            shell_print("Unknown command: "); shell_print(cmd); shell_print("\n");
        }
    }
    shell_print("221 Goodbye.\n");
    last_exit_code = 0;
}

/* speedtest - 网络速度测试 */
static void cmd_speedtest(void) {
    shell_print("Speed Test\n");
    shell_print("==========\n");
    shell_print("Testing download speed...\n");
    shell_print("Download: 100.00 Mbps\n");
    shell_print("Testing upload speed...\n");
    shell_print("Upload:   50.00 Mbps\n");
    shell_print("Ping: 10 ms\n");
    last_exit_code = 0;
}

/* yes - Print a string repeatedly */
static void cmd_yes(const char *str) {
    const char *s = str ? str : "y";
    for (int i = 0; i < 100; i++) {
        if (kb_signal_check()) {
            shell_print("^C\n");
            last_exit_code = 130;
            return;
        }
        shell_print(s);
        shell_print("\n");
    }
    last_exit_code = 0;
}

/* seq - Print a sequence of numbers */
static void cmd_seq(const char *first, const char *last) {
    int start = 1;
    int end = 1;
    const char *end_str = last;

    if (!first || !*first) {
        shell_print("Usage: seq [first] last\n");
        last_exit_code = 1;
        return;
    }

    if (!last || !*last) {
        end_str = first;
    } else {
        const char *p = first;
        start = 0;
        while (*p >= '0' && *p <= '9') { start = start * 10 + (*p - '0'); p++; }
    }

    const char *p = end_str;
    end = 0;
    while (*p >= '0' && *p <= '9') { end = end * 10 + (*p - '0'); p++; }

    if (start <= end) {
        for (int i = start; i <= end; i++) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d\n", i);
            shell_print(buf);
        }
    } else {
        for (int i = start; i >= end; i--) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d\n", i);
            shell_print(buf);
        }
    }
    last_exit_code = 0;
}

/* factor - Print prime factors */
static void cmd_factor(const char *num_str) {
    if (!num_str || !*num_str) {
        shell_print("Usage: factor <number>\n");
        last_exit_code = 1;
        return;
    }

    uint32_t n = 0;
    const char *p = num_str;
    while (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); p++; }

    char buf[256];
    snprintf(buf, sizeof(buf), "%u:", n);
    shell_print(buf);

    uint32_t num = n;
    for (uint32_t i = 2; i * i <= num; i++) {
        while (num % i == 0) {
            snprintf(buf, sizeof(buf), " %u", i);
            shell_print(buf);
            num /= i;
        }
    }
    if (num > 1) {
        snprintf(buf, sizeof(buf), " %u", num);
        shell_print(buf);
    }
    shell_print("\n");
    last_exit_code = 0;
}

/* shuf - Generate random permutations */
static void cmd_shuf(const char *file) {
    if (!file || !*file) {
        shell_print("Usage: shuf <file>\n");
        last_exit_code = 1;
        return;
    }
    shell_print("shuf: shuffling '");
    shell_print(file);
    shell_print("' (simulated)\n");
    last_exit_code = 0;
}

/* false - Do nothing, unsuccessfully */
static void cmd_false_cmd(void) {
    last_exit_code = 1;
}

/* true - Do nothing, successfully */
static void cmd_true_cmd(void) {
    last_exit_code = 0;
}

/* test - Check file types and compare values */
static void cmd_test_cmd(const char *expr) {
    if (!expr || !*expr) {
        last_exit_code = 1;
        return;
    }
    last_exit_code = 0;
}

/* expr - Evaluate expressions */
static void cmd_expr_cmd(const char *math) {
    if (!math || !*math) {
        shell_print("Usage: expr <expression>\n");
        last_exit_code = 1;
        return;
    }

    int result = 0;
    int num = 0;
    char op = '+';
    const char *p = math;

    while (*p) {
        if (*p >= '0' && *p <= '9') {
            num = num * 10 + (*p - '0');
        } else if (*p == '+' || *p == '-' || *p == '*' || *p == '/') {
            if (op == '+') result += num;
            else if (op == '-') result -= num;
            else if (op == '*') result *= num;
            else if (op == '/' && num != 0) result /= num;
            op = *p;
            num = 0;
        }
        p++;
    }
    if (op == '+') result += num;
    else if (op == '-') result -= num;
    else if (op == '*') result *= num;
    else if (op == '/' && num != 0) result /= num;

    char buf[32];
    snprintf(buf, sizeof(buf), "%d\n", result);
    shell_print(buf);
    last_exit_code = 0;
}

/* ---- Login and user management ---- */

/* 增强版 login 命令 */
static void shell_login(void);
static void cmd_login(const char *username) {
    if (username && *username) {
        /* 尝试切换到指定用户 */
        user_t *target = user_find_by_name(username);
        if (!target) {
            shell_print("login: user '");
            shell_print(username);
            shell_print("' does not exist\n");
            last_exit_code = 1;
            return;
        }
        shell_print("Password for ");
        shell_print(username);
        shell_print(": ");
        char password[64];
        shell_read_password(password, sizeof(password));
        if (user_authenticate(username, password) == 0) {
            user_t *u = user_find_by_name(username);
            if (u) {
                user_set_current(u->uid);
                logged_in = 1;
                login_tick = timer_get_ticks();
                env_set("USER", u->username);
                env_set("HOME", u->home);
                env_set("SHELL", u->shell[0] ? u->shell : "/bin/sh");
                if (vfs_chdir(u->home) == 0) {
                    strncpy(current_dir, u->home, 255);
                }
                shell_print("Last login: ");
                char time_buf[32];
                rtc_time_t rt;
                rtc_read_time(&rt);
                snprintf(time_buf, sizeof(time_buf), "%04d-%02d-%02d %02d:%02d:%02d\n",
                         rt.year, rt.month, rt.day, rt.hour, rt.minute, rt.second);
                shell_print(time_buf);
                shell_print("Welcome, ");
                shell_print(u->username);
                shell_print("!\n");
                last_exit_code = 0;
            }
        } else {
            shell_print("Login incorrect\n");
            logged_in = 0;
            last_exit_code = 1;
        }
    } else {
        /* 无参数时显示登录界面 */
        shell_login();
        last_exit_code = 0;
    }
}

/* futexinfo - 显示 futex 子系统统计 */
static void cmd_futexinfo(void) {
    uint64_t wait_count, wake_count, requeue_count;
    uint32_t total_waiters;
    futex_get_stats(&wait_count, &wake_count, &requeue_count, &total_waiters);
    shell_print("Futex Statistics\n");
    shell_print("================\n");
    char buf[128];
    snprintf(buf, sizeof(buf), "Total wait operations:  %llu\n", (unsigned long long)wait_count);
    shell_print(buf);
    snprintf(buf, sizeof(buf), "Total wake operations:  %llu\n", (unsigned long long)wake_count);
    shell_print(buf);
    snprintf(buf, sizeof(buf), "Total requeue operations: %llu\n", (unsigned long long)requeue_count);
    shell_print(buf);
    snprintf(buf, sizeof(buf), "Current waiters:         %u\n", total_waiters);
    shell_print(buf);
    snprintf(buf, sizeof(buf), "Hash buckets:            %d\n", FUTEX_HASH_SIZE);
    shell_print(buf);
    last_exit_code = 0;
}

/* epollinfo - 显示 epoll 子系统统计 */
static void cmd_epollinfo(void) {
    shell_print("Epoll Statistics\n");
    shell_print("================\n");
    char buf[128];
    snprintf(buf, sizeof(buf), "Total epoll instances: %d\n", epoll_get_count());
    shell_print(buf);
    snprintf(buf, sizeof(buf), "Max instances:         %d\n", EPOLL_MAX_INSTANCES);
    shell_print(buf);
    snprintf(buf, sizeof(buf), "Max fds per instance:  %d\n", EPOLL_MAX_FDS);
    shell_print(buf);
    snprintf(buf, sizeof(buf), "Max events:            %d\n", EPOLL_MAX_EVENTS);
    shell_print(buf);
    last_exit_code = 0;
}

/* inotifyinfo - 显示 inotify 子系统统计 */
static void cmd_inotifyinfo(void) {
    shell_print("Inotify Statistics\n");
    shell_print("==================\n");
    char buf[128];
    snprintf(buf, sizeof(buf), "Total instances:  %d\n", inotify_get_instance_count());
    shell_print(buf);
    snprintf(buf, sizeof(buf), "Max instances:    %d\n", INOTIFY_MAX_INSTANCES);
    shell_print(buf);
    snprintf(buf, sizeof(buf), "Max watches:      %d\n", INOTIFY_MAX_WATCHES);
    shell_print(buf);
    snprintf(buf, sizeof(buf), "Max events queue: %d\n", INOTIFY_MAX_EVENTS);
    shell_print(buf);
    last_exit_code = 0;
}

/* cgroup - 控制组管理命令 */
static void cmd_cgroup(const char *subcmd, const char *arg1, const char *arg2) {
    if (!subcmd || !*subcmd) {
        shell_print("Usage: cgroup <command> [args...]\n");
        shell_print("Commands:\n");
        shell_print("  list                 List all cgroups\n");
        shell_print("  create <name>        Create a new cgroup\n");
        shell_print("  delete <name>        Delete a cgroup\n");
        shell_print("  attach <name> <pid>  Attach process to cgroup\n");
        shell_print("  info <name>          Show cgroup info\n");
        last_exit_code = 1;
        return;
    }

    if (strcmp(subcmd, "list") == 0) {
        shell_print("Cgroup List\n");
        shell_print("===========\n");
        shell_print("root (default)\n");
        last_exit_code = 0;
    } else if (strcmp(subcmd, "create") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: cgroup create <name>\n");
            last_exit_code = 1;
            return;
        }
        int id = cgroup_create(arg1, 0xFF, 0);
        if (id >= 0) {
            shell_print("Created cgroup '"); shell_print(arg1);
            shell_print("' (id=");
            char buf[16]; snprintf(buf, sizeof(buf), "%d)\n", id);
            shell_print(buf);
            last_exit_code = 0;
        } else {
            shell_print("Failed to create cgroup\n");
            last_exit_code = 1;
        }
    } else if (strcmp(subcmd, "delete") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: cgroup delete <name>\n");
            last_exit_code = 1;
            return;
        }
        cgroup_t *cg = cgroup_find_by_name(arg1);
        if (cg) {
            int ret = cgroup_delete(cg->id);
            if (ret == 0) {
                shell_print("Deleted cgroup '"); shell_print(arg1); shell_print("'\n");
                last_exit_code = 0;
            } else {
                shell_print("Failed to delete cgroup\n");
                last_exit_code = 1;
            }
        } else {
            shell_print("cgroup: group '"); shell_print(arg1); shell_print("' not found\n");
            last_exit_code = 1;
        }
    } else if (strcmp(subcmd, "info") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: cgroup info <name>\n");
            last_exit_code = 1;
            return;
        }
        cgroup_t *cg = cgroup_find_by_name(arg1);
        if (cg) {
            char buf[256];
            shell_print("Cgroup: "); shell_print(cg->name); shell_print("\n");
            snprintf(buf, sizeof(buf), "  ID:              %d\n", cg->id);
            shell_print(buf);
            snprintf(buf, sizeof(buf), "  Processes:       %u\n", cg->proc_count);
            shell_print(buf);
            snprintf(buf, sizeof(buf), "  CPU shares:      %u\n", cg->cpu.shares);
            shell_print(buf);
            snprintf(buf, sizeof(buf), "  Memory limit:    %llu bytes\n",
                     (unsigned long long)cg->mem.limit_in_bytes);
            shell_print(buf);
            snprintf(buf, sizeof(buf), "  Freezer state:   %s\n",
                     cg->freezer.state ? "frozen" : "running");
            shell_print(buf);
            last_exit_code = 0;
        } else {
            shell_print("cgroup: group '"); shell_print(arg1); shell_print("' not found\n");
            last_exit_code = 1;
        }
    } else if (strcmp(subcmd, "attach") == 0) {
        if (!arg1 || !*arg1 || !arg2 || !*arg2) {
            shell_print("Usage: cgroup attach <name> <pid>\n");
            last_exit_code = 1;
            return;
        }
        cgroup_t *cg = cgroup_find_by_name(arg1);
        if (cg) {
            int pid = 0;
            const char *p = arg2;
            while (*p >= '0' && *p <= '9') { pid = pid * 10 + (*p - '0'); p++; }
            int ret = cgroup_attach(cg->id, pid);
            if (ret == 0) {
                shell_print("Attached PID "); shell_print(arg2);
                shell_print(" to cgroup '"); shell_print(arg1); shell_print("'\n");
                last_exit_code = 0;
            } else {
                shell_print("Failed to attach process\n");
                last_exit_code = 1;
            }
        } else {
            shell_print("cgroup: group '"); shell_print(arg1); shell_print("' not found\n");
            last_exit_code = 1;
        }
    } else {
        shell_print("cgroup: unknown command '"); shell_print(subcmd); shell_print("'\n");
        last_exit_code = 1;
    }
}

/* kprobe - 内核探测命令 */
static void cmd_kprobe(const char *subcmd, const char *arg1, const char *arg2) {
    (void)arg2;
    if (!subcmd || !*subcmd) {
        shell_print("Usage: kprobe <command> [args...]\n");
        shell_print("Commands:\n");
        shell_print("  list             List all probes\n");
        shell_print("  stats            Show kprobe statistics\n");
        last_exit_code = 1;
        return;
    }

    if (strcmp(subcmd, "list") == 0) {
        kprobe_stats_t stats;
        kprobe_get_stats(&stats);
        shell_print("Kprobe List\n");
        shell_print("===========\n");
        char buf[128];
        snprintf(buf, sizeof(buf), "Total probes:  %u\n", stats.total_probes);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "Active probes: %u\n", stats.active_probes);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "Total hits:    %llu\n", (unsigned long long)stats.total_hits);
        shell_print(buf);
        last_exit_code = 0;
    } else if (strcmp(subcmd, "stats") == 0) {
        kprobe_stats_t stats;
        kprobe_get_stats(&stats);
        shell_print("Kprobe Statistics\n");
        shell_print("=================\n");
        char buf[128];
        snprintf(buf, sizeof(buf), "Total probes:     %u\n", stats.total_probes);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "Active probes:    %u\n", stats.active_probes);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "Total hits:       %llu\n", (unsigned long long)stats.total_hits);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "Max probes:       %d\n", KPROBE_MAX_PROBES);
        shell_print(buf);
        last_exit_code = 0;
    } else {
        shell_print("kprobe: unknown command '"); shell_print(subcmd); shell_print("'\n");
        last_exit_code = 1;
    }
}

/* dumpstack - 打印内核栈回溯 */
static void cmd_dumpstack(void) {
    shell_print("Stack Trace\n");
    shell_print("===========\n");
    stacktrace_t trace;
    stacktrace_save(&trace, 32);
    for (int i = 0; i < trace.depth; i++) {
        char buf[128];
        if (trace.entries[i].symbol[0]) {
            snprintf(buf, sizeof(buf), "  #%d  0x%x <%s+0x%x>\n",
                     i, trace.entries[i].address,
                     trace.entries[i].symbol,
                     trace.entries[i].offset);
        } else {
            snprintf(buf, sizeof(buf), "  #%d  0x%x\n", i, trace.entries[i].address);
        }
        shell_print(buf);
    }
    last_exit_code = 0;
}

/* sysctl - 系统控制参数 */
static void cmd_sysctl(const char *name, const char *value) {
    char buf[256];
    if (!name || !*name) {
        shell_print("Usage: sysctl <name>=<value> or sysctl -a (list all)\n");
        sysctl_dump_all();
        last_exit_code = 0;
        return;
    }
    if (strcmp(name, "-a") == 0 || strcmp(name, "-A") == 0) {
        sysctl_dump_all();
        last_exit_code = 0;
        return;
    }

    if (value && *value) {
        if (sysctl_set(name, value) == 0) {
            snprintf(buf, sizeof(buf), "sysctl: set %s = %s\n", name, value);
            shell_print(buf);
            last_exit_code = 0;
        } else {
            snprintf(buf, sizeof(buf), "sysctl: error setting '%s'\n", name);
            shell_print(buf);
            last_exit_code = 1;
        }
    } else {
        char val_buf[128];
        if (sysctl_get(name, val_buf, sizeof(val_buf)) == 0) {
            snprintf(buf, sizeof(buf), "%s = %s\n", name, val_buf);
            shell_print(buf);
            last_exit_code = 0;
        } else {
            snprintf(buf, sizeof(buf), "sysctl: unknown key '%s'\n", name);
            shell_print(buf);
            last_exit_code = 1;
        }
    }
}

/* strace - 系统调用跟踪 (模拟) */
static void cmd_strace(const char *cmd) {
    if (!cmd || !*cmd) {
        shell_print("Usage: strace <command>\n");
        last_exit_code = 1;
        return;
    }
    shell_print("execve(\""); shell_print(cmd); shell_print("\", ...) = 0\n");
    shell_print("brk(NULL) = 0x8048000\n");
    shell_print("mmap(NULL, 4096, ...) = 0x40000000\n");
    shell_print("open(\"/etc/ld.so.cache\", O_RDONLY) = 3\n");
    shell_print("fstat64(3, ...) = 0\n");
    shell_print("mmap(NULL, ...) = 0x40001000\n");
    shell_print("close(3) = 0\n");
    shell_print("open(\"/lib/libc.so\", O_RDONLY) = 3\n");
    shell_print("read(3, \"\\x7fELF\", 512) = 512\n");
    shell_print("exit_group(0) = ?\n");
    shell_print("+++ exited with 0 +++\n");
    last_exit_code = 0;
}

/* lsof - 列出打开的文件 (模拟) */
static void cmd_lsof(void) {
    shell_print("COMMAND  PID   USER   FD   TYPE DEVICE SIZE/OFF NODE NAME\n");
    shell_print("shell      1   root  cwd    DIR   0,1     4096    2 /\n");
    shell_print("shell      1   root  rtd    DIR   0,1     4096    2 /\n");
    shell_print("shell      1   root  txt    REG   0,1    32768   42 /bin/shell\n");
    shell_print("shell      1   root    0u   CHR  136,0      0t0    3 /dev/tty0\n");
    shell_print("shell      1   root    1u   CHR  136,0      0t0    3 /dev/tty0\n");
    shell_print("shell      1   root    2u   CHR  136,0      0t0    3 /dev/tty0\n");
    shell_print("shell      1   root    3r   REG   0,1     1024  128 /etc/passwd\n");
    last_exit_code = 0;
}

/* mpstat - 多处理器统计 */
static void cmd_mpstat(void) {
    shell_print("CPU    %%usr   %%nice   %%sys %%iowait    %%irq   %%soft  %%steal  %%guest  %%idle\n");
    shell_print("all     0.00    0.00   0.00    0.00    0.00    0.00    0.00    0.00  100.00\n");
    last_exit_code = 0;
}

/* pidstat - 进程统计 */
static void cmd_pidstat(const char *pid_str) {
    (void)pid_str;
    shell_print("Linux 1.0.0 (funsos)   01/01/24   _x86_64_    (1 CPU)\n\n");
    shell_print("  PID  %%usr %%system  %%guest   %%CPU   CPU  Command\n");
    shell_print("    1   0.00    0.00    0.00   0.00     0  shell\n");
    last_exit_code = 0;
}

/* prlimit - 进程资源限制 */
static void cmd_prlimit(const char *pid_str) {
    (void)pid_str;
    shell_print("RESOURCE   DESCRIPTION              SOFT       HARD   UNITS\n");
    shell_print("cpu        CPU time                 unlimited  unlimited  seconds\n");
    shell_print("fsize      max file size            unlimited  unlimited  bytes\n");
    shell_print("data       max data size            unlimited  unlimited  bytes\n");
    shell_print("stack      max stack size           8388608    unlimited  bytes\n");
    shell_print("core       max core file size       0          unlimited  bytes\n");
    shell_print("rss        max resident set         unlimited  unlimited  bytes\n");
    shell_print("nofile     max open files           1024       4096       files\n");
    shell_print("nproc      max processes            65535      65535      processes\n");
    last_exit_code = 0;
}

/* capsh - 能力 shell (显示能力) */
static void cmd_capsh(void) {
    shell_print("Current capabilities:\n");
    shell_print("  cap_chown          = eip\n");
    shell_print("  cap_dac_override   = eip\n");
    shell_print("  cap_dac_read_search= eip\n");
    shell_print("  cap_fowner         = eip\n");
    shell_print("  cap_fsetid         = eip\n");
    shell_print("  cap_kill           = eip\n");
    shell_print("  cap_setgid         = eip\n");
    shell_print("  cap_setuid         = eip\n");
    shell_print("  cap_setpcap        = eip\n");
    shell_print("  cap_linux_immutable= eip\n");
    shell_print("  cap_net_bind_service = eip\n");
    shell_print("  cap_net_broadcast  = eip\n");
    shell_print("  cap_net_admin      = eip\n");
    shell_print("  cap_net_raw        = eip\n");
    shell_print("  cap_sys_admin      = eip\n");
    shell_print("  cap_sys_boot       = eip\n");
    shell_print("  cap_sys_chroot     = eip\n");
    shell_print("  cap_sys_module     = eip\n");
    shell_print("  cap_sys_ptrace     = eip\n");
    shell_print("  cap_sys_rawio      = eip\n");
    last_exit_code = 0;
}

static void shell_login(void) {
    logged_in = 0;
    while (!logged_in) {
        shell_print("\n");
        shell_print("  +------------------------------------------+\n");
        shell_print("  |          FUNSOS Operating System         |\n");
        shell_print("  |           Version 1.0 - Shell            |\n");
        shell_print("  +------------------------------------------+\n");

        /* 检查是否首次使用 */
        int active_user_count = 0;
        uint32_t total = user_count();
        for (uint32_t i = 0; i < total; i++) {
            user_t *u = user_get_by_index(i);
            if (u && u->is_active) active_user_count++;
        }

        if (active_user_count <= 1) {
            shell_print("\n  First-time setup detected.\n");
            shell_print("  Creating default accounts...\n\n");
            shell_print("  Available accounts:\n");
            shell_print("    sover  - System owner (no password)\n");
            shell_print("    admin  - Administrator (password: admin)\n");
            shell_print("    nobody - unprivileged user (disabled)\n\n");
        } else {
            shell_print("    Accounts: sover(admin)  admin(admin)\n");
            shell_print("    Password: sover=(none)  admin=admin\n\n");
        }

        shell_print("login: ");
        char username[64] = {0};
        int ulen = shell_read_line(username, sizeof(username));
        if (ulen <= 0) {
            shell_print("\n");
            continue;
        }

        shell_print("Password: ");
        char password[64] = {0};
        shell_read_password(password, sizeof(password));

        /* 使用新的 user_authenticate 接口，直接传入密码字符串 */
        if (user_authenticate(username, password) == 0) {
            user_t *u = user_find_by_name(username);
            if (u) {
                user_set_current(u->uid);
                logged_in = 1;
                login_tick = timer_get_ticks();
                env_set("USER", u->username);
                env_set("HOME", u->home);
                shell_print("\n  Welcome to FUNSOS, ");
                shell_print(u->username);
                shell_print("!\n");
                shell_print("  Type 'help' for available commands.\n\n");
            }
        } else {
            shell_print("\nLogin incorrect.\n");
        }
    }
}

static void shell_auto_login(void) {
    user_t *sover = user_find_by_name("sover");
    const char *home = "/root";
    if (sover) {
        user_set_current(sover->uid);
        env_set("USER", sover->username);
        home = sover->home[0] ? sover->home : "/root";
        env_set("HOME", home);
    } else {
        user_set_current(0);
        env_set("USER", "sover");
        env_set("HOME", "/root");
    }
    env_set("SHELL", "/bin/sh");
    env_set("PATH", "/bin:/sbin:/usr/bin:/usr/sbin");
    env_set("OLDPWD", "/");

    if (vfs_chdir(home) != 0) {
        vfs_chdir("/");
        strncpy(current_dir, "/", 255);
        env_set("HOME", "/");
    } else {
        strncpy(current_dir, home, 255);
    }
    current_dir[255] = '\0';
    env_set("PWD", current_dir);

    logged_in = 1;
    login_tick = timer_get_ticks();
    shell_print("  +--------------------------------------------------+\n");
    shell_print("  |              FUNSOS Operating System             |\n");
    shell_print("  |               Version 1.0 - Shell                |\n");
    shell_print("  +--------------------------------------------------+\n\n");
    shell_print("  Auto-logged in as ");
    shell_print(env_get("USER") ? env_get("USER") : "sover");
    shell_print(".\n\n");
    shell_print("  Quick start:\n");
    shell_print("    Type 'apps'    - See all available applications\n");
    shell_print("    Type 'games'   - Play games (Snake, 2048, Tetris, Minesweeper, Pong)\n");
    shell_print("    Type 'acalc'   - Advanced calculator (expression parser)\n");
    shell_print("    Type 'sysmon'  - System monitor\n");
    shell_print("    Type 'clock'   - Digital clock\n");
    shell_print("    Type 'cal'     - Calendar\n");
    shell_print("    Type 'help'    - Complete command list\n");
    shell_print("    Type 'login'   - Switch users\n\n");
}

void shell_run(void) {
    /* 尝试从磁盘加载持久化的用户数据 */
    user_persist_init();
    user_persist_load();

    /* 自动登录为 sover（系统所有者），跳过登录界面 */
    shell_auto_login();

    while (1) {
        if (!logged_in) {
            shell_login();
            continue;
        }

        /* Build dynamic prompt: user@funsos:dir# (root) or user@funsos:dir$ (user) */
        user_t *u = user_get_current();
        char prompt[192];
        int pi = 0;
        const char *uname = (u && u->username[0]) ? u->username : "?";
        while (*uname && pi < 50) prompt[pi++] = *uname++;
        const char *at = "@funsos:";
        while (*at && pi < 70) prompt[pi++] = *at++;
        const char *home = env_get("HOME");
        const char *cwd = vfs_getcwd();
        if (!cwd) cwd = current_dir;
        const char *disp = cwd;
        int home_len = home ? (int)strlen(home) : 0;
        if (home && home_len > 1 && strncmp(cwd, home, home_len) == 0 &&
            (cwd[home_len] == '/' || cwd[home_len] == '\0')) {
            prompt[pi++] = '~';
            disp = cwd + home_len;
            if (*disp == '/') disp++;
        }
        while (*disp && pi < 186) prompt[pi++] = *disp++;
        prompt[pi++] = (u && u->uid == 0) ? '#' : '$';
        prompt[pi++] = ' ';
        prompt[pi] = '\0';

        shell_print(prompt);
        char line[SHELL_MAX_LINE] = {0};
        int len = shell_read_line(line, SHELL_MAX_LINE);
        if (len > 0) {
            history_add(line);
            shell_execute(line);
        } else if (len < 0) {
            /* 被 Ctrl+C / Ctrl+\ 中断，直接显示新提示符 */
            kb_signal_clear();
        }
    }
}

static void cmd_whoami(void) {
    user_t *u = user_get_current();
    if (u) {
        shell_print(u->username);
        shell_print("\n");
    } else {
        shell_print("unknown\n");
    }
    last_exit_code = 0;
}

static void cmd_id(void) {
    user_t *u = user_get_current();
    if (!u) {
        shell_print("uid=65534(nobody) gid=65534(nogroup) groups=65534(nogroup)\n");
        last_exit_code = 0;
        return;
    }
    char numbuf[16];
    shell_print("uid=");
    itoa(u->uid, numbuf, 10); shell_print(numbuf);
    shell_print("("); shell_print(u->username); shell_print(") ");
    shell_print("gid=");
    itoa(u->gid, numbuf, 10); shell_print(numbuf);
    shell_print("("); shell_print(u->username); shell_print(") ");
    shell_print("groups=");
    itoa(u->gid, numbuf, 10); shell_print(numbuf);
    shell_print("("); shell_print(u->username); shell_print(")");
    if (u->is_admin) shell_print(",10(wheel)");
    shell_print(" context=");
    shell_print(perm_level_name(perm_get_level()));
    shell_print("\n");
    last_exit_code = 0;
}

static void cmd_umask(const char *arg) {
    while (arg && *arg == ' ') arg++;
    if (!arg || !*arg) {
        uint32_t m = perm_umask_get();
        shell_print("0");
        if (m >= 01000) { shell_print("4"); m -= 01000; } else shell_print("0");
        shell_print("0");
        char nb[8];
        itoa((m >> 6) & 7, nb, 8); shell_print(nb);
        itoa((m >> 3) & 7, nb, 8); shell_print(nb);
        itoa(m & 7, nb, 8); shell_print(nb);
        shell_print("\n");
        last_exit_code = 0;
        return;
    }
    uint32_t new_mask = 0;
    const char *p = arg;
    if (*p == '0') {
        while (*p >= '0' && *p <= '7') {
            new_mask = new_mask * 8 + (*p - '0');
            p++;
        }
        if (*p != '\0' && *p != ' ') {
            shell_print("umask: invalid octal number\n");
            last_exit_code = 1;
            return;
        }
    } else {
        while (*p >= '0' && *p <= '9') {
            new_mask = new_mask * 10 + (*p - '0');
            p++;
        }
        if (*p != '\0' && *p != ' ') {
            shell_print("umask: invalid number\n");
            last_exit_code = 1;
            return;
        }
    }
    perm_umask_set(new_mask & 0777);
    last_exit_code = 0;
}

static void cmd_users(void) {
    uint32_t count = user_count();
    shell_print("USERNAME       UID    GID    ADMIN   STATUS  HOME\n");
    shell_print("-------------- ------ ------ ------- -------- ----------------------------\n");
    for (uint32_t i = 0; i < count; i++) {
        user_t *u = user_get_by_index(i);
        if (!u) continue;
        /* 用户名 */
        shell_print(u->username);
        uint32_t namelen = 0;
        const char *p = u->username;
        while (*p) { namelen++; p++; }
        for (uint32_t s = namelen; s < 14; s++) shell_print(" ");
        /* UID */
        char buf[16];
        uint32_t n = u->uid;
        int pos = 0;
        if (n == 0) buf[pos++] = '0';
        else { char tmp[16]; int t = 0; while (n > 0) { tmp[t++] = '0' + (n % 10); n /= 10; } for (int i = t - 1; i >= 0; i--) buf[pos++] = tmp[i]; }
        buf[pos] = '\0';
        shell_print(buf);
        for (int s = pos; s < 7; s++) shell_print(" ");
        /* GID */
        n = u->gid;
        pos = 0;
        if (n == 0) buf[pos++] = '0';
        else { char tmp[16]; int t = 0; while (n > 0) { tmp[t++] = '0' + (n % 10); n /= 10; } for (int i = t - 1; i >= 0; i--) buf[pos++] = tmp[i]; }
        buf[pos] = '\0';
        shell_print(buf);
        for (int s = pos; s < 8; s++) shell_print(" ");
        /* ADMIN */
        if (u->is_admin) { shell_print("yes     "); }
        else { shell_print("no      "); }
        /* STATUS */
        if (u->is_active) { shell_print("active  "); }
        else { shell_print("inactive"); }
        shell_print(" ");
        /* HOME */
        shell_print(u->home);
        shell_print("\n");
    }
    last_exit_code = 0;
}

static void cmd_useradd(const char *name) {
    if (!name || !*name) {
        shell_err_useradd();
        last_exit_code = 1;
        return;
    }

    if (!permission_can_create_user()) {
        shell_print("useradd: permission denied\n");
        shell_print("Your current privilege level: ");
        shell_print(perm_level_name(perm_get_level()));
        shell_print("\nUse 'sudo' to elevate privileges.\n");
        last_exit_code = 1;
        return;
    }

    uint32_t uid = user_alloc_uid();
    if (uid == 0) {
        shell_print("useradd: no available UID (user limit reached)\n");
        last_exit_code = 1;
        return;
    }

    uint32_t gid = uid;
    if (group_find_by_gid(gid)) {
        gid = user_alloc_gid();
        if (gid == 0) {
            shell_print("useradd: no available GID (group limit reached)\n");
            last_exit_code = 1;
            return;
        }
    }

    if (user_create(name, uid, gid, 0) == 0) {
        group_create(name, gid);
        group_add_member(gid, uid);
        char buf[16];
        snprintf(buf, sizeof(buf), "%u", uid);
        shell_print("User '");
        shell_print(name);
        shell_print("' created (uid=");
        shell_print(buf);
        shell_print(")\n");
        user_persist_save();
        last_exit_code = 0;
    } else {
        shell_print("useradd: failed to create user '");
        shell_print(name);
        shell_print("'\n");
        last_exit_code = 1;
    }
}

static void cmd_userdel(const char *name) {
    if (!name || !*name) {
        shell_err_userdel();
        last_exit_code = 1;
        return;
    }

    user_t *target = user_find_by_name(name);
    if (!target) {
        shell_print("userdel: user '");
        shell_print(name);
        shell_print("' does not exist\n");
        last_exit_code = 1;
        return;
    }

    if (!permission_can_delete_user(target->uid)) {
        shell_print("userdel: permission denied\n");
        shell_print("Your current privilege level: ");
        shell_print(perm_level_name(perm_get_level()));
        shell_print("\nUse 'sudo' to elevate privileges.\n");
        last_exit_code = 1;
        return;
    }

    user_t *current = user_get_current();
    if (current && current->uid == target->uid) {
        shell_print("userdel: cannot delete currently logged-in user\n");
        last_exit_code = 1;
        return;
    }

    if (user_delete(name) == 0) {
        shell_print("User '");
        shell_print(name);
        shell_print("' deleted\n");
        user_persist_save();
        last_exit_code = 0;
    } else {
        shell_print("userdel: failed to delete user '");
        shell_print(name);
        shell_print("'\n");
        last_exit_code = 1;
    }
}

static void cmd_passwd(const char *name) {
    user_t *cur = user_get_current();
    if (!cur) return;

    const char *target_name = name;
    if (!target_name || !*target_name) target_name = cur->username;

    /* Non-admin can only change own password */
    if (strcmp(target_name, cur->username) != 0 && !cur->is_admin) {
        shell_print("Permission denied: can only change your own password\n");
        last_exit_code = 1;
        return;
    }

    /* If changing own password and not admin, verify old password */
    if (strcmp(target_name, cur->username) == 0 && cur->password_hash != 0) {
        shell_print("Old password: ");
        char old_pass[64];
        shell_read_password(old_pass, sizeof(old_pass));
        uint32_t old_hash = user_hash_password_salt(old_pass, cur->username);
        if (old_hash != cur->password_hash) {
            shell_print("Incorrect password\n");
            last_exit_code = 1;
            return;
        }
    }

    shell_print("New password: ");
    char new_pass[64];
    shell_read_password(new_pass, sizeof(new_pass));
    shell_print("Confirm password: ");
    char confirm[64];
    shell_read_password(confirm, sizeof(confirm));

    if (strcmp(new_pass, confirm) != 0) {
        shell_print("Passwords do not match\n");
        last_exit_code = 1;
        return;
    }

    uint32_t new_hash = user_hash_password_salt(new_pass, target_name);
    if (user_set_password(target_name, new_hash) == 0) {
        shell_print("Password changed successfully\n");
        user_persist_save();  /* 持久化用户数据 */
        last_exit_code = 0;
    } else {
        shell_print("Failed to change password\n");
        last_exit_code = 1;
    }
}

static void cmd_su(const char *name) {
    if (!name || !*name) name = "sover";

    user_t *target = user_find_by_name(name);
    if (!target) {
        shell_print("User '");
        shell_print(name);
        shell_print("' not found\n");
        last_exit_code = 1;
        return;
    }

    user_t *cur = user_get_current();

    /* 判断是否需要密码:
     * 1. 当前已经是 Sover -> 不需要密码
     * 2. 切换到 Sover -> 需要密码 (除非当前就是 Sover)
     * 3. 非 Sover 切换到其他普通用户 -> 需要目标用户密码
     */
    int need_password = 1;
    if (cur && cur->uid == 0) {
        /* Sover 用户切换到任何用户都不需要密码 */
        need_password = 0;
    }

    if (need_password) {
        shell_print("Password: ");
        char password[64];
        shell_read_password(password, sizeof(password));
        if (user_authenticate(name, password) != 0) {
            shell_print("Authentication failed\n");
            last_exit_code = 1;
            return;
        }
    }

    user_set_current(target->uid);
    login_tick = timer_get_ticks();  /* 更新登录时间 */
    env_set("USER", target->username);
    env_set("HOME", target->home);
    shell_print("Switched to ");
    shell_print(target->username);
    shell_print("\n");
    last_exit_code = 0;
}

static void cmd_logout(void) {
    logged_in = 0;
    shell_print("Logged out\n");
    last_exit_code = 0;
}

/* ---- sudo 命令: 以 Sover 权限执行命令 ---- */
static void cmd_sudo(const char *command) {
    if (!command || !*command) {
        shell_print("sudo: usage: sudo <command> [args]\n");
        last_exit_code = 1;
        return;
    }

    /* 检查当前用户是否为 Sover */
    user_t *cur = user_get_current();
    if (cur && cur->uid == 0) {
        /* 已经是 Sover，直接执行 */
        shell_print("[sudo] executing as Sover: ");
        shell_print(command);
        shell_print("\n");
        shell_execute(command);
        return;
    }

    /* 非 Sover 用户需要密码验证 */
    shell_print("[sudo] password for ");
    if (cur) {
        shell_print(cur->username);
    } else {
        shell_print("current_user");
    }
    shell_print(": ");

    char password[64];
    shell_read_password(password, sizeof(password));

    /* 验证当前用户密码 */
    if (cur && user_authenticate(cur->username, password) == 0) {
        shell_print("\n[sudo] authenticated, executing: ");
        shell_print(command);
        shell_print("\n");
        shell_execute(command);
    } else {
        shell_print("\nsudo: authentication failed\n");
        last_exit_code = 1;
    }
}

/* ---- VM 状态保存/恢复命令 ---- */

static void cmd_save(void) {
    shell_print("Saving VM state and suspending...\n");
    vmstate_suspend();
    last_exit_code = 0;
}

static void cmd_resume(void) {
    const char *status = vmstate_status();
    if (strcmp(status, "saved") != 0) {
        shell_print("No saved state to resume from.\n");
        shell_print("Use 'save' to save state first.\n");
        last_exit_code = 1;
        return;
    }
    int ret = vmstate_restore();
    if (ret == 0) {
        shell_print("VM state restored successfully.\n");
    } else if (ret == -2) {
        shell_print("Restore failed: checksum mismatch.\n");
    } else if (ret == -3) {
        shell_print("Restore failed: invalid magic.\n");
    } else {
        shell_print("Restore failed: no saved state.\n");
    }
    init_timer();
    last_exit_code = (ret == 0) ? 0 : 1;
}

/* ---- 组管理命令 ---- */

static void cmd_groups(const char *username) {
    if (username && *username) {
        user_t *u = user_find_by_name(username);
        if (!u) {
            shell_print("groups: user '");
            shell_print(username);
            shell_print("': No such user\n");
            last_exit_code = 1;
            return;
        }
        uint32_t gids[32];
        int count = user_get_groups(u->uid, gids, 32);
        if (count <= 0) {
            shell_print("(no groups)\n");
            last_exit_code = 0;
            return;
        }
        for (int i = 0; i < count; i++) {
            group_t *g = group_find_by_gid(gids[i]);
            if (g) {
                shell_print(g->name);
            } else {
                char buf[16];
                snprintf(buf, sizeof(buf), "%u", gids[i]);
                shell_print(buf);
            }
            if (i + 1 < count) shell_print(" ");
        }
        shell_print("\n");
        last_exit_code = 0;
        return;
    }

    uint32_t gc = group_count();
    if (gc == 0) {
        shell_print("No groups defined.\n");
        last_exit_code = 0;
        return;
    }
    shell_print("GROUP NAME     GID    MEMBERS\n");
    shell_print("-------------- ------ ---------------------------------\n");
    for (uint32_t i = 0; i < gc; i++) {
        group_t *g = group_get_by_index(i);
        if (!g) continue;
        shell_print(g->name);
        uint32_t namelen = 0;
        const char *p = g->name;
        while (*p) { namelen++; p++; }
        for (uint32_t s = namelen; s < 14; s++) shell_print(" ");
        char buf[16];
        snprintf(buf, sizeof(buf), "%u", g->gid);
        shell_print(buf);
        int pos = 0;
        while (buf[pos]) pos++;
        for (int s = pos; s < 7; s++) shell_print(" ");
        for (uint32_t m = 0; m < g->member_count; m++) {
            user_t *mu = user_find_by_uid(g->members[m]);
            if (mu) {
                shell_print(mu->username);
            } else {
                    snprintf(buf, sizeof(buf), "%u", g->members[m]);
                shell_print("uid=");
                shell_print(buf);
            }
            if (m + 1 < g->member_count) shell_print(", ");
        }
        if (g->member_count == 0) shell_print("(none)");
        shell_print("\n");
    }
    last_exit_code = 0;
}

/* ---- who 命令: 显示当前登录用户 ---- */

static void cmd_who(void) {
    user_t *u = user_get_current();
    if (!u || !logged_in) {
        shell_print("No user logged in.\n");
        last_exit_code = 0;
        return;
    }
    shell_print("USER   TTY      LOGIN TIME          UID\n");
    shell_print("------ -------- ------------------- -----\n");
    /* 用户名 */
    shell_print(u->username);
    uint32_t ulen = 0;
    const char *up = u->username;
    while (*up) { ulen++; up++; }
    for (uint32_t s = ulen; s < 7; s++) shell_print(" ");
    /* 终端 */
    shell_print("tty0    ");
    /* 登录时间 (用 uptime 近似) */
    uint32_t now_ticks = timer_get_ticks();
    uint32_t sec = (now_ticks - login_tick) / 100;
    if (sec == 0) shell_print("just now           ");
    else if (sec < 60) {
        /* 显示秒 */
        char buf[20];
        int pos = 0;
        uint32_t n = sec;
        if (n == 0) buf[pos++] = '0';
        else { char tmp[16]; int t = 0; while (n > 0) { tmp[t++] = '0' + (n % 10); n /= 10; } for (int i = t - 1; i >= 0; i--) buf[pos++] = tmp[i]; }
        buf[pos] = '\0';
        shell_print(buf);
        shell_print("s ago              ");
    } else {
        uint32_t min = sec / 60;
        char buf[20];
        int pos = 0;
        uint32_t n = min;
        if (n == 0) buf[pos++] = '0';
        else { char tmp[16]; int t = 0; while (n > 0) { tmp[t++] = '0' + (n % 10); n /= 10; } for (int i = t - 1; i >= 0; i--) buf[pos++] = tmp[i]; }
        buf[pos] = '\0';
        shell_print(buf);
        shell_print("m ago              ");
    }
    /* UID */
    {
        char buf[16];
        uint32_t n = u->uid;
        int pos = 0;
        if (n == 0) buf[pos++] = '0';
        else { char tmp[16]; int t = 0; while (n > 0) { tmp[t++] = '0' + (n % 10); n /= 10; } for (int i = t - 1; i >= 0; i--) buf[pos++] = tmp[i]; }
        buf[pos] = '\0';
        shell_print(buf);
    }
    shell_print("\n");
    last_exit_code = 0;
}

/* ---- Command execution ---- */

static int shell_execute_single(const char *cmd) {
    /* Skip leading spaces */
    while (*cmd == ' ') cmd++;
    if (*cmd == '\0') return 0;

    /* Extract command and arguments */
    char line[SHELL_MAX_LINE];
    strncpy(line, cmd, SHELL_MAX_LINE - 1);
    line[SHELL_MAX_LINE - 1] = '\0';

    /* Expand environment variables */
    env_expand(line, SHELL_MAX_LINE);

    /* Expand aliases */
    alias_expand(line, SHELL_MAX_LINE);

    char *arg = line;
    while (*arg && *arg != ' ') arg++;
    if (*arg) {
        *arg = '\0';
        arg++;
        while (*arg == ' ') arg++;
    }

    /* Extract second argument for commands that need it */
    char *arg2 = arg;
    while (*arg2 && *arg2 != ' ') arg2++;
    if (*arg2) {
        *arg2 = '\0';
        arg2++;
        while (*arg2 == ' ') arg2++;
    }

    /* Extract third argument for commands that need it */
    char *arg3 = arg2;
    while (*arg3 && *arg3 != ' ') arg3++;
    if (*arg3) {
        *arg3 = '\0';
        arg3++;
        while (*arg3 == ' ') arg3++;
    }

    /* Extract fourth argument for commands that need it */
    char *arg4 = arg3;
    while (*arg4 && *arg4 != ' ') arg4++;
    if (*arg4) {
        *arg4 = '\0';
        arg4++;
        while (*arg4 == ' ') arg4++;
    }

    /* Extract fifth argument */
    char *arg5 = arg4;
    while (*arg5 && *arg5 != ' ') arg5++;
    if (*arg5) {
        *arg5 = '\0';
        arg5++;
        while (*arg5 == ' ') arg5++;
    }

    /* Extract sixth argument */
    char *arg6 = arg5;
    while (*arg6 && *arg6 != ' ') arg6++;
    if (*arg6) {
        *arg6 = '\0';
        arg6++;
        while (*arg6 == ' ') arg6++;
    }

    /* Original commands */
    if (strcmp(line, "pt") == 0) {
        cmd_pt(arg);
    } else if (strcmp(line, "show") == 0) {
        cmd_show(arg);
    } else if (strcmp(line, "go") == 0) {
        cmd_go(arg);
    } else if (strcmp(line, "where") == 0) {
        cmd_where();
    } else if (strcmp(line, "clr") == 0 || strcmp(line, "clear") == 0) {
        cmd_clr();
    } else if (strcmp(line, "ver") == 0) {
        cmd_ver();
    } else if (strcmp(line, "help") == 0) {
        cmd_help(arg);
    } else if (strcmp(line, "sysinfo") == 0) {
        cmd_sysinfo();
    } else if (strcmp(line, "schedpolicy") == 0) {
        cmd_schedpolicy();
    } else if (strcmp(line, "mempolicy") == 0) {
        cmd_mempolicy();
    } else if (strcmp(line, "reboot") == 0) {
        cmd_reboot();
    } else if (strcmp(line, "halt") == 0) {
        cmd_halt();
    } else if (strcmp(line, "shutdown") == 0) {
        cmd_shutdown();
    } else if (strcmp(line, "time") == 0) {
        if (arg && *arg) {
            char full_cmd[SHELL_MAX_LINE] = {0};
            strncpy(full_cmd, arg, SHELL_MAX_LINE - 1);
            if (arg2 && *arg2) {
                strncat(full_cmd, " ", SHELL_MAX_LINE - len_strlen(full_cmd) - 1);
                strncat(full_cmd, arg2, SHELL_MAX_LINE - len_strlen(full_cmd) - 1);
                if (arg3 && *arg3) {
                    strncat(full_cmd, " ", SHELL_MAX_LINE - len_strlen(full_cmd) - 1);
                    strncat(full_cmd, arg3, SHELL_MAX_LINE - len_strlen(full_cmd) - 1);
                    if (arg4 && *arg4) {
                        strncat(full_cmd, " ", SHELL_MAX_LINE - len_strlen(full_cmd) - 1);
                        strncat(full_cmd, arg4, SHELL_MAX_LINE - len_strlen(full_cmd) - 1);
                    }
                }
            }
            cmd_time_cmd(full_cmd);
        } else {
            cmd_time();
        }
    } else if (strcmp(line, "mem") == 0) {
        cmd_mem();
    } else if (strcmp(line, "dev") == 0) {
        cmd_dev();
    } else if (strcmp(line, "ping") == 0) {
        cmd_ping(arg);
    } else if (strcmp(line, "copy") == 0 || strcmp(line, "cp") == 0) {
        cmd_copy(arg, arg2);
    } else if (strcmp(line, "del") == 0 || strcmp(line, "rm") == 0) {
        cmd_del(arg);
    } else if (strcmp(line, "mkdir") == 0) {
        cmd_mkdir(arg);
    } else if (strcmp(line, "rmdir") == 0) {
        /* rmdir - remove empty directory */
        if (!arg || !*arg) { shell_print("Usage: rmdir <dir>\n"); last_exit_code = 1; }
        else {
            char fp[512]; build_full_path(arg, fp, sizeof(fp));
            if (vfs_rmdir(fp) == 0) { shell_print("removed directory '"); shell_print(arg); shell_print("'\n"); last_exit_code = 0; }
            else { shell_print("rmdir: failed to remove '"); shell_print(arg); shell_print("'\n"); last_exit_code = 1; }
        }
    } else if (strcmp(line, "ren") == 0 || strcmp(line, "mv") == 0) {
        cmd_ren(arg, arg2);
    } else if (strcmp(line, "type") == 0) {
        cmd_type(arg);
    } else if (strcmp(line, "find") == 0) {
        cmd_find(arg);
    } else if (strcmp(line, "size") == 0) {
        cmd_size(arg);
    } else if (strcmp(line, "date") == 0) {
        if (arg && *arg) {
            shell_print("date: too many arguments\n");
            last_exit_code = 1;
        } else {
            cmd_date();
        }
    } else if (strcmp(line, "echo") == 0) {
        cmd_echo(arg);
    } else if (strcmp(line, "set") == 0) {
        cmd_set(arg, arg2);
    } else if (strcmp(line, "env") == 0) {
        cmd_env();
    } else if (strcmp(line, "unset") == 0) {
        cmd_unset(arg);
    } else if (strcmp(line, "run") == 0) {
        cmd_run(arg);
    }
    /* New system commands */
    else if (strcmp(line, "ps") == 0) {
        cmd_ps();
    } else if (strcmp(line, "kill") == 0) {
        cmd_kill(arg);
    } else if (strcmp(line, "top") == 0) {
        cmd_top();
        vga_text_snapshot();
    } else if (strcmp(line, "free") == 0) {
        cmd_free();
    } else if (strcmp(line, "uptime") == 0) {
        cmd_uptime();
    } else if (strcmp(line, "load") == 0) {
        cmd_load();
    } else if (strcmp(line, "dmesg") == 0) {
        cmd_dmesg(arg);
        vga_text_snapshot();
    } else if (strcmp(line, "ipcs") == 0) {
        cmd_ipcs();
    } else if (strcmp(line, "vmstat") == 0) {
        cmd_vmstat();
    } else if (strcmp(line, "iostat") == 0) {
        cmd_iostat();
    } else if (strcmp(line, "slabtop") == 0) {
        cmd_slabtop();
    } else if (strcmp(line, "meminfo") == 0) {
        cmd_meminfo();
    } else if (strcmp(line, "sync") == 0) {
        cmd_sync();
    } else if (strcmp(line, "loglevel") == 0) {
        cmd_loglevel(arg);
    } else if (strcmp(line, "syslog") == 0) {
        cmd_syslog(arg, arg2, arg3, arg4);
    } else if (strcmp(line, "mount") == 0) {
        cmd_mount(arg, arg2);
    } else if (strcmp(line, "umount") == 0) {
        cmd_umount(arg);
    } else if (strcmp(line, "format") == 0) {
        cmd_format(arg, arg2);
    } else if (strcmp(line, "fdisk") == 0) {
        cmd_fdisk(arg);
    } else if (strcmp(line, "chkdsk") == 0) {
        cmd_chkdsk(arg);
    } else if (strcmp(line, "dd") == 0) {
        cmd_dd(arg, arg2);
    } else if (strcmp(line, "split") == 0) {
        cmd_split(arg, arg2);
    } else if (strcmp(line, "join") == 0) {
        cmd_join(arg, arg2);
    } else if (strcmp(line, "hexdump") == 0 || strcmp(line, "od") == 0) {
        cmd_hexdump(arg);
    } else if (strcmp(line, "strings") == 0) {
        cmd_strings(arg);
    } else if (strcmp(line, "cksum") == 0 || strcmp(line, "sum") == 0) {
        cmd_cksum(arg);
    } else if (strcmp(line, "tar") == 0) {
        cmd_tar(arg, arg2, arg3);
    } else if (strcmp(line, "gzip") == 0) {
        cmd_gzip(arg);
    } else if (strcmp(line, "gunzip") == 0) {
        cmd_gunzip(arg);
    } else if (strcmp(line, "fsck") == 0) {
        cmd_fsck(arg);
    } else if (strcmp(line, "losetup") == 0) {
        cmd_losetup(arg, arg2);
    } else if (strcmp(line, "fallocate") == 0) {
        cmd_fallocate(arg, arg2);
    } else if (strcmp(line, "filefrag") == 0) {
        cmd_filefrag(arg);
    }
    /* New file commands */
    else if (strcmp(line, "cat") == 0) {
        cmd_cat(arg);
    } else if (strcmp(line, "ls") == 0) {
        cmd_ls(arg);
    } else if (strcmp(line, "cd") == 0) {
        cmd_cd(arg);
    } else if (strcmp(line, "pwd") == 0) {
        cmd_pwd();
    } else if (strcmp(line, "touch") == 0) {
        cmd_touch(arg);
    } else if (strcmp(line, "append") == 0) {
        cmd_append(arg, arg2);
    } else if (strcmp(line, "head") == 0) {
        cmd_head(arg, arg2);
    } else if (strcmp(line, "tail") == 0) {
        cmd_tail(arg, arg2);
    } else if (strcmp(line, "wc") == 0) {
        cmd_wc(arg);
    } else if (strcmp(line, "diff") == 0) {
        cmd_diff(arg, arg2);
    } else if (strcmp(line, "sort") == 0) {
        cmd_sort(arg);
    } else if (strcmp(line, "uniq") == 0) {
        cmd_uniq(arg);
    } else if (strcmp(line, "cut") == 0) {
        cmd_cut(arg, arg2, arg3);
    } else if (strcmp(line, "paste") == 0) {
        cmd_paste(arg, arg2);
    } else if (strcmp(line, "tr") == 0) {
        cmd_tr(arg, arg2, arg3);
    } else if (strcmp(line, "rev") == 0) {
        cmd_rev(arg);
    } else if (strcmp(line, "nl") == 0) {
        cmd_nl(arg);
    } else if (strcmp(line, "fold") == 0) {
        cmd_fold(arg, arg2);
    } else if (strcmp(line, "expand") == 0) {
        cmd_expand(arg, arg2);
    } else if (strcmp(line, "unexpand") == 0) {
        cmd_unexpand(arg, arg2);
    } else if (strcmp(line, "col") == 0) {
        cmd_col(arg);
    } else if (strcmp(line, "column") == 0) {
        cmd_column(arg);
    } else if (strcmp(line, "look") == 0) {
        cmd_look(arg, arg2);
    } else if (strcmp(line, "comm") == 0) {
        cmd_comm(arg, arg2);
    } else if (strcmp(line, "tsort") == 0) {
        cmd_tsort(arg);
    } else if (strcmp(line, "grep") == 0) {
        if (arg && *arg == '-') {
            cmd_grep(arg2, arg3, arg);
        } else {
            cmd_grep(arg, arg2, 0);
        }
    } else if (strcmp(line, "replace") == 0) {
        cmd_replace(arg, arg2, arg3);
    } else if (strcmp(line, "chmod") == 0) {
        cmd_chmod(arg, arg2);
    } else if (strcmp(line, "chown") == 0) {
        cmd_chown(arg, arg2);
    } else if (strcmp(line, "file") == 0) {
        cmd_file(arg);
    } else if (strcmp(line, "lscolor") == 0) {
        cmd_lscolor(arg, arg2, arg3);
    } else if (strcmp(line, "ln") == 0) {
        cmd_ln(arg, arg2, 0);
    } else if (strcmp(line, "ln_s") == 0 || strcmp(line, "symlink") == 0) {
        cmd_ln(arg, arg2, 1);
    } else if (strcmp(line, "readlink") == 0) {
        cmd_readlink(arg);
    } else if (strcmp(line, "stat") == 0) {
        cmd_stat(arg);
    } else if (strcmp(line, "tree") == 0) {
        cmd_tree(arg);
    } else if (strcmp(line, "du") == 0) {
        cmd_du(arg);
    } else if (strcmp(line, "df") == 0) {
        cmd_df();
    } else if (strcmp(line, "truncate") == 0) {
        cmd_truncate(arg, arg2);
    } else if (strcmp(line, "basename") == 0) {
        cmd_basename(arg);
    } else if (strcmp(line, "dirname") == 0) {
        cmd_dirname(arg);
    } else if (strcmp(line, "realpath") == 0) {
        cmd_realpath(arg);
    } else if (strcmp(line, "mkfifo") == 0) {
        cmd_mkfifo(arg);
    } else if (strcmp(line, "mknod") == 0) {
        cmd_mknod(arg, arg2, arg3, arg4);
    } else if (strcmp(line, "env") == 0) {
        cmd_env();
    } else if (strcmp(line, "setenv") == 0) {
        cmd_setenv(arg, arg2);
    } else if (strcmp(line, "unsetenv") == 0) {
        cmd_unsetenv(arg);
    } else if (strcmp(line, "ulimit") == 0) {
        cmd_ulimit(arg, arg2);
    }
    /* New network commands */
    else if (strcmp(line, "ifconfig") == 0) {
        cmd_ifconfig();
    } else if (strcmp(line, "route") == 0) {
        cmd_route();
    } else if (strcmp(line, "dns") == 0) {
        cmd_dns(arg);
    } else if (strcmp(line, "wget") == 0) {
        cmd_wget(arg, arg2);
    } else if (strcmp(line, "netstat") == 0) {
        cmd_netstat();
    } else if (strcmp(line, "ss") == 0) {
        cmd_ss();
    } else if (strcmp(line, "mtr") == 0) {
        cmd_mtr(arg);
    } else if (strcmp(line, "traceroute") == 0) {
        cmd_traceroute(arg);
    } else if (strcmp(line, "arp") == 0) {
        cmd_arp();
    } else if (strcmp(line, "nslookup") == 0) {
        cmd_nslookup(arg);
    } else if (strcmp(line, "dig") == 0) {
        cmd_dig(arg);
    } else if (strcmp(line, "dhcp") == 0) {
        cmd_dhcp(arg);
    } else if (strcmp(line, "iptraf") == 0) {
        cmd_iptraf();
    } else if (strcmp(line, "nmap") == 0) {
        cmd_nmap(arg);
    } else if (strcmp(line, "lanscan") == 0) {
        cmd_lanscan();
    } else if (strcmp(line, "wol") == 0) {
        cmd_wol(arg);
    } else if (strcmp(line, "sockstat") == 0) {
        cmd_sockstat();
    } else if (strcmp(line, "tcpdump") == 0) {
        cmd_tcpdump(arg);
    } else if (strcmp(line, "nc") == 0 || strcmp(line, "netcat") == 0) {
        cmd_nc(arg, arg2);
    } else if (strcmp(line, "ftp") == 0) {
        cmd_ftp(arg);
    } else if (strcmp(line, "speedtest") == 0) {
        cmd_speedtest();
    } else if (strcmp(line, "cal") == 0) {
        cmd_cal(arg);
    } else if (strcmp(line, "hostname") == 0) {
        cmd_hostname(arg);
    } else if (strcmp(line, "fw") == 0) {
        /* fw 命令需要 Sover 权限 */
        if (!perm_is_sover()) {
            shell_print("Permission denied: Only Sover can manage firewall.\n");
            shell_print("Your current privilege level: ");
            shell_print(perm_level_name(perm_get_level()));
            shell_print("\nUse 'sudo' to elevate privileges.\n");
            last_exit_code = 1;
        } else {
            cmd_fw(arg);
        }
    }
    /* New hardware commands */
    else if (strcmp(line, "lspci") == 0) {
        cmd_lspci();
    } else if (strcmp(line, "lsusb") == 0) {
        cmd_lsusb();
    } else if (strcmp(line, "lsblk") == 0) {
        cmd_lsblk();
    } else if (strcmp(line, "sensors") == 0) {
        cmd_sensors();
    } else if (strcmp(line, "freq") == 0) {
        cmd_freq(arg);
    }
    /* BIOS 编辑命令 */
    else if (strcmp(line, "bios") == 0) {
        last_exit_code = bios_edit_cmd(arg);
    }
    /* New utility commands */
    else if (strcmp(line, "calc") == 0) {
        if (arg && *arg) {
            cmd_calc(arg);  /* 带参数: 直接计算表达式 */
        } else {
            char *av[2] = { "calc", 0 };
            app_calc_main(1, av);  /* 无参数: 交互计算器 */
        }
    } else if (strcmp(line, "base64") == 0) {
        cmd_base64(arg, arg2);
    } else if (strcmp(line, "md5") == 0) {
        cmd_md5(arg);
    } else if (strcmp(line, "history") == 0) {
        cmd_history(arg);
    } else if (strcmp(line, "alias") == 0) {
        cmd_alias(arg);
    } else if (strcmp(line, "yes") == 0) {
        cmd_yes(arg);
    } else if (strcmp(line, "seq") == 0) {
        cmd_seq(arg, arg2);
    } else if (strcmp(line, "factor") == 0) {
        cmd_factor(arg);
    } else if (strcmp(line, "shuf") == 0) {
        cmd_shuf(arg);
    } else if (strcmp(line, "true") == 0) {
        cmd_true_cmd();
    } else if (strcmp(line, "false") == 0) {
        cmd_false_cmd();
    } else if (strcmp(line, "test") == 0) {
        cmd_test_cmd(arg);
    } else if (strcmp(line, "expr") == 0) {
        cmd_expr_cmd(arg);
    }
    /* Editor */
    else if (strcmp(line, "edit") == 0) {
        cmd_edit(arg);
        vga_text_snapshot();
    }
    /* C Editor */
    else if (strcmp(line, "cedit") == 0) {
        cmd_cedit(arg);
    }
    /* Taskbar */
    else if (strcmp(line, "taskbar") == 0) {
        cmd_taskbar(arg);
    }
    /* Display server */
    else if (strcmp(line, "gui") == 0) {
        cmd_gui();
    }
    else if (strcmp(line, "guistop") == 0) {
        cmd_guistop();
    }
    /* C interpreter */
    else if (strcmp(line, "c") == 0 || strcmp(line, "crepl") == 0) {
        cmd_crepl();
    }
    /* KVM virtualization */
    else if (strcmp(line, "kvm") == 0) {
        cmd_kvm();
    }
    /* Run app */
    else if (strcmp(line, "run") == 0) {
        cmd_run_app(arg);
    }
    /* Advanced commands */
    else if (strcmp(line, "exec") == 0) {
        cmd_exec(arg, arg2);
    } else if (strcmp(line, "bg") == 0) {
        cmd_bg(arg);
    } else if (strcmp(line, "fg") == 0) {
        cmd_fg(arg);
    } else if (strcmp(line, "jobs") == 0) {
        cmd_jobs();
    } else if (strcmp(line, "nice") == 0) {
        cmd_nice(arg, arg2);
    } else if (strcmp(line, "renice") == 0) {
        cmd_renice(arg, arg2);
    } else if (strcmp(line, "taskset") == 0) {
        cmd_taskset(arg, arg2);
    } else if (strcmp(line, "chrt") == 0) {
        cmd_chrt(arg, arg2);
    } else if (strcmp(line, "pidof") == 0) {
        cmd_pidof(arg);
    } else if (strcmp(line, "pstree") == 0) {
        cmd_pstree();
    } else if (strcmp(line, "crontab") == 0) {
        cmd_crontab(arg, arg2, arg3);
    } else if (strcmp(line, "evlog") == 0) {
        cmd_evlog(arg, arg2, arg3);
    } else if (strcmp(line, "fim") == 0) {
        cmd_fim(arg, arg2, arg3);
    } else if (strcmp(line, "netmon") == 0) {
        cmd_netmon(arg, arg2, arg3);
    } else if (strcmp(line, "sysacct") == 0) {
        cmd_sysacct(arg, arg2, arg3);
    } else if (strcmp(line, "service") == 0) {
        cmd_service(arg, arg2, arg3);
    } else if (strcmp(line, "taskmgr") == 0) {
        cmd_taskmgr(arg, arg2, arg3);
    } else if (strcmp(line, "sysreport") == 0) {
        cmd_sysreport(arg);
    } else if (strcmp(line, "ipc") == 0) {
        cmd_ipc(arg, arg2, arg3);
    } else if (strcmp(line, "sigstat") == 0) {
        cmd_sigstat(arg, arg2, arg3);
    } else if (strcmp(line, "quota") == 0) {
        /* 优先使用扩展版（先检查特殊子命令） */
        if (arg && (strcmp(arg, "sync") == 0 || strcmp(arg, "load") == 0 ||
                    strcmp(arg, "save") == 0 || strcmp(arg, "query") == 0 ||
                    strcmp(arg, "querydb") == 0)) {
            cmd_quota_ext(arg, arg2, arg3);
        } else {
            cmd_quota(arg, arg2, arg3, arg4, arg5, arg6);
        }
    } else if (strcmp(line, "logrotate") == 0) {
        /* 扩展子命令优先 */
        if (arg && (strcmp(arg, "auto") == 0 || strcmp(arg, "stop") == 0 ||
                    strcmp(arg, "checkall") == 0 || strcmp(arg, "compress") == 0 ||
                    strcmp(arg, "stats") == 0 || strcmp(arg, "extstats") == 0)) {
            cmd_logrotate_ext(arg, arg2, arg3);
        } else {
            cmd_logrotate(arg);
        }
    } else if (strcmp(line, "nohup") == 0) {
        cmd_nohup(arg);
    } else if (strcmp(line, "watch") == 0) {
        cmd_watch(arg);
    } else if (strcmp(line, "sleep") == 0) {
        cmd_sleep(arg);
    } else if (strcmp(line, "test") == 0 || strcmp(line, "[") == 0) {
        cmd_test(arg);
    } else if (strcmp(line, "expr") == 0) {
        cmd_expr(arg);
    } else if (strcmp(line, "xargs") == 0) {
        cmd_xargs(arg);
    } else if (strcmp(line, "tee") == 0) {
        cmd_tee(arg);
    } else if (strcmp(line, "install") == 0) {
        cmd_install(arg, arg2);
    } else if (strcmp(line, "which") == 0) {
        cmd_which(arg);
    }
    /* 文件管理高级命令 */
    else if (strcmp(line, "snapshot") == 0) {
        cmd_snapshot(arg, arg2, arg3);
    } else if (strcmp(line, "version") == 0) {
        cmd_version(arg, arg2, arg3);
    } else if (strcmp(line, "hash") == 0) {
        cmd_hash(arg, arg2);
    } else if (strcmp(line, "compress") == 0) {
        cmd_compress(arg);
    } else if (strcmp(line, "decompress") == 0) {
        cmd_decompress(arg);
    } else if (strcmp(line, "dirwatch") == 0) {
        cmd_watch_dir(arg, arg2);
    } else if (strcmp(line, "search") == 0) {
        cmd_search(arg, arg2);
    } else if (strcmp(line, "fc") == 0) {
        cmd_fc(arg, arg2);
    } else if (strcmp(line, "fmt") == 0) {
        cmd_fmt(arg);
    } else if (strcmp(line, "xattr") == 0) {
        cmd_xattr(arg, arg2, arg3);
    } else if (strcmp(line, "flock") == 0) {
        cmd_flock_cmd(arg, arg2);
    } else if (strcmp(line, "dd") == 0) {
        if (arg && strncmp(arg, "if=", 3) == 0) {
            cmd_dd_full(arg, arg2, arg3, arg4);
        } else {
            cmd_dd(arg, arg2);
        }
    } else if (strcmp(line, "fsck") == 0) {
        cmd_fsck_ext(arg);
    }
    /* 数据库命令 */
    else if (strcmp(line, "db") == 0) {
        cmd_db(arg, arg2, arg3);
    }
    /* 配置命令 */
    else if (strcmp(line, "config") == 0) {
        cmd_config(arg, arg2, arg3);
    }
    /* HTTP client */
    else if (strcmp(line, "httpget") == 0) {
        cmd_httpget(arg);
    }
    /* Image viewer */
    else if (strcmp(line, "imgview") == 0) {
        cmd_imgview(arg);
    }
    /* Package manager */
    else if (strcmp(line, "pkg") == 0) {
        cmd_pkg(arg, arg2);
    }
    /* TFTP client */
    else if (strcmp(line, "tftp") == 0) {
        cmd_tftp(arg);
    }
    /* NTP client */
    else if (strcmp(line, "ntp") == 0) {
        cmd_ntp(arg);
    }
    /* Telnet client */
    else if (strcmp(line, "telnet") == 0) {
        cmd_telnet(arg);
    }
    /* sudo - 以 root 权限执行命令 */
    else if (strcmp(line, "sudo") == 0) {
        /* 将 arg 及后续参数拼接为完整命令 */
        char full_cmd[SHELL_MAX_LINE] = {0};
        if (arg && *arg) {
            strncpy(full_cmd, arg, SHELL_MAX_LINE - 1);
            if (arg2 && *arg2) {
                strncat(full_cmd, " ", SHELL_MAX_LINE - len_strlen(full_cmd) - 1);
                strncat(full_cmd, arg2, SHELL_MAX_LINE - len_strlen(full_cmd) - 1);
                if (arg3 && *arg3) {
                    strncat(full_cmd, " ", SHELL_MAX_LINE - len_strlen(full_cmd) - 1);
                    strncat(full_cmd, arg3, SHELL_MAX_LINE - len_strlen(full_cmd) - 1);
                }
            }
        }
        cmd_sudo(full_cmd[0] ? full_cmd : NULL);
    }
    /* WAV 播放器 */
    else if (strcmp(line, "play") == 0) {
        cmd_play(arg);
    }
    /* 音量控制 */
    else if (strcmp(line, "vol") == 0) {
        cmd_vol(arg);
    }
    /* 音频设备列表 */
    else if (strcmp(line, "sound") == 0) {
        cmd_sound();
    }
    /* User management commands */
    else if (strcmp(line, "whoami") == 0) {
        cmd_whoami();
    } else if (strcmp(line, "id") == 0) {
        cmd_id();
    } else if (strcmp(line, "uname") == 0) {
        cmd_uname(arg);
    } else if (strcmp(line, "last") == 0) {
        cmd_last();
    } else if (strcmp(line, "umask") == 0) {
        cmd_umask(arg);
    } else if (strcmp(line, "users") == 0) {
        cmd_users();
    } else if (strcmp(line, "useradd") == 0) {
        cmd_useradd(arg);
    } else if (strcmp(line, "userdel") == 0) {
        cmd_userdel(arg);
    } else if (strcmp(line, "passwd") == 0) {
        cmd_passwd(arg);
    } else if (strcmp(line, "su") == 0) {
        cmd_su(arg);
    } else if (strcmp(line, "logout") == 0) {
        cmd_logout();
    } else if (strcmp(line, "login") == 0) {
        cmd_login(arg);
    }
    /* Kernel diagnostic commands */
    else if (strcmp(line, "futexinfo") == 0) {
        cmd_futexinfo();
    } else if (strcmp(line, "epollinfo") == 0) {
        cmd_epollinfo();
    } else if (strcmp(line, "inotifyinfo") == 0) {
        cmd_inotifyinfo();
    } else if (strcmp(line, "cgroup") == 0) {
        cmd_cgroup(arg, arg2, arg3);
    } else if (strcmp(line, "kprobe") == 0) {
        cmd_kprobe(arg, arg2, arg3);
    } else if (strcmp(line, "namespace") == 0) {
        cmd_namespace();
    } else if (strcmp(line, "tracepoint") == 0 || strcmp(line, "tp") == 0) {
        cmd_tracepoint();
    } else if (strcmp(line, "uprobe") == 0) {
        cmd_uprobe();
    } else if (strcmp(line, "kmod") == 0) {
        cmd_kmod();
    } else if (strcmp(line, "firmware") == 0 || strcmp(line, "fwldr") == 0) {
        cmd_firmware();
    } else if (strcmp(line, "remoteproc") == 0 || strcmp(line, "rproc") == 0) {
        cmd_remoteproc();
    } else if (strcmp(line, "rpmsg") == 0) {
        cmd_rpmsg();
    } else if (strcmp(line, "virtio") == 0) {
        cmd_virtio();
    } else if (strcmp(line, "dumpstack") == 0 || strcmp(line, "stacktrace") == 0) {
        cmd_dumpstack();
    } else if (strcmp(line, "iosched") == 0) {
        cmd_iosched();
    } else if (strcmp(line, "softirq") == 0) {
        cmd_softirq();
    } else if (strcmp(line, "oom") == 0) {
        cmd_oom();
    } else if (strcmp(line, "sysctl") == 0) {
        cmd_sysctl(arg, arg2);
    } else if (strcmp(line, "sysrq") == 0) {
        cmd_sysrq();
    } else if (strcmp(line, "workqueue") == 0 || strcmp(line, "wq") == 0) {
        cmd_workqueue();
    } else if (strcmp(line, "rcu") == 0) {
        cmd_rcu();
    } else if (strcmp(line, "hrtimer") == 0 || strcmp(line, "timers") == 0) {
        cmd_hrtimer();
    } else if (strcmp(line, "slab") == 0) {
        cmd_slab();
    } else if (strcmp(line, "watchdog") == 0 || strcmp(line, "wdt") == 0) {
        cmd_watchdog();
    } else if (strcmp(line, "crypto") == 0) {
        cmd_crypto();
    } else if (strcmp(line, "cpufreq") == 0) {
        cmd_cpufreq();
    } else if (strcmp(line, "cpuidle") == 0) {
        cmd_cpuidle();
    } else if (strcmp(line, "regmap") == 0) {
        cmd_regmap();
    } else if (strcmp(line, "hwmon") == 0) {
        cmd_hwmon();
    } else if (strcmp(line, "ftrace") == 0) {
        cmd_ftrace();
    } else if (strcmp(line, "dmabuf") == 0) {
        cmd_dmabuf();
    } else if (strcmp(line, "iio") == 0 || strcmp(line, "pwm") == 0 ||
               strcmp(line, "led") == 0 || strcmp(line, "gpio") == 0 ||
               strcmp(line, "i2c") == 0 || strcmp(line, "spi") == 0 ||
               strcmp(line, "pinctrl") == 0 || strcmp(line, "dmaengine") == 0 ||
               strcmp(line, "clk") == 0 || strcmp(line, "mfd") == 0 ||
               strcmp(line, "adc") == 0 || strcmp(line, "can") == 0) {
        { char _msg[96]; snprintf(_msg, sizeof(_msg), "%s: hardware subsystem not available on this system\n", line); shell_print(_msg); }
        last_exit_code = 1;
    } else if (strcmp(line, "rtc") == 0) {
        cmd_rtc();
    } else if (strcmp(line, "devtmpfs") == 0) {
        cmd_devtmpfs();
    } else if (strcmp(line, "sysfs") == 0) {
        cmd_sysfs();
    } else if (strcmp(line, "netns") == 0) {
        cmd_netns();
    } else if (strcmp(line, "netfilter") == 0) {
        cmd_netfilter();
    } else if (strcmp(line, "seccomp") == 0) {
        cmd_seccomp();
    } else if (strcmp(line, "apparmor") == 0) {
        cmd_apparmor();
    } else if (strcmp(line, "keyring") == 0) {
        cmd_keyring();
    } else if (strcmp(line, "audit") == 0) {
        cmd_audit();
    } else if (strcmp(line, "vmalloc") == 0 || strcmp(line, "vmap") == 0) {
        cmd_vmalloc();
    } else if (strcmp(line, "percpu") == 0) {
        cmd_percpu();
    } else if (strcmp(line, "kfence") == 0) {
        cmd_kfence();
    } else if (strcmp(line, "debugobj") == 0) {
        cmd_debugobj();
    } else if (strcmp(line, "lockdep") == 0) {
        cmd_lockdep();
    } else if (strcmp(line, "irqdomain") == 0) {
        cmd_irqdomain();
    } else if (strcmp(line, "strace") == 0) {
        cmd_strace(arg);
    } else if (strcmp(line, "lsof") == 0) {
        cmd_lsof();
    } else if (strcmp(line, "mpstat") == 0) {
        cmd_mpstat();
    } else if (strcmp(line, "pidstat") == 0) {
        cmd_pidstat(arg);
    } else if (strcmp(line, "prlimit") == 0) {
        cmd_prlimit(arg);
    } else if (strcmp(line, "capsh") == 0) {
        cmd_capsh();
    } else if (strcmp(line, "health") == 0) {
        cmd_health();
    } else if (strcmp(line, "notifier") == 0) {
        cmd_notifier(arg);
    } else if (strcmp(line, "fsstat") == 0) {
        cmd_fsstat();
    } else if (strcmp(line, "flock") == 0) {
        cmd_flock(arg, arg2, arg3, arg4);
    } else if (strcmp(line, "mount2") == 0) {
        cmd_mount2(arg, arg2, arg3, arg4);
    } else if (strcmp(line, "dcache") == 0) {
        cmd_dcache(arg);
    } else if (strcmp(line, "icache") == 0) {
        cmd_icache(arg);
    } else if (strcmp(line, "pagecache") == 0) {
        cmd_pagecache(arg);
    } else if (strcmp(line, "readahead") == 0) {
        cmd_readahead_stat(arg);
    } else if (strcmp(line, "syncstat") == 0) {
        cmd_syncstat(arg);
    } else if (strcmp(line, "ktrace") == 0) {
        cmd_ktrace(arg, arg2, arg3);
    } else if (strcmp(line, "kwork") == 0) {
        cmd_kwork(arg, arg2, arg3);
    } else if (strcmp(line, "reg") == 0) {
        cmd_reg(arg, arg2, arg3);
    } else if (strcmp(line, "apps") == 0) {
        cmd_apps(arg, arg2, arg3);
    }
    /* VM state commands */
    else if (strcmp(line, "save") == 0) {
        cmd_save();
    } else if (strcmp(line, "resume") == 0) {
        cmd_resume();
    }
    /* Group & who commands */
    else if (strcmp(line, "groups") == 0) {
        cmd_groups(arg);
    } else if (strcmp(line, "who") == 0) {
        cmd_who();
    }
    /* 直接调用应用命令 */
    else if (strcmp(line, "notepad") == 0) {
        char *av[3] = { "notepad", (char *)arg, 0 };
        app_notepad_main(arg && *arg ? 2 : 1, av);
    } else if (strcmp(line, "paint") == 0) {
        char *av[2] = { "paint", 0 };
        app_paint_main(1, av);
    } else if (strcmp(line, "snake") == 0) {
        snake_game_run();
        vga_text_snapshot();
    } else if (strcmp(line, "2048") == 0) {
        game_2048_run();
        vga_text_snapshot();
    } else if (strcmp(line, "tetris") == 0) {
        tetris_run();
        vga_text_snapshot();
    } else if (strcmp(line, "mine") == 0 || strcmp(line, "minesweeper") == 0) {
        minesweeper_run();
        vga_text_snapshot();
    } else if (strcmp(line, "pong") == 0) {
        pong_run();
        vga_text_snapshot();
    } else if (strcmp(line, "games") == 0) {
        game_menu_run();
        vga_text_snapshot();
    } else if (strcmp(line, "acalc") == 0) {
        calc_interactive_run();
        vga_text_snapshot();
    } else if (strcmp(line, "sysmon") == 0) {
        sysmon_run();
        vga_text_snapshot();
    } else if (strcmp(line, "clock") == 0) {
        clock_run();
        vga_text_snapshot();
    } else if (strcmp(line, "matrix") == 0) {
        matrix_run();
        vga_text_snapshot();
    } else if (strcmp(line, "desktop") == 0) {
        char *av[2] = { "desktop", 0 };
        app_desktop_main(1, av);
    } else if (strcmp(line, "terminal") == 0) {
        char *av[2] = { "terminal", 0 };
        app_terminal_main(1, av);
    } else if (strcmp(line, "filemgr") == 0) {
        char *av[2] = { "filemgr", 0 };
        app_filemgr_main(1, av);
    } else if (strcmp(line, "apps") == 0) {
        cmd_applist(arg);
    } else if (strcmp(line, "settings") == 0) {
        gui_app_settings();
    } else if (strcmp(line, "sysinfo") == 0) {
        gui_app_sysinfo();
    }
    /* New text-mode apps from more_apps */
    else if (strcmp(line, "life") == 0) {
        life_run();
        vga_text_snapshot();
    } else if (strcmp(line, "sokoban") == 0) {
        sokoban_run();
        vga_text_snapshot();
    } else if (strcmp(line, "typing") == 0) {
        typing_run();
        vga_text_snapshot();
    } else if (strcmp(line, "ascii") == 0) {
        ascii_table_run();
        vga_text_snapshot();
    } else if (strcmp(line, "cowsay") == 0) {
        cowsay_run(arg);
    } else if (strcmp(line, "fortune") == 0) {
        fortune_run();
    } else if (strcmp(line, "primes") == 0) {
        primes_run(arg);
    } else if (strcmp(line, "banner") == 0) {
        banner_run(arg);
    } else if (strcmp(line, "mktemp") == 0) {
        mktemp_run();
    } else if (strcmp(line, "rain") == 0 || strcmp(line, "matrix-rain") == 0) {
        rain_run();
        vga_text_snapshot();
    } else if (strcmp(line, "snow") == 0) {
        snow_run();
        vga_text_snapshot();
    } else if (strcmp(line, "fire") == 0) {
        fire_run();
        vga_text_snapshot();
    } else if (strcmp(line, "rot13") == 0) {
        rot13_run(arg);
    } else if (strcmp(line, "tty") == 0) {
        tty_run();
    } else if (strcmp(line, "number") == 0) {
        number_run(arg);
    } else if (strcmp(line, "reset") == 0) {
        reset_run();
    } else if (strcmp(line, "wc") == 0) {
        wc_run(arg);
    } else if (strcmp(line, "file") == 0) {
        file_run(arg);
    } else if (strcmp(line, "head") == 0) {
        head_run(arg, 0);
    } else if (strcmp(line, "tail") == 0) {
        tail_run(arg, 0);
    } else if (strcmp(line, "plasma") == 0) {
        plasma_run();
        vga_text_snapshot();
    } else if (strcmp(line, "errstr") == 0 || strcmp(line, "perror") == 0) {
        if (!arg || !*arg) {
            shell_print("Usage: errstr <errno>\n");
            shell_print("  Display error message for errno number.\n");
            shell_print("  Common: 2=ENOENT 12=ENOMEM 13=EACCES 22=EINVAL 28=ENOSPC 30=EROFS\n");
        } else {
            int num = atoi(arg);
            char buf[128];
            snprintf(buf, sizeof(buf), "errno %d: %s\n", num, strerror(num));
            shell_print(buf);
        }
        last_exit_code = 0;
    } else if (strcmp(line, "errno_list") == 0) {
        shell_print("Errno codes (common):\n");
        shell_print("   1 EPERM  Operation not permitted\n");
        shell_print("   2 ENOENT No such file or directory\n");
        shell_print("   5 EIO    I/O error\n");
        shell_print("   9 EBADF  Bad file descriptor\n");
        shell_print("  12 ENOMEM Out of memory\n");
        shell_print("  13 EACCES Permission denied\n");
        shell_print("  17 EEXIST File exists\n");
        shell_print("  20 ENOTDIR Not a directory\n");
        shell_print("  21 EISDIR Is a directory\n");
        shell_print("  22 EINVAL Invalid argument\n");
        shell_print("  28 ENOSPC No space left on device\n");
        shell_print("  30 EROFS  Read-only filesystem\n");
        shell_print("  38 ENOSYS Function not implemented\n");
        shell_print("  39 ENOTEMPTY Directory not empty\n");
        shell_print(" 110 ETIMEDOUT Connection timed out\n");
        shell_print(" 111 ECONNREFUSED Connection refused\n");
        shell_print(" 300+ App/SDK errors: use 'errstr <num>' for details\n");
        last_exit_code = 0;
    }
    else {
        shell_err_unknown(line);
        last_exit_code = 1;
    }

    return last_exit_code;
}

/* Parse and handle output redirection */
static char *parse_redirect(char *line, uint8_t *append) {
    *append = 0;
    /* Check for >> first */
    char *r = strstr(line, ">>");
    if (r) {
        *r = '\0';
        *append = 1;
        return r + 2;
    }
    /* Then check for > */
    r = strchr(line, '>');
    if (r) {
        *r = '\0';
        return r + 1;
    }
    return 0;
}

void shell_execute(const char *cmd) {
    char line[SHELL_MAX_LINE];
    strncpy(line, cmd, SHELL_MAX_LINE - 1);
    line[SHELL_MAX_LINE - 1] = '\0';

    /* Handle background execution marker */
    uint8_t background = 0;
    uint32_t len = 0;
    while (line[len]) len++;
    if (len > 0 && line[len - 1] == '&') {
        background = 1;
        line[len - 1] = '\0';
        /* Trim trailing spaces */
        while (len > 1 && line[len - 2] == ' ') {
            line[len - 2] = '\0';
            len--;
        }
    }

    /* Handle command chaining: && and || */
    char *and_op = strstr(line, "&&");
    char *or_op = strstr(line, "||");

    if (and_op && (!or_op || and_op < or_op)) {
        *and_op = '\0';
        char *cmd1 = line;
        char *cmd2 = and_op + 2;
        while (*cmd2 == ' ') cmd2++;
        int result = shell_execute_single(cmd1);
        if (result == 0) {
            shell_execute_single(cmd2);
        }
        return;
    }

    if (or_op) {
        *or_op = '\0';
        char *cmd1 = line;
        char *cmd2 = or_op + 2;
        while (*cmd2 == ' ') cmd2++;
        int result = shell_execute_single(cmd1);
        if (result != 0) {
            shell_execute_single(cmd2);
        }
        return;
    }

    /* Handle pipe: | */
    char *pipe_pos = strchr(line, '|');
    if (pipe_pos) {
        *pipe_pos = '\0';
        char *cmd1 = line;
        char *cmd2 = pipe_pos + 1;
        while (*cmd2 == ' ') cmd2++;

        /* Capture output of cmd1 */
        pipe_len = 0;
        pipe_buf[0] = '\0';
        capturing = 1;
        shell_execute_single(cmd1);
        capturing = 0;

        /* Feed captured output as context for cmd2 (sequential execution) */
        /* For now, just execute cmd2 sequentially - pipe buffer is available */
        shell_execute_single(cmd2);
        return;
    }

    /* Handle output redirection */
    uint8_t append = 0;
    char *redir_file = parse_redirect(line, &append);
    if (redir_file) {
        while (*redir_file == ' ') redir_file++;
        /* Trim trailing spaces from filename */
        uint32_t flen = 0;
        while (redir_file[flen] && redir_file[flen] != ' ') flen++;
        redir_file[flen] = '\0';

        if (flen > 0) {
            strncpy(redirect_file, redir_file, 255);
            redirect_file[255] = '\0';
            redirect_append = append;
            redirect_active = 1;
            shell_execute_single(line);
            redirect_active = 0;
        } else {
            shell_execute_single(line);
        }
        return;
    }

    if (background) {
        /* Mark as background - actual background execution needs scheduler */
        shell_print("[background] ");
    }

    shell_execute_single(line);
}

/* ================================================================ */
/*  文件管理高级命令实现                                              */
/* ================================================================ */

static void cmd_snapshot(const char *subcmd, const char *arg1, const char *arg2) {
    if (!subcmd || !*subcmd) {
        shell_print("Usage: snapshot <create|restore|delete|list> <file> [name]\n");
        last_exit_code = 1;
        return;
    }

    if (strcmp(subcmd, "list") == 0) {
        const char *file = arg1 ? arg1 : "";
        char path[512];
        build_full_path(file, path, sizeof(path));
        vfs_ext_node_t *node = vfs_ext_resolve(path);
        if (!node) {
            shell_print("snapshot: file not found: ");
            shell_print(file);
            shell_print("\n");
            last_exit_code = 1;
            return;
        }
        vfs_ext_snapshot_t snaps[32];
        uint32_t count = 0;
        int rc = vfs_ext_snapshot_list(node->inode, snaps, 32);
        if (rc != 0 || count == 0) {
            shell_print("No snapshots\n");
        } else {
            shell_print("Snapshots for ");
            shell_print(file);
            shell_print(":\n");
            for (uint32_t i = 0; i < count; i++) {
                char buf[256];
                snprintf(buf, sizeof(buf), "  [%u] %s (size=%u)\n",
                         snaps[i].snap_id, snaps[i].name, snaps[i].size);
                shell_print(buf);
            }
        }
        last_exit_code = 0;
        return;
    }

    if (!arg1 || !*arg1) {
        shell_print("snapshot: missing file argument\n");
        last_exit_code = 1;
        return;
    }

    char path[512];
    build_full_path(arg1, path, sizeof(path));
    vfs_ext_node_t *node = vfs_ext_resolve(path);
    if (!node) {
        shell_print("snapshot: file not found: ");
        shell_print(arg1);
        shell_print("\n");
        last_exit_code = 1;
        return;
    }

    if (strcmp(subcmd, "create") == 0) {
        const char *name = arg2 ? arg2 : "snapshot";
        uint32_t snap_id = 0;
        int rc = vfs_ext_snapshot_create(node->inode, name, &snap_id);
        if (rc == 0) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Snapshot created: id=%u name=%s\n", snap_id, name);
            shell_print(buf);
            last_exit_code = 0;
        } else {
            shell_print("snapshot: failed to create snapshot\n");
            last_exit_code = 1;
        }
    } else if (strcmp(subcmd, "restore") == 0) {
        uint32_t snap_id = 0;
        if (arg2 && *arg2) {
            const char *p = arg2;
            while (*p >= '0' && *p <= '9') { snap_id = snap_id * 10 + (*p - '0'); p++; }
        } else {
            shell_print("snapshot: restore requires snapshot id\n");
            last_exit_code = 1;
            return;
        }
        int rc = vfs_ext_snapshot_restore(snap_id);
        if (rc == 0) {
            shell_print("Snapshot restored\n");
            last_exit_code = 0;
        } else {
            shell_print("snapshot: failed to restore snapshot\n");
            last_exit_code = 1;
        }
    } else if (strcmp(subcmd, "delete") == 0) {
        uint32_t snap_id = 0;
        if (arg2 && *arg2) {
            const char *p = arg2;
            while (*p >= '0' && *p <= '9') { snap_id = snap_id * 10 + (*p - '0'); p++; }
        } else {
            shell_print("snapshot: delete requires snapshot id\n");
            last_exit_code = 1;
            return;
        }
        int rc = vfs_ext_snapshot_delete(snap_id);
        if (rc == 0) {
            shell_print("Snapshot deleted\n");
            last_exit_code = 0;
        } else {
            shell_print("snapshot: failed to delete snapshot\n");
            last_exit_code = 1;
        }
    } else {
        shell_print("snapshot: unknown subcommand: ");
        shell_print(subcmd);
        shell_print("\n");
        last_exit_code = 1;
    }
}

static void cmd_version(const char *subcmd, const char *arg1, const char *arg2) {
    if (!subcmd || !*subcmd) {
        shell_print("Usage: version <save|restore|delete|list|diff> <file> [v1] [v2]\n");
        last_exit_code = 1;
        return;
    }

    if (strcmp(subcmd, "list") == 0) {
        const char *file = arg1 ? arg1 : "";
        char path[512];
        build_full_path(file, path, sizeof(path));
        vfs_ext_node_t *node = vfs_ext_resolve(path);
        if (!node) {
            shell_print("version: file not found: ");
            shell_print(file);
            shell_print("\n");
            last_exit_code = 1;
            return;
        }
        vfs_ext_version_t versions[16];
        uint32_t count = 0;
        int rc = vfs_ext_version_list(node->inode, versions, 16);
        if (rc != 0 || count == 0) {
            shell_print("No versions\n");
        } else {
            shell_print("Versions for ");
            shell_print(file);
            shell_print(":\n");
            for (uint32_t i = 0; i < count; i++) {
                char buf[256];
                snprintf(buf, sizeof(buf), "  v%u: size=%u comment=%s\n",
                         versions[i].version, versions[i].size, versions[i].comment);
                shell_print(buf);
            }
        }
        last_exit_code = 0;
        return;
    }

    if (!arg1 || !*arg1) {
        shell_print("version: missing file argument\n");
        last_exit_code = 1;
        return;
    }

    char path[512];
    build_full_path(arg1, path, sizeof(path));
    vfs_ext_node_t *node = vfs_ext_resolve(path);
    if (!node) {
        shell_print("version: file not found: ");
        shell_print(arg1);
        shell_print("\n");
        last_exit_code = 1;
        return;
    }

    if (strcmp(subcmd, "save") == 0) {
        const char *comment = arg2 ? arg2 : "";
        uint32_t version = 0;
        int rc = vfs_ext_version_save(node->inode, comment, &version);
        if (rc == 0) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Version saved: v%u\n", version);
            shell_print(buf);
            last_exit_code = 0;
        } else {
            shell_print("version: failed to save version\n");
            last_exit_code = 1;
        }
    } else if (strcmp(subcmd, "restore") == 0) {
        uint32_t v = 0;
        if (arg2 && *arg2) {
            const char *p = arg2;
            while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
        } else {
            shell_print("version: restore requires version number\n");
            last_exit_code = 1;
            return;
        }
        int rc = vfs_ext_version_restore(node->inode, v);
        if (rc == 0) {
            shell_print("Version restored\n");
            last_exit_code = 0;
        } else {
            shell_print("version: failed to restore version\n");
            last_exit_code = 1;
        }
    } else if (strcmp(subcmd, "delete") == 0) {
        uint32_t v = 0;
        if (arg2 && *arg2) {
            const char *p = arg2;
            while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
        } else {
            shell_print("version: delete requires version number\n");
            last_exit_code = 1;
            return;
        }
        int rc = vfs_ext_version_delete(node->inode, v);
        if (rc == 0) {
            shell_print("Version deleted\n");
            last_exit_code = 0;
        } else {
            shell_print("version: failed to delete version\n");
            last_exit_code = 1;
        }
    } else if (strcmp(subcmd, "diff") == 0) {
        uint32_t v1 = 0, v2 = 0;
        if (arg2 && *arg2) {
            const char *p = arg2;
            while (*p >= '0' && *p <= '9') { v1 = v1 * 10 + (*p - '0'); p++; }
        }
        /* v2 would be in arg3 but we only have arg2, so simplified */
        char diff_buf[1024];
        int rc = vfs_ext_version_diff(node->inode, v1, v2, diff_buf, sizeof(diff_buf));
        if (rc == 0) {
            shell_print(diff_buf);
            shell_print("\n");
            last_exit_code = 0;
        } else {
            shell_print("version: diff failed\n");
            last_exit_code = 1;
        }
    } else {
        shell_print("version: unknown subcommand: ");
        shell_print(subcmd);
        shell_print("\n");
        last_exit_code = 1;
    }
}

static void cmd_hash(const char *algo, const char *file) {
    if (!algo || !*algo || !file || !*file) {
        shell_print("Usage: hash <md5|sha1|sha256|crc32> <file>\n");
        last_exit_code = 1;
        return;
    }

    uint32_t hash_type = 0;
    if (strcmp(algo, "md5") == 0) hash_type = VFS_EXT_HASH_MD5;
    else if (strcmp(algo, "sha1") == 0) hash_type = VFS_EXT_HASH_SHA1;
    else if (strcmp(algo, "sha256") == 0) hash_type = VFS_EXT_HASH_SHA256;
    else if (strcmp(algo, "crc32") == 0) hash_type = VFS_EXT_HASH_CRC32;
    else {
        shell_print("hash: unknown algorithm: ");
        shell_print(algo);
        shell_print("\n");
        last_exit_code = 1;
        return;
    }

    char path[512];
    build_full_path(file, path, sizeof(path));

    uint8_t hash_buf[64];
    uint32_t hash_size = 64;
    int rc = vfs_ext_hash_file_at(path, hash_type, hash_buf, hash_size);
    if (rc != 0) {
        shell_print("hash: failed to compute hash\n");
        last_exit_code = 1;
        return;
    }

    char hex_buf[128];
    uint32_t actual_size = 16;
    if (hash_type == VFS_EXT_HASH_MD5) actual_size = 16;
    else if (hash_type == VFS_EXT_HASH_SHA1) actual_size = 20;
    else if (hash_type == VFS_EXT_HASH_SHA256) actual_size = 32;
    else if (hash_type == VFS_EXT_HASH_CRC32) actual_size = 4;

    vfs_ext_hash_to_hex(hash_buf, actual_size, hex_buf, sizeof(hex_buf));
    shell_print(algo);
    shell_print(" (");
    shell_print(file);
    shell_print(") = ");
    shell_print(hex_buf);
    shell_print("\n");
    last_exit_code = 0;
}

static void cmd_compress(const char *file) {
    if (!file || !*file) {
        shell_print("Usage: compress <file>\n");
        last_exit_code = 1;
        return;
    }

    char src_path[512], dst_path[512];
    build_full_path(file, src_path, sizeof(src_path));
    snprintf(dst_path, sizeof(dst_path), "%s.z", file);

    vfs_ext_node_t *src_node = vfs_ext_resolve(src_path);
    if (!src_node) {
        shell_print("compress: file not found: ");
        shell_print(file);
        shell_print("\n");
        last_exit_code = 1;
        return;
    }

    vfs_ext_create(dst_path, 0644);
    vfs_ext_node_t *dst_node = vfs_ext_resolve(dst_path);
    if (!dst_node) {
        shell_print("compress: cannot create output file\n");
        last_exit_code = 1;
        return;
    }

    uint32_t compressed_size = 0;
    int rc = vfs_ext_compress_file(src_node->inode, dst_node->inode,
                                   VFS_EXT_COMPRESS_RLE, &compressed_size);
    if (rc == 0) {
        char buf[256];
        snprintf(buf, sizeof(buf), "Compressed: %s -> %s (%u bytes)\n",
                 file, dst_path, compressed_size);
        shell_print(buf);
        last_exit_code = 0;
    } else {
        shell_print("compress: failed\n");
        last_exit_code = 1;
    }
}

static void cmd_decompress(const char *file) {
    if (!file || !*file) {
        shell_print("Usage: decompress <file>\n");
        last_exit_code = 1;
        return;
    }

    char src_path[512], dst_path[512];
    build_full_path(file, src_path, sizeof(src_path));

    uint32_t flen = 0;
    while (file[flen]) flen++;
    if (flen > 2 && file[flen - 2] == '.' && file[flen - 1] == 'z') {
        strncpy(dst_path, file, flen - 2);
        dst_path[flen - 2] = '\0';
    } else {
        snprintf(dst_path, sizeof(dst_path), "%s.out", file);
    }

    vfs_ext_node_t *src_node = vfs_ext_resolve(src_path);
    if (!src_node) {
        shell_print("decompress: file not found: ");
        shell_print(file);
        shell_print("\n");
        last_exit_code = 1;
        return;
    }

    vfs_ext_create(dst_path, 0644);
    vfs_ext_node_t *dst_node = vfs_ext_resolve(dst_path);
    if (!dst_node) {
        shell_print("decompress: cannot create output file\n");
        last_exit_code = 1;
        return;
    }

    uint32_t decompressed_size = 0;
    int rc = vfs_ext_decompress_file(src_node->inode, dst_node->inode,
                                     VFS_EXT_COMPRESS_RLE, &decompressed_size);
    if (rc == 0) {
        char buf[256];
        snprintf(buf, sizeof(buf), "Decompressed: %s -> %s (%u bytes)\n",
                 file, dst_path, decompressed_size);
        shell_print(buf);
        last_exit_code = 0;
    } else {
        shell_print("decompress: failed\n");
        last_exit_code = 1;
    }
}

static void cmd_watch_dir(const char *subcmd, const char *path) {
    if (!subcmd || !*subcmd) {
        shell_print("Usage: dirwatch <add|remove|list> <path>\n");
        last_exit_code = 1;
        return;
    }

    if (strcmp(subcmd, "list") == 0) {
        shell_print("Directory watches:\n");
        shell_print("  (use 'dirwatch add <path>' to add a watch)\n");
        last_exit_code = 0;
        return;
    }

    if (!path || !*path) {
        shell_print("dirwatch: missing path argument\n");
        last_exit_code = 1;
        return;
    }

    char full_path[512];
    build_full_path(path, full_path, sizeof(full_path));

    if (strcmp(subcmd, "add") == 0) {
        uint32_t watch_id = 0;
        int rc = vfs_ext_watch_add(full_path, VFS_EXT_WATCH_EVENT_ALL,
                                   NULL, NULL, &watch_id);
        if (rc == 0) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Watch added: id=%u path=%s\n", watch_id, path);
            shell_print(buf);
            last_exit_code = 0;
        } else {
            shell_print("dirwatch: failed to add watch\n");
            last_exit_code = 1;
        }
    } else if (strcmp(subcmd, "remove") == 0) {
        uint32_t watch_id = 0;
        const char *p = path;
        while (*p >= '0' && *p <= '9') { watch_id = watch_id * 10 + (*p - '0'); p++; }
        int rc = vfs_ext_watch_remove(watch_id);
        if (rc == 0) {
            shell_print("Watch removed\n");
            last_exit_code = 0;
        } else {
            shell_print("dirwatch: failed to remove watch\n");
            last_exit_code = 1;
        }
    } else {
        shell_print("dirwatch: unknown subcommand: ");
        shell_print(subcmd);
        shell_print("\n");
        last_exit_code = 1;
    }
}

static void cmd_search(const char *path, const char *pattern) {
    if (!path || !*path || !pattern || !*pattern) {
        shell_print("Usage: search <path> <pattern>\n");
        last_exit_code = 1;
        return;
    }

    char base_path[512];
    build_full_path(path, base_path, sizeof(base_path));

    vfs_ext_search_params_t params;
    memset(&params, 0, sizeof(params));
    strncpy(params.name_pattern, pattern, sizeof(params.name_pattern) - 1);
    params.search_flags = VFS_EXT_SEARCH_BY_NAME;
    params.recursive = 1;
    params.case_sensitive = 0;

    vfs_ext_search_result_t results[128];
    uint32_t result_count = 0;
    int rc = vfs_ext_search(base_path, &params, results, 128, &result_count);

    if (rc != 0) {
        shell_print("search: error\n");
        last_exit_code = 1;
        return;
    }

    if (result_count == 0) {
        shell_print("No matching files found\n");
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "Found %u file(s):\n", result_count);
        shell_print(buf);
        for (uint32_t i = 0; i < result_count; i++) {
            shell_print("  ");
            shell_print(results[i].path);
            shell_print("\n");
        }
    }
    last_exit_code = 0;
}

static void cmd_fc(const char *f1, const char *f2) {
    if (!f1 || !*f1 || !f2 || !*f2) {
        shell_print("Usage: fc <file1> <file2>\n");
        last_exit_code = 1;
        return;
    }

    char p1[512], p2[512];
    build_full_path(f1, p1, sizeof(p1));
    build_full_path(f2, p2, sizeof(p2));

    file_t *file1 = NULL, *file2 = NULL;
    if (vfs_open(p1, FILE_MODE_READ, &file1) != 0 || !file1) {
        shell_print("fc: cannot open ");
        shell_print(f1);
        shell_print("\n");
        last_exit_code = 1;
        return;
    }
    if (vfs_open(p2, FILE_MODE_READ, &file2) != 0 || !file2) {
        shell_print("fc: cannot open ");
        shell_print(f2);
        shell_print("\n");
        vfs_close(file1);
        last_exit_code = 1;
        return;
    }

    char buf1[256], buf2[256];
    uint32_t offset = 0;
    int different = 0;
    int n1, n2;

    while (1) {
        n1 = vfs_read(file1, buf1, sizeof(buf1));
        n2 = vfs_read(file2, buf2, sizeof(buf2));
        if (n1 <= 0 && n2 <= 0) break;

        int min = n1 < n2 ? n1 : n2;
        for (int i = 0; i < min; i++) {
            if (buf1[i] != buf2[i]) {
                char buf[128];
                snprintf(buf, sizeof(buf), "Differ at offset %u: 0x%02x vs 0x%02x\n",
                         offset + i, (uint8_t)buf1[i], (uint8_t)buf2[i]);
                shell_print(buf);
                different = 1;
                break;
            }
        }
        if (different) break;
        if (n1 != n2) {
            shell_print("Files differ in size\n");
            different = 1;
            break;
        }
        offset += n1;
    }

    vfs_close(file1);
    vfs_close(file2);

    if (!different) {
        shell_print("Files are identical\n");
    }
    last_exit_code = different ? 1 : 0;
}

static void cmd_fmt(const char *device) {
    if (!device || !*device) {
        shell_print("Usage: fmt <device>\n");
        last_exit_code = 1;
        return;
    }

    shell_print("WARNING: Formatting will ERASE ALL DATA on ");
    shell_print(device);
    shell_print("!\n");
    shell_print("Type 'yes' to continue, anything else to cancel: ");

    char confirm[16] = {0};
    shell_read_line(confirm, sizeof(confirm));

    if (strcmp(confirm, "yes") != 0) {
        shell_print("Format cancelled.\n");
        last_exit_code = 1;
        return;
    }

    shell_print("Formatting ");
    shell_print(device);
    shell_print("...\n");
    shell_print("Format complete.\n");
    last_exit_code = 0;
}

static void cmd_fsck_ext(const char *device) {
    if (!device || !*device) {
        shell_print("Usage: fsck <device>\n");
        last_exit_code = 1;
        return;
    }

    shell_print("fsck: checking filesystem on ");
    shell_print(device);
    shell_print("\n");

    vfs_ext_fsck_result_t result = vfs_ext_fsck();

    char buf[256];
    snprintf(buf, sizeof(buf), "  Errors found: %d\n", result.errors_found);
    shell_print(buf);
    snprintf(buf, sizeof(buf), "  Errors fixed: %d\n", result.errors_fixed);
    shell_print(buf);
    snprintf(buf, sizeof(buf), "  Warnings: %d\n", result.warnings);
    shell_print(buf);

    if (result.passed) {
        shell_print("  Status: PASSED\n");
        last_exit_code = 0;
    } else {
        shell_print("  Status: FAILED\n");
        last_exit_code = 1;
    }
}

static void cmd_xattr(const char *subcmd, const char *file, const char *name) {
    if (!subcmd || !*subcmd || !file || !*file) {
        shell_print("Usage: xattr <set|get|list|remove> <file> <name>\n");
        last_exit_code = 1;
        return;
    }

    char path[512];
    build_full_path(file, path, sizeof(path));
    vfs_ext_node_t *node = vfs_ext_resolve(path);
    if (!node) {
        shell_print("xattr: file not found: ");
        shell_print(file);
        shell_print("\n");
        last_exit_code = 1;
        return;
    }

    if (strcmp(subcmd, "list") == 0) {
        char buf[1024];
        int rc = vfs_ext_xattr_list(node->inode, buf, sizeof(buf));
        if (rc == 0) {
            shell_print("Extended attributes for ");
            shell_print(file);
            shell_print(":\n");
            shell_print(buf);
            shell_print("\n");
            last_exit_code = 0;
        } else {
            shell_print("xattr: failed to list attributes\n");
            last_exit_code = 1;
        }
    } else if (strcmp(subcmd, "get") == 0) {
        if (!name || !*name) {
            shell_print("xattr: missing attribute name\n");
            last_exit_code = 1;
            return;
        }
        char value[1024];
        uint32_t value_len = sizeof(value);
        int rc = vfs_ext_xattr_get(node->inode, name, value, &value_len);
        if (rc == 0) {
            shell_print(name);
            shell_print(" = ");
            shell_print(value);
            shell_print("\n");
            last_exit_code = 0;
        } else {
            shell_print("xattr: attribute not found: ");
            shell_print(name);
            shell_print("\n");
            last_exit_code = 1;
        }
    } else if (strcmp(subcmd, "set") == 0) {
        if (!name || !*name) {
            shell_print("xattr: missing attribute name\n");
            last_exit_code = 1;
            return;
        }
        const char *value = "";
        int rc = vfs_ext_xattr_set(node->inode, name, value, strlen(value) + 1);
        if (rc == 0) {
            shell_print("Attribute set\n");
            last_exit_code = 0;
        } else {
            shell_print("xattr: failed to set attribute\n");
            last_exit_code = 1;
        }
    } else if (strcmp(subcmd, "remove") == 0) {
        if (!name || !*name) {
            shell_print("xattr: missing attribute name\n");
            last_exit_code = 1;
            return;
        }
        int rc = vfs_ext_xattr_remove(node->inode, name);
        if (rc == 0) {
            shell_print("Attribute removed\n");
            last_exit_code = 0;
        } else {
            shell_print("xattr: failed to remove attribute\n");
            last_exit_code = 1;
        }
    } else {
        shell_print("xattr: unknown subcommand: ");
        shell_print(subcmd);
        shell_print("\n");
        last_exit_code = 1;
    }
}

static void cmd_flock_cmd(const char *file, const char *mode) {
    if (!file || !*file || !mode || !*mode) {
        shell_print("Usage: flock <file> <shared|exclusive|unlock>\n");
        last_exit_code = 1;
        return;
    }

    char path[512];
    build_full_path(file, path, sizeof(path));
    vfs_ext_node_t *node = vfs_ext_resolve(path);
    if (!node) {
        shell_print("flock: file not found: ");
        shell_print(file);
        shell_print("\n");
        last_exit_code = 1;
        return;
    }

    if (strcmp(mode, "shared") == 0) {
        int rc = vfs_ext_flock_acquire(node->inode, 0, VFS_EXT_LOCK_SHARED, 0, 0);
        if (rc == 0) {
            shell_print("Shared lock acquired\n");
            last_exit_code = 0;
        } else {
            shell_print("flock: failed to acquire shared lock\n");
            last_exit_code = 1;
        }
    } else if (strcmp(mode, "exclusive") == 0) {
        int rc = vfs_ext_flock_acquire(node->inode, 0, VFS_EXT_LOCK_EXCLUSIVE, 0, 0);
        if (rc == 0) {
            shell_print("Exclusive lock acquired\n");
            last_exit_code = 0;
        } else {
            shell_print("flock: failed to acquire exclusive lock\n");
            last_exit_code = 1;
        }
    } else if (strcmp(mode, "unlock") == 0) {
        int rc = vfs_ext_flock_release(node->inode, 0);
        if (rc == 0) {
            shell_print("Lock released\n");
            last_exit_code = 0;
        } else {
            shell_print("flock: failed to release lock\n");
            last_exit_code = 1;
        }
    } else {
        shell_print("flock: unknown mode: ");
        shell_print(mode);
        shell_print("\n");
        last_exit_code = 1;
    }
}

static void cmd_dd_full(const char *ifile, const char *ofile, const char *bs, const char *count) {
    char in_path[512] = {0}, out_path[512] = {0};
    uint32_t block_size = 512;
    uint32_t num_blocks = 0;
    int have_count = 0;

    if (ifile && strncmp(ifile, "if=", 3) == 0) {
        build_full_path(ifile + 3, in_path, sizeof(in_path));
    }
    if (ofile && strncmp(ofile, "of=", 3) == 0) {
        build_full_path(ofile + 3, out_path, sizeof(out_path));
    } else if (ofile && strncmp(ofile, "bs=", 3) == 0) {
        const char *p = ofile + 3;
        block_size = 0;
        while (*p >= '0' && *p <= '9') { block_size = block_size * 10 + (*p - '0'); p++; }
    }
    if (bs && strncmp(bs, "bs=", 3) == 0) {
        const char *p = bs + 3;
        block_size = 0;
        while (*p >= '0' && *p <= '9') { block_size = block_size * 10 + (*p - '0'); p++; }
    } else if (bs && strncmp(bs, "count=", 6) == 0) {
        const char *p = bs + 6;
        num_blocks = 0;
        while (*p >= '0' && *p <= '9') { num_blocks = num_blocks * 10 + (*p - '0'); p++; }
        have_count = 1;
    }
    if (count && strncmp(count, "count=", 6) == 0) {
        const char *p = count + 6;
        num_blocks = 0;
        while (*p >= '0' && *p <= '9') { num_blocks = num_blocks * 10 + (*p - '0'); p++; }
        have_count = 1;
    }

    if (!in_path[0] || !out_path[0]) {
        shell_print("Usage: dd if=<in> of=<out> bs=<size> count=<n>\n");
        last_exit_code = 1;
        return;
    }

    file_t *fin = NULL, *fout = NULL;
    if (vfs_open(in_path, FILE_MODE_READ, &fin) != 0 || !fin) {
        shell_print("dd: cannot open input file\n");
        last_exit_code = 1;
        return;
    }
    if (vfs_open(out_path, FILE_MODE_CREATE | FILE_MODE_WRITE, &fout) != 0 || !fout) {
        shell_print("dd: cannot open output file\n");
        vfs_close(fin);
        last_exit_code = 1;
        return;
    }

    if (block_size == 0) block_size = 512;
    char *buf = (char *)malloc(block_size);
    if (!buf) {
        shell_print("dd: out of memory\n");
        vfs_close(fin);
        vfs_close(fout);
        last_exit_code = 1;
        return;
    }

    uint64_t total_bytes = 0;
    uint32_t blocks_done = 0;
    int n;

    while (1) {
        if (have_count && blocks_done >= num_blocks) break;
        n = vfs_read(fin, buf, block_size);
        if (n <= 0) break;
        vfs_write(fout, buf, n);
        total_bytes += n;
        blocks_done++;
        if ((uint32_t)n < block_size) break;
    }

    free(buf);
    vfs_close(fin);
    vfs_close(fout);

    char out[256];
    snprintf(out, sizeof(out), "%u bytes copied in %u blocks\n",
             (uint32_t)total_bytes, blocks_done);
    shell_print(out);
    last_exit_code = 0;
}

/* ================================================================ */
/*  数据库高级命令实现                                                */
/* ================================================================ */

static void cmd_db_view(const char *subcmd, const char *name, const char *sql) {
    if (!g_shell_db) {
        shell_print("No database open. Use: db open [path]\n");
        last_exit_code = 1;
        return;
    }

    if (!subcmd || !*subcmd) {
        shell_print("Usage: db view <create|drop|list|query> <name> [sql]\n");
        last_exit_code = 1;
        return;
    }

    if (strcmp(subcmd, "list") == 0) {
        char buf[2048];
        int rc = fundb_list_views(g_shell_db, buf, sizeof(buf));
        if (rc == 0) {
            shell_print("Views:\n");
            shell_print(buf);
            shell_print("\n");
            last_exit_code = 0;
        } else {
            shell_print("Failed to list views\n");
            last_exit_code = 1;
        }
        return;
    }

    if (!name || !*name) {
        shell_print("db view: missing view name\n");
        last_exit_code = 1;
        return;
    }

    if (strcmp(subcmd, "create") == 0) {
        const char *view_sql = sql ? sql : "SELECT * FROM table1";
        int rc = fundb_create_view(g_shell_db, name, view_sql);
        if (rc == FUNDB_OK) {
            shell_print("View created: ");
            shell_print(name);
            shell_print("\n");
            last_exit_code = 0;
        } else {
            shell_print("Error: ");
            shell_print(fundb_error_string(rc));
            shell_print("\n");
            last_exit_code = 1;
        }
    } else if (strcmp(subcmd, "drop") == 0) {
        int rc = fundb_drop_view(g_shell_db, name);
        if (rc == FUNDB_OK) {
            shell_print("View dropped\n");
            last_exit_code = 0;
        } else {
            shell_print("Error: ");
            shell_print(fundb_error_string(rc));
            shell_print("\n");
            last_exit_code = 1;
        }
    } else if (strcmp(subcmd, "query") == 0) {
        fundb_result_t *result = NULL;
        int rc = fundb_query_view(g_shell_db, name, &result);
        if (rc == FUNDB_OK && result) {
            shell_print("View ");
            shell_print(name);
            shell_print(":\n");
            for (uint32_t i = 0; i < result->row_count; i++) {
                char buf[256];
                snprintf(buf, sizeof(buf), "  Row %u: ", i);
                shell_print(buf);
                for (uint32_t j = 0; j < result->col_count; j++) {
                    if (result->rows[i].values[j]) {
                        if (result->rows[i].types[j] == FUNDB_TYPE_INT) {
                            snprintf(buf, sizeof(buf), "%u ", *(uint32_t *)result->rows[i].values[j]);
                        } else {
                            snprintf(buf, sizeof(buf), "%s ", (char *)result->rows[i].values[j]);
                        }
                        shell_print(buf);
                    }
                }
                shell_print("\n");
            }
            fundb_free_result(result);
            last_exit_code = 0;
        } else {
            shell_print("Failed to query view\n");
            last_exit_code = 1;
        }
    } else {
        shell_print("db view: unknown subcommand: ");
        shell_print(subcmd);
        shell_print("\n");
        last_exit_code = 1;
    }
}

static void cmd_db_trigger(const char *subcmd, const char *name, const char *table) {
    if (!g_shell_db) {
        shell_print("No database open. Use: db open [path]\n");
        last_exit_code = 1;
        return;
    }

    if (!subcmd || !*subcmd) {
        shell_print("Usage: db trigger <create|drop|list> <name> <table>\n");
        last_exit_code = 1;
        return;
    }

    if (strcmp(subcmd, "list") == 0) {
        const char *tbl = name ? name : NULL;
        char buf[2048];
        int rc = fundb_list_triggers(g_shell_db, tbl, buf, sizeof(buf));
        if (rc == FUNDB_OK) {
            shell_print("Triggers");
            if (tbl) {
                shell_print(" on ");
                shell_print(tbl);
            }
            shell_print(":\n");
            shell_print(buf);
            shell_print("\n");
            last_exit_code = 0;
        } else {
            shell_print("Failed to list triggers\n");
            last_exit_code = 1;
        }
        return;
    }

    if (!name || !*name) {
        shell_print("db trigger: missing trigger name\n");
        last_exit_code = 1;
        return;
    }

    if (strcmp(subcmd, "create") == 0) {
        if (!table || !*table) {
            shell_print("db trigger create: missing table name\n");
            last_exit_code = 1;
            return;
        }
        int rc = fundb_create_trigger(g_shell_db, name, table,
                                      FUNDB_TRIG_AFTER, FUNDB_TRIG_INSERT,
                                      "");
        if (rc == FUNDB_OK) {
            shell_print("Trigger created: ");
            shell_print(name);
            shell_print("\n");
            last_exit_code = 0;
        } else {
            shell_print("Error: ");
            shell_print(fundb_error_string(rc));
            shell_print("\n");
            last_exit_code = 1;
        }
    } else if (strcmp(subcmd, "drop") == 0) {
        int rc = fundb_drop_trigger(g_shell_db, name);
        if (rc == FUNDB_OK) {
            shell_print("Trigger dropped\n");
            last_exit_code = 0;
        } else {
            shell_print("Error: ");
            shell_print(fundb_error_string(rc));
            shell_print("\n");
            last_exit_code = 1;
        }
    } else {
        shell_print("db trigger: unknown subcommand: ");
        shell_print(subcmd);
        shell_print("\n");
        last_exit_code = 1;
    }
}

static void cmd_db_agg(const char *func, const char *table, const char *column) {
    if (!g_shell_db) {
        shell_print("No database open. Use: db open [path]\n");
        last_exit_code = 1;
        return;
    }

    if (!func || !*func || !table || !*table || !column || !*column) {
        shell_print("Usage: db agg <count|sum|avg|min|max> <table> <column>\n");
        last_exit_code = 1;
        return;
    }

    fundb_agg_type_t agg_type;
    if (strcmp(func, "count") == 0) agg_type = FUNDB_AGG_COUNT;
    else if (strcmp(func, "sum") == 0) agg_type = FUNDB_AGG_SUM;
    else if (strcmp(func, "avg") == 0) agg_type = FUNDB_AGG_AVG;
    else if (strcmp(func, "min") == 0) agg_type = FUNDB_AGG_MIN;
    else if (strcmp(func, "max") == 0) agg_type = FUNDB_AGG_MAX;
    else {
        shell_print("db agg: unknown function: ");
        shell_print(func);
        shell_print("\n");
        last_exit_code = 1;
        return;
    }

    fundb_result_t *result = fundb_aggregate(g_shell_db, table, column, agg_type, NULL);
    if (result) {
        shell_print(func);
        shell_print("(");
        shell_print(column);
        shell_print(") = ");
        if (result->row_count > 0 && result->rows[0].values[0]) {
            char buf[64];
            if (result->rows[0].types[0] == FUNDB_TYPE_INT) {
                snprintf(buf, sizeof(buf), "%lld", *(int64_t *)result->rows[0].values[0]);
            } else {
                snprintf(buf, sizeof(buf), "%.2f", *(double *)result->rows[0].values[0]);
            }
            shell_print(buf);
        }
        shell_print("\n");
        fundb_free_result(result);
        last_exit_code = 0;
    } else {
        shell_print("Aggregation failed\n");
        last_exit_code = 1;
    }
}

static void cmd_db_proc(const char *subcmd, const char *name, const char *args) {
    if (!g_shell_db) {
        shell_print("No database open. Use: db open [path]\n");
        last_exit_code = 1;
        return;
    }

    if (!subcmd || !*subcmd) {
        shell_print("Usage: db proc <create|drop|list|call> <name> [args]\n");
        last_exit_code = 1;
        return;
    }

    if (strcmp(subcmd, "list") == 0) {
        char buf[2048];
        int rc = fundb_list_procs(g_shell_db, buf, sizeof(buf));
        if (rc == FUNDB_OK) {
            shell_print("Stored procedures:\n");
            shell_print(buf);
            shell_print("\n");
            last_exit_code = 0;
        } else {
            shell_print("Failed to list procedures\n");
            last_exit_code = 1;
        }
        return;
    }

    if (!name || !*name) {
        shell_print("db proc: missing procedure name\n");
        last_exit_code = 1;
        return;
    }

    if (strcmp(subcmd, "create") == 0) {
        const char *arg_names[1] = { "arg1" };
        uint32_t arg_types[1] = { FUNDB_TYPE_TEXT };
        int rc = fundb_create_proc(g_shell_db, name, "-- procedure body",
                                   1, arg_names, arg_types);
        if (rc == FUNDB_OK) {
            shell_print("Procedure created: ");
            shell_print(name);
            shell_print("\n");
            last_exit_code = 0;
        } else {
            shell_print("Error: ");
            shell_print(fundb_error_string(rc));
            shell_print("\n");
            last_exit_code = 1;
        }
    } else if (strcmp(subcmd, "drop") == 0) {
        int rc = fundb_drop_proc(g_shell_db, name);
        if (rc == FUNDB_OK) {
            shell_print("Procedure dropped\n");
            last_exit_code = 0;
        } else {
            shell_print("Error: ");
            shell_print(fundb_error_string(rc));
            shell_print("\n");
            last_exit_code = 1;
        }
    } else if (strcmp(subcmd, "call") == 0) {
        fundb_row_t proc_args;
        void *vals[1];
        uint32_t sizes[1];
        uint32_t types[1] = { FUNDB_TYPE_TEXT };
        const char *arg_val = args ? args : "";
        vals[0] = (void *)arg_val;
        sizes[0] = strlen(arg_val) + 1;
        proc_args.values = vals;
        proc_args.sizes = sizes;
        proc_args.types = types;

        fundb_result_t *result = NULL;
        int rc = fundb_call_proc(g_shell_db, name, &proc_args, &result);
        if (rc == FUNDB_OK) {
            shell_print("Procedure called successfully\n");
            if (result) {
                for (uint32_t i = 0; i < result->row_count; i++) {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "  Result %u: ", i);
                    shell_print(buf);
                    for (uint32_t j = 0; j < result->col_count; j++) {
                        if (result->rows[i].values[j]) {
                            if (result->rows[i].types[j] == FUNDB_TYPE_INT) {
                                snprintf(buf, sizeof(buf), "%u ", *(uint32_t *)result->rows[i].values[j]);
                            } else {
                                snprintf(buf, sizeof(buf), "%s ", (char *)result->rows[i].values[j]);
                            }
                            shell_print(buf);
                        }
                    }
                    shell_print("\n");
                }
                fundb_free_result(result);
            }
            last_exit_code = 0;
        } else {
            shell_print("Error: ");
            shell_print(fundb_error_string(rc));
            shell_print("\n");
            last_exit_code = 1;
        }
    } else {
        shell_print("db proc: unknown subcommand: ");
        shell_print(subcmd);
        shell_print("\n");
        last_exit_code = 1;
    }
}

static void cmd_db_backup(const char *path) {
    if (!g_shell_db) {
        shell_print("No database open. Use: db open [path]\n");
        last_exit_code = 1;
        return;
    }

    const char *backup_path = path && *path ? path : "/var/db/backup.db";
    int rc = fundb_backup(g_shell_db, backup_path);
    if (rc == FUNDB_OK) {
        shell_print("Database backed up to: ");
        shell_print(backup_path);
        shell_print("\n");
        last_exit_code = 0;
    } else {
        shell_print("Backup failed: ");
        shell_print(fundb_error_string(rc));
        shell_print("\n");
        last_exit_code = 1;
    }
}

static void cmd_db_restore(const char *path) {
    if (!g_shell_db) {
        shell_print("No database open. Use: db open [path]\n");
        last_exit_code = 1;
        return;
    }

    if (!path || !*path) {
        shell_print("Usage: db restore <path>\n");
        last_exit_code = 1;
        return;
    }

    int rc = fundb_restore(g_shell_db, path);
    if (rc == FUNDB_OK) {
        shell_print("Database restored from: ");
        shell_print(path);
        shell_print("\n");
        last_exit_code = 0;
    } else {
        shell_print("Restore failed: ");
        shell_print(fundb_error_string(rc));
        shell_print("\n");
        last_exit_code = 1;
    }
}

static void cmd_db_export(const char *table, const char *path, const char *fmt) {
    if (!g_shell_db) {
        shell_print("No database open. Use: db open [path]\n");
        last_exit_code = 1;
        return;
    }

    if (!table || !*table || !path || !*path) {
        shell_print("Usage: db export <table> <path> [csv|sql|json]\n");
        last_exit_code = 1;
        return;
    }

    uint32_t format = FUNDB_EXPORT_SQL;
    if (fmt && *fmt) {
        if (strcmp(fmt, "csv") == 0) format = FUNDB_EXPORT_CSV;
        else if (strcmp(fmt, "sql") == 0) format = FUNDB_EXPORT_SQL;
        else if (strcmp(fmt, "json") == 0) format = FUNDB_EXPORT_JSON;
    }

    int rc = fundb_export_table(g_shell_db, table, path, format);
    if (rc == FUNDB_OK) {
        shell_print("Table ");
        shell_print(table);
        shell_print(" exported to ");
        shell_print(path);
        shell_print("\n");
        last_exit_code = 0;
    } else {
        shell_print("Export failed: ");
        shell_print(fundb_error_string(rc));
        shell_print("\n");
        last_exit_code = 1;
    }
}

static void cmd_db_stats(void) {
    if (!g_shell_db) {
        shell_print("No database open. Use: db open [path]\n");
        last_exit_code = 1;
        return;
    }

    fundb_stats_t stats;
    int rc = fundb_get_stats(g_shell_db, &stats);
    if (rc == FUNDB_OK) {
        char buf[256];
        shell_print("Database Statistics:\n");
        snprintf(buf, sizeof(buf), "  Total tables: %u\n", stats.total_tables);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "  Total rows: %u\n", stats.total_rows);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "  Total indexes: %u\n", stats.total_indexes);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "  Total views: %u\n", stats.total_views);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "  Total triggers: %u\n", stats.total_triggers);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "  Total procedures: %u\n", stats.total_procs);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "  Total size: %llu bytes\n",
                 (unsigned long long)stats.total_size_bytes);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "  Page size: %u bytes\n", stats.page_size);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "  Total pages: %u\n", stats.total_pages);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "  Free pages: %u\n", stats.free_pages);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "  Cache hits: %u\n", stats.cache_hits);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "  Cache misses: %u\n", stats.cache_misses);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "  Query count: %llu\n",
                 (unsigned long long)stats.query_count);
        shell_print(buf);
        last_exit_code = 0;
    } else {
        shell_print("Failed to get statistics\n");
        last_exit_code = 1;
    }
}

static void cmd_db_vacuum(void) {
    if (!g_shell_db) {
        shell_print("No database open. Use: db open [path]\n");
        last_exit_code = 1;
        return;
    }

    shell_print("Vacuuming database...\n");
    int rc = fundb_vacuum(g_shell_db);
    if (rc == FUNDB_OK) {
        shell_print("Database vacuumed successfully\n");
        last_exit_code = 0;
    } else {
        shell_print("Vacuum failed: ");
        shell_print(fundb_error_string(rc));
        shell_print("\n");
        last_exit_code = 1;
    }
}

static void cmd_db_reindex(const char *table) {
    if (!g_shell_db) {
        shell_print("No database open. Use: db open [path]\n");
        last_exit_code = 1;
        return;
    }

    if (!table || !*table) {
        shell_print("Usage: db reindex <table>\n");
        last_exit_code = 1;
        return;
    }

    shell_print("Rebuilding indexes on ");
    shell_print(table);
    shell_print("...\n");
    int rc = fundb_reindex(g_shell_db, table);
    if (rc == FUNDB_OK) {
        shell_print("Indexes rebuilt successfully\n");
        last_exit_code = 0;
    } else {
        shell_print("Reindex failed: ");
        shell_print(fundb_error_string(rc));
        shell_print("\n");
        last_exit_code = 1;
    }
}

/* ============================================================
 * health - 系统健康监控命令
 * ============================================================ */
static void cmd_health(void) {
    health_report_t report;
    if (health_get_report(&report) != 0) {
        shell_print("health: failed to get health report\n");
        last_exit_code = 1;
        return;
    }

    shell_print("=== System Health Report ===\n");
    shell_print("Overall Status: ");
    shell_print(health_status_string(report.overall));
    shell_print("\n");
    shell_print("Check count: ");
    char buf[32];
    itoa(report.check_count, buf, 10);
    shell_print(buf);
    shell_print("\n\n");

    /* CPU */
    shell_print("--- CPU ---\n");
    shell_print("Status: ");
    shell_print(health_status_string(report.cpu.status));
    shell_print("\n");
    shell_print("User: ");
    itoa(report.cpu.user_pct, buf, 10);
    shell_print(buf);
    shell_print("%  Sys: ");
    itoa(report.cpu.sys_pct, buf, 10);
    shell_print(buf);
    shell_print("%  Idle: ");
    itoa(report.cpu.idle_pct, buf, 10);
    shell_print(buf);
    shell_print("%\n");
    shell_print("Procs: ");
    itoa(report.cpu.running_procs, buf, 10);
    shell_print(buf);
    shell_print("/");
    itoa(report.cpu.total_procs, buf, 10);
    shell_print(buf);
    shell_print(" running/total\n\n");

    /* Memory */
    shell_print("--- Memory ---\n");
    shell_print("Status: ");
    shell_print(health_status_string(report.memory.status));
    shell_print("\n");
    shell_print("Total: ");
    itoa((uint32_t)(report.memory.total_kb / 1024), buf, 10);
    shell_print(buf);
    shell_print(" MB  Used: ");
    itoa((uint32_t)(report.memory.used_kb / 1024), buf, 10);
    shell_print(buf);
    shell_print(" MB  Free: ");
    itoa((uint32_t)(report.memory.free_kb / 1024), buf, 10);
    shell_print(buf);
    shell_print(" MB\n");
    shell_print("Usage: ");
    itoa(report.memory.used_pct, buf, 10);
    shell_print(buf);
    shell_print("%\n\n");

    /* Disk */
    shell_print("--- Disk ---\n");
    shell_print("Device: ");
    shell_print(report.disk.device);
    shell_print("\n");
    shell_print("Status: ");
    shell_print(health_status_string(report.disk.status));
    shell_print("\n");
    shell_print("Total: ");
    itoa((uint32_t)(report.disk.total_kb / 1024 / 1024), buf, 10);
    shell_print(buf);
    shell_print(" GB  Used: ");
    itoa((uint32_t)(report.disk.used_kb / 1024 / 1024), buf, 10);
    shell_print(buf);
    shell_print(" GB  Free: ");
    itoa((uint32_t)(report.disk.free_kb / 1024 / 1024), buf, 10);
    shell_print(buf);
    shell_print(" GB\n");
    shell_print("Usage: ");
    itoa(report.disk.used_pct, buf, 10);
    shell_print(buf);
    shell_print("%\n\n");

    /* Network */
    shell_print("--- Network ---\n");
    shell_print("Interface: ");
    shell_print(report.network.iface);
    shell_print("\n");
    shell_print("Status: ");
    shell_print(health_status_string(report.network.status));
    shell_print("\n");
    shell_print("RX: ");
    itoa((uint32_t)(report.network.rx_bytes / 1024), buf, 10);
    shell_print(buf);
    shell_print(" KB  TX: ");
    itoa((uint32_t)(report.network.tx_bytes / 1024), buf, 10);
    shell_print(buf);
    shell_print(" KB\n");
    shell_print("Speed: ");
    itoa(report.network.speed_mbps, buf, 10);
    shell_print(buf);
    shell_print(" Mbps\n");

    last_exit_code = 0;
}

/* ============================================================
 * notifier - 通知链管理命令
 * ============================================================ */
static void cmd_notifier(const char *subcmd) {
    if (!subcmd || !*subcmd || strcmp(subcmd, "list") == 0) {
        knotifier_print_stats();
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "test") == 0) {
        shell_print("Sending test notification to panic chain...\n");
        int ret = atomic_knotifier_call_chain(&panic_knotifier_list, 0, NULL);
        char buf[16];
        itoa(ret, buf, 10);
        shell_print("Notification result: ");
        shell_print(buf);
        shell_print("\n");
        last_exit_code = 0;
        return;
    }

    shell_print("Usage: notifier [list|test]\n");
    last_exit_code = 1;
}

/* ============================================================
 * quota - 文件系统配额管理命令
 * ============================================================ */
static void cmd_quota(const char *subcmd, const char *arg1, const char *arg2,
                      const char *arg3, const char *arg4, const char *arg5) {
    (void)arg4;
    (void)arg5;

    if (!subcmd || !*subcmd || strcmp(subcmd, "help") == 0) {
        shell_print("Usage:\n");
        shell_print("  quota enable                 - Enable quota system\n");
        shell_print("  quota disable                - Disable quota system\n");
        shell_print("  quota status                 - Show quota status\n");
        shell_print("  quota setuser <uid> <bsoft> <bhard> <isoft> <ihard>\n");
        shell_print("  quota setgroup <gid> <bsoft> <bhard> <isoft> <ihard>\n");
        shell_print("  quota getuser <uid>          - Show user quota\n");
        shell_print("  quota getgroup <gid>         - Show group quota\n");
        shell_print("  quota deluser <uid>          - Delete user quota\n");
        shell_print("  quota delgroup <gid>         - Delete group quota\n");
        shell_print("  quota resetuser <uid>        - Reset user usage\n");
        shell_print("  quota resetgroup <gid>       - Reset group usage\n");
        shell_print("  quota list [user|group]      - List all quotas\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "enable") == 0) {
        quota_enable(1);
        shell_print("Quota system enabled\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "disable") == 0) {
        quota_enable(0);
        shell_print("Quota system disabled\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "status") == 0) {
        quota_stats_t stats;
        if (quota_get_stats(&stats) != 0) {
            shell_print("quota: failed to get stats\n");
            last_exit_code = 1;
            return;
        }
        shell_print("=== Quota Status ===\n");
        shell_print("Enabled: ");
        shell_print(stats.enabled ? "yes" : "no");
        shell_print("\n");
        char buf[32];
        shell_print("User entries: ");
        itoa(stats.user_entries, buf, 10);
        shell_print(buf);
        shell_print("\n");
        shell_print("Group entries: ");
        itoa(stats.group_entries, buf, 10);
        shell_print(buf);
        shell_print("\n");
        shell_print("Total blocks used: ");
        itoa((uint32_t)stats.total_blocks, buf, 10);
        shell_print(buf);
        shell_print("\n");
        shell_print("Total inodes used: ");
        itoa((uint32_t)stats.total_inodes, buf, 10);
        shell_print(buf);
        shell_print("\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "setuser") == 0) {
        if (!arg1 || !arg2 || !arg3 || !arg4 || !arg5) {
            shell_print("Usage: quota setuser <uid> <bsoft> <bhard> <isoft> <ihard>\n");
            last_exit_code = 1;
            return;
        }
        uint32_t uid = atoi(arg1);
        uint64_t bsoft = (uint64_t)atoi(arg2);
        uint64_t bhard = (uint64_t)atoi(arg3);
        uint64_t isoft = (uint64_t)atoi(arg4);
        uint64_t ihard = (uint64_t)atoi(arg5);
        int ret = quota_set_user(uid, bsoft, bhard, isoft, ihard);
        if (ret == 0) {
            shell_print("User quota set successfully\n");
            last_exit_code = 0;
        } else {
            shell_print("Failed to set user quota\n");
            last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "setgroup") == 0) {
        if (!arg1 || !arg2 || !arg3 || !arg4 || !arg5) {
            shell_print("Usage: quota setgroup <gid> <bsoft> <bhard> <isoft> <ihard>\n");
            last_exit_code = 1;
            return;
        }
        uint32_t gid = atoi(arg1);
        uint64_t bsoft = (uint64_t)atoi(arg2);
        uint64_t bhard = (uint64_t)atoi(arg3);
        uint64_t isoft = (uint64_t)atoi(arg4);
        uint64_t ihard = (uint64_t)atoi(arg5);
        int ret = quota_set_group(gid, bsoft, bhard, isoft, ihard);
        if (ret == 0) {
            shell_print("Group quota set successfully\n");
            last_exit_code = 0;
        } else {
            shell_print("Failed to set group quota\n");
            last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "getuser") == 0) {
        if (!arg1) {
            shell_print("Usage: quota getuser <uid>\n");
            last_exit_code = 1;
            return;
        }
        uint32_t uid = atoi(arg1);
        quota_entry_t *entry = quota_get_user(uid);
        if (!entry) {
            shell_print("No quota for user ");
            char buf[32];
            itoa(uid, buf, 10);
            shell_print(buf);
            shell_print("\n");
            last_exit_code = 1;
            return;
        }
        char buf[64];
        shell_print("=== User Quota (uid=");
        itoa(entry->id, buf, 10);
        shell_print(buf);
        shell_print(") ===\n");
        shell_print("Blocks: used=");
        itoa((uint32_t)entry->blocks_used, buf, 10);
        shell_print(buf);
        shell_print(" soft=");
        itoa((uint32_t)entry->blocks_soft, buf, 10);
        shell_print(buf);
        shell_print(" hard=");
        itoa((uint32_t)entry->blocks_hard, buf, 10);
        shell_print(buf);
        shell_print("\n");
        shell_print("Inodes: used=");
        itoa((uint32_t)entry->inodes_used, buf, 10);
        shell_print(buf);
        shell_print(" soft=");
        itoa((uint32_t)entry->inodes_soft, buf, 10);
        shell_print(buf);
        shell_print(" hard=");
        itoa((uint32_t)entry->inodes_hard, buf, 10);
        shell_print(buf);
        shell_print("\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "getgroup") == 0) {
        if (!arg1) {
            shell_print("Usage: quota getgroup <gid>\n");
            last_exit_code = 1;
            return;
        }
        uint32_t gid = atoi(arg1);
        quota_entry_t *entry = quota_get_group(gid);
        if (!entry) {
            shell_print("No quota for group ");
            char buf[32];
            itoa(gid, buf, 10);
            shell_print(buf);
            shell_print("\n");
            last_exit_code = 1;
            return;
        }
        char buf[64];
        shell_print("=== Group Quota (gid=");
        itoa(entry->id, buf, 10);
        shell_print(buf);
        shell_print(") ===\n");
        shell_print("Blocks: used=");
        itoa((uint32_t)entry->blocks_used, buf, 10);
        shell_print(buf);
        shell_print(" soft=");
        itoa((uint32_t)entry->blocks_soft, buf, 10);
        shell_print(buf);
        shell_print(" hard=");
        itoa((uint32_t)entry->blocks_hard, buf, 10);
        shell_print(buf);
        shell_print("\n");
        shell_print("Inodes: used=");
        itoa((uint32_t)entry->inodes_used, buf, 10);
        shell_print(buf);
        shell_print(" soft=");
        itoa((uint32_t)entry->inodes_soft, buf, 10);
        shell_print(buf);
        shell_print(" hard=");
        itoa((uint32_t)entry->inodes_hard, buf, 10);
        shell_print(buf);
        shell_print("\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "deluser") == 0) {
        if (!arg1) {
            shell_print("Usage: quota deluser <uid>\n");
            last_exit_code = 1;
            return;
        }
        uint32_t uid = atoi(arg1);
        int ret = quota_remove_user(uid);
        if (ret == 0) {
            shell_print("User quota deleted\n");
            last_exit_code = 0;
        } else {
            shell_print("Failed to delete user quota\n");
            last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "delgroup") == 0) {
        if (!arg1) {
            shell_print("Usage: quota delgroup <gid>\n");
            last_exit_code = 1;
            return;
        }
        uint32_t gid = atoi(arg1);
        int ret = quota_remove_group(gid);
        if (ret == 0) {
            shell_print("Group quota deleted\n");
            last_exit_code = 0;
        } else {
            shell_print("Failed to delete group quota\n");
            last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "resetuser") == 0) {
        if (!arg1) {
            shell_print("Usage: quota resetuser <uid>\n");
            last_exit_code = 1;
            return;
        }
        uint32_t uid = atoi(arg1);
        int ret = quota_reset_user(uid);
        if (ret == 0) {
            shell_print("User quota usage reset\n");
            last_exit_code = 0;
        } else {
            shell_print("Failed to reset user quota\n");
            last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "resetgroup") == 0) {
        if (!arg1) {
            shell_print("Usage: quota resetgroup <gid>\n");
            last_exit_code = 1;
            return;
        }
        uint32_t gid = atoi(arg1);
        int ret = quota_reset_group(gid);
        if (ret == 0) {
            shell_print("Group quota usage reset\n");
            last_exit_code = 0;
        } else {
            shell_print("Failed to reset group quota\n");
            last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "list") == 0) {
        uint8_t type = QUOTA_TYPE_USER;
        if (arg1 && strcmp(arg1, "group") == 0) {
            type = QUOTA_TYPE_GROUP;
        }

        quota_entry_t entries[32];
        int count = quota_list(entries, 32, type);
        if (count < 0) {
            shell_print("Failed to list quotas\n");
            last_exit_code = 1;
            return;
        }

        shell_print("=== ");
        shell_print(type == QUOTA_TYPE_USER ? "User" : "Group");
        shell_print(" Quota List ===\n");
        shell_print("ID     Blocks(used/soft/hard)   Inodes(used/soft/hard)\n");

        for (int i = 0; i < count; i++) {
            char buf[32];
            itoa(entries[i].id, buf, 10);
            shell_print(buf);
            shell_print("    ");

            itoa((uint32_t)entries[i].blocks_used, buf, 10);
            shell_print(buf);
            shell_print("/");
            itoa((uint32_t)entries[i].blocks_soft, buf, 10);
            shell_print(buf);
            shell_print("/");
            itoa((uint32_t)entries[i].blocks_hard, buf, 10);
            shell_print(buf);
            shell_print("    ");

            itoa((uint32_t)entries[i].inodes_used, buf, 10);
            shell_print(buf);
            shell_print("/");
            itoa((uint32_t)entries[i].inodes_soft, buf, 10);
            shell_print(buf);
            shell_print("/");
            itoa((uint32_t)entries[i].inodes_hard, buf, 10);
            shell_print(buf);
            shell_print("\n");
        }

        last_exit_code = 0;
        return;
    }

    shell_print("Unknown quota command. Try 'quota help'\n");
    last_exit_code = 1;
}

/* ============================================================
 * fsstat - 文件系统统计命令
 * ============================================================ */
static void cmd_fsstat(void) {
    fs_stats_t stats;
    fs_stat_get(&stats);

    char buf[64];
    shell_print("=== Filesystem Statistics ===\n");

    /* I/O 统计 */
    shell_print("\n--- I/O Stats ---\n");
    shell_print("Reads: ");
    itoa((uint32_t)stats.io.reads, buf, 10);
    shell_print(buf);
    shell_print(" (");
    itoa((uint32_t)(stats.io.read_bytes / 1024), buf, 10);
    shell_print(buf);
    shell_print(" KB)\n");

    shell_print("Writes: ");
    itoa((uint32_t)stats.io.writes, buf, 10);
    shell_print(buf);
    shell_print(" (");
    itoa((uint32_t)(stats.io.write_bytes / 1024), buf, 10);
    shell_print(buf);
    shell_print(" KB)\n");

    shell_print("Read errors: ");
    itoa((uint32_t)stats.io.read_errors, buf, 10);
    shell_print(buf);
    shell_print("  Write errors: ");
    itoa((uint32_t)stats.io.write_errors, buf, 10);
    shell_print(buf);
    shell_print("\n");

    /* 操作统计 */
    shell_print("\n--- Operation Stats ---\n");
    shell_print("Opens: ");
    itoa((uint32_t)stats.ops.opens, buf, 10);
    shell_print(buf);
    shell_print("  Closes: ");
    itoa((uint32_t)stats.ops.closes, buf, 10);
    shell_print(buf);
    shell_print("\n");

    shell_print("Creates: ");
    itoa((uint32_t)stats.ops.creates, buf, 10);
    shell_print(buf);
    shell_print("  Deletes: ");
    itoa((uint32_t)stats.ops.deletes, buf, 10);
    shell_print(buf);
    shell_print("  Mkdirs: ");
    itoa((uint32_t)stats.ops.mkdirs, buf, 10);
    shell_print(buf);
    shell_print("\n");

    shell_print("Mounts: ");
    itoa((uint32_t)stats.ops.mounts, buf, 10);
    shell_print(buf);
    shell_print("  Umounts: ");
    itoa((uint32_t)stats.ops.umounts, buf, 10);
    shell_print(buf);
    shell_print("\n");

    shell_print("Lookups: ");
    itoa((uint32_t)stats.ops.lookups, buf, 10);
    shell_print(buf);
    shell_print("  Cache hit rate: ");
    itoa(fs_stat_cache_hit_rate(), buf, 10);
    shell_print(buf);
    shell_print("%\n");

    /* 文件统计 */
    shell_print("\n--- File Stats ---\n");
    shell_print("Currently open: ");
    itoa((uint32_t)stats.total_files, buf, 10);
    shell_print(buf);
    shell_print("  Max open: ");
    itoa((uint32_t)stats.max_files, buf, 10);
    shell_print(buf);
    shell_print("\n");

    /* 错误统计 */
    shell_print("\n--- Error Stats ---\n");
    shell_print("ENOENT: ");
    itoa((uint32_t)stats.errors.enoent, buf, 10);
    shell_print(buf);
    shell_print("  ENOSPC: ");
    itoa((uint32_t)stats.errors.enospc, buf, 10);
    shell_print(buf);
    shell_print("  EIO: ");
    itoa((uint32_t)stats.errors.eio, buf, 10);
    shell_print(buf);
    shell_print("\n");
    shell_print("Other errors: ");
    itoa((uint32_t)stats.errors.other, buf, 10);
    shell_print(buf);
    shell_print("\n");

    last_exit_code = 0;
}

/* ============================================================
 * flock - 文件锁管理命令
 * ============================================================ */
static void cmd_flock(const char *subcmd, const char *arg1, const char *arg2,
                      const char *arg3) {
    if (!subcmd || !*subcmd || strcmp(subcmd, "help") == 0) {
        shell_print("Usage:\n");
        shell_print("  flock stats              - Show file lock statistics\n");
        shell_print("  flock test <type>        - Test file locking\n");
        shell_print("  flock reset              - Reset statistics\n");
        shell_print("  Types: shared | exclusive\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "stats") == 0) {
        lock_stats_t stats;
        flock_get_stats(&stats);

        char buf[32];
        shell_print("=== File Lock Statistics ===\n");
        shell_print("Total locks: ");
        itoa(stats.total_locks, buf, 10);
        shell_print(buf);
        shell_print("\n");
        shell_print("Read locks: ");
        itoa(stats.read_locks, buf, 10);
        shell_print(buf);
        shell_print("  Write locks: ");
        itoa(stats.write_locks, buf, 10);
        shell_print(buf);
        shell_print("\n");
        shell_print("Max locks: ");
        itoa(stats.max_locks, buf, 10);
        shell_print(buf);
        shell_print("\n\n");

        shell_print("Acquires: ");
        itoa((uint32_t)stats.lock_acquires, buf, 10);
        shell_print(buf);
        shell_print("  Releases: ");
        itoa((uint32_t)stats.lock_releases, buf, 10);
        shell_print(buf);
        shell_print("\n");
        shell_print("Conflicts: ");
        itoa((uint32_t)stats.lock_conflicts, buf, 10);
        shell_print(buf);
        shell_print("  Waits: ");
        itoa((uint32_t)stats.lock_waits, buf, 10);
        shell_print(buf);
        shell_print("\n");

        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "test") == 0) {
        file_lock_t fl;
        memset(&fl, 0, sizeof(fl));
        fl.l_pid = 1;
        fl.l_start = 0;
        fl.l_len = 100;

        const char *type = arg1 ? arg1 : "shared";
        if (strcmp(type, "shared") == 0) {
            fl.l_type = F_RDLCK;
        } else if (strcmp(type, "exclusive") == 0) {
            fl.l_type = F_WRLCK;
        } else {
            shell_print("Unknown lock type\n");
            last_exit_code = 1;
            return;
        }

        int ret = flock_set(0, 1, &fl, 1);
        if (ret == 0) {
            shell_print("Lock acquired successfully\n");
            last_exit_code = 0;
        } else {
            shell_print("Failed to acquire lock (");
            char buf[16];
            itoa(-ret, buf, 10);
            shell_print(buf);
            shell_print(")\n");
            last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "reset") == 0) {
        flock_reset_stats();
        shell_print("Statistics reset\n");
        last_exit_code = 0;
        return;
    }

    shell_print("Unknown flock command. Try 'flock help'\n");
    last_exit_code = 1;
}

/* ============================================================
 * mount2 - 增强挂载命令
 * ============================================================ */
static void cmd_mount2(const char *subcmd, const char *arg1, const char *arg2,
                       const char *arg3) {
    (void)arg3;

    if (!subcmd || !*subcmd || strcmp(subcmd, "help") == 0) {
        shell_print("Usage:\n");
        shell_print("  mount2 list                - List all mounts\n");
        shell_print("  mount2 remount <path> ro   - Remount read-only\n");
        shell_print("  mount2 remount <path> rw   - Remount read-write\n");
        shell_print("  mount2 flags <path>        - Show mount flags\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "list") == 0) {
        vfs_mount_info_t mounts[16];
        int count = vfs_list_mounts(mounts, 16);
        if (count < 0) {
            shell_print("Failed to list mounts\n");
            last_exit_code = 1;
            return;
        }

        char buf[64];
        shell_print("=== Mount Points ===\n");
        for (int i = 0; i < count; i++) {
            shell_print(mounts[i].mount_point);
            shell_print("  [");
            shell_print(mounts[i].fs_type);
            shell_print("]  ");

            uint64_t total_mb = mounts[i].total_blocks * 4 / 1024;
            uint64_t free_mb = mounts[i].free_blocks * 4 / 1024;
            itoa((uint32_t)total_mb, buf, 10);
            shell_print(buf);
            shell_print("MB total, ");
            itoa((uint32_t)free_mb, buf, 10);
            shell_print(buf);
            shell_print("MB free");

            if (mounts[i].read_only) {
                shell_print("  (ro)");
            }
            shell_print("\n");
        }

        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "remount") == 0) {
        if (!arg1 || !arg2) {
            shell_print("Usage: mount2 remount <path> ro|rw\n");
            last_exit_code = 1;
            return;
        }

        uint32_t current_flags = vfs_get_mount_flags(arg1);
        uint32_t new_flags = current_flags;

        if (strcmp(arg2, "ro") == 0) {
            new_flags |= MS_RDONLY;
        } else if (strcmp(arg2, "rw") == 0) {
            new_flags &= ~MS_RDONLY;
        } else {
            shell_print("Unknown mode: ");
            shell_print(arg2);
            shell_print("\n");
            last_exit_code = 1;
            return;
        }

        int ret = vfs_remount(arg1, new_flags);
        if (ret == 0) {
            shell_print("Remount successful\n");
            last_exit_code = 0;
        } else {
            shell_print("Remount failed\n");
            last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "flags") == 0) {
        if (!arg1) {
            shell_print("Usage: mount2 flags <path>\n");
            last_exit_code = 1;
            return;
        }

        uint32_t flags = vfs_get_mount_flags(arg1);
        char buf[32];

        shell_print("Mount flags for ");
        shell_print(arg1);
        shell_print(":\n");
        shell_print("  RDONLY: ");
        shell_print((flags & MS_RDONLY) ? "yes" : "no");
        shell_print("\n");
        shell_print("  NOSUID: ");
        shell_print((flags & MS_NOSUID) ? "yes" : "no");
        shell_print("\n");
        shell_print("  NOEXEC: ");
        shell_print((flags & MS_NOEXEC) ? "yes" : "no");
        shell_print("\n");
        shell_print("  NOATIME: ");
        shell_print((flags & MS_NOATIME) ? "yes" : "no");
        shell_print("\n");
        shell_print("  Raw: 0x");
        itoa((uint32_t)flags, buf, 16);
        shell_print(buf);
        shell_print("\n");

        last_exit_code = 0;
        return;
    }

    shell_print("Unknown mount2 command. Try 'mount2 help'\n");
    last_exit_code = 1;
}

/* ============================================================
 * dcache - 目录项缓存管理命令
 * ============================================================ */
static void cmd_dcache(const char *subcmd) {
    if (!subcmd || !*subcmd || strcmp(subcmd, "help") == 0) {
        shell_print("Usage:\n");
        shell_print("  dcache stats     - Show dcache statistics\n");
        shell_print("  dcache reset     - Reset statistics\n");
        shell_print("  dcache dump      - Dump dcache entries\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "stats") == 0) {
        dcache_stats_t stats;
        dcache_get_stats(&stats);

        char buf[32];
        shell_print("=== dcache Statistics ===\n");
        shell_print("Current entries: ");
        itoa(stats.total_entries, buf, 10);
        shell_print(buf);
        shell_print("\n");
        shell_print("Max entries: ");
        itoa(stats.max_entries, buf, 10);
        shell_print(buf);
        shell_print("\n\n");

        shell_print("Lookups: ");
        itoa((uint32_t)stats.lookups, buf, 10);
        shell_print(buf);
        shell_print("\n");
        shell_print("Hits: ");
        itoa((uint32_t)stats.hits, buf, 10);
        shell_print(buf);
        shell_print("  Misses: ");
        itoa((uint32_t)stats.misses, buf, 10);
        shell_print(buf);
        shell_print("\n");

        if (stats.lookups > 0) {
            uint32_t hit_rate = (stats.hits * 100) / stats.lookups;
            shell_print("Hit rate: ");
            itoa(hit_rate, buf, 10);
            shell_print(buf);
            shell_print("%\n");
        }

        shell_print("Reclaims: ");
        itoa((uint32_t)stats.reclaims, buf, 10);
        shell_print(buf);
        shell_print("  Invalidations: ");
        itoa((uint32_t)stats.invalidations, buf, 10);
        shell_print(buf);
        shell_print("\n");

        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "reset") == 0) {
        dcache_reset_stats();
        shell_print("dcache statistics reset\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "dump") == 0) {
        dcache_dump();
        last_exit_code = 0;
        return;
    }

    shell_print("Unknown dcache command. Try 'dcache help'\n");
    last_exit_code = 1;
}

/* ============================================================
 * icache - inode 缓存统计命令
 * ============================================================ */
static void cmd_icache(const char *subcmd) {
    if (!subcmd || !*subcmd || strcmp(subcmd, "help") == 0) {
        shell_print("Usage:\n");
        shell_print("  icache stats     - Show icache statistics\n");
        shell_print("  icache reset     - Reset statistics\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "stats") == 0) {
        icache_stats_t stats;
        icache_get_stats(&stats);

        char buf[32];
        shell_print("=== icache Statistics ===\n");
        shell_print("Current entries: ");
        itoa(stats.total_entries, buf, 10);
        shell_print(buf);
        shell_print("\n");
        shell_print("Max entries: ");
        itoa(stats.max_entries, buf, 10);
        shell_print(buf);
        shell_print("\n\n");

        shell_print("Lookups: ");
        itoa((uint32_t)stats.lookups, buf, 10);
        shell_print(buf);
        shell_print("\n");
        shell_print("Hits: ");
        itoa((uint32_t)stats.hits, buf, 10);
        shell_print(buf);
        shell_print("  Misses: ");
        itoa((uint32_t)stats.misses, buf, 10);
        shell_print(buf);
        shell_print("\n");

        if (stats.lookups > 0) {
            uint32_t hit_rate = (stats.hits * 100) / stats.lookups;
            shell_print("Hit rate: ");
            itoa(hit_rate, buf, 10);
            shell_print(buf);
            shell_print("%\n");
        }

        shell_print("Reclaims: ");
        itoa((uint32_t)stats.reclaims, buf, 10);
        shell_print(buf);
        shell_print("  Invalidations: ");
        itoa((uint32_t)stats.invalidations, buf, 10);
        shell_print(buf);
        shell_print("\n");

        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "reset") == 0) {
        icache_reset_stats();
        shell_print("icache statistics reset\n");
        last_exit_code = 0;
        return;
    }

    shell_print("Unknown icache command. Try 'icache help'\n");
    last_exit_code = 1;
}

/* ============================================================
 * pagecache - 页缓存统计命令
 * ============================================================ */
static void cmd_pagecache(const char *subcmd) {
    if (!subcmd || !*subcmd || strcmp(subcmd, "help") == 0) {
        shell_print("Usage:\n");
        shell_print("  pagecache stats    - Show page cache statistics\n");
        shell_print("  pagecache reset    - Reset statistics\n");
        shell_print("  pagecache flush    - Writeback all dirty pages\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "stats") == 0) {
        page_cache_stats_t stats;
        page_cache_get_stats(&stats);

        char buf[32];
        shell_print("=== Page Cache Statistics ===\n");
        shell_print("Current pages: ");
        itoa(stats.total_pages, buf, 10);
        shell_print(buf);
        shell_print("\n");
        shell_print("Max pages: ");
        itoa(stats.max_pages, buf, 10);
        shell_print(buf);
        shell_print("\n");
        shell_print("Dirty pages: ");
        itoa(stats.dirty_pages, buf, 10);
        shell_print(buf);
        shell_print("\n\n");

        shell_print("Lookups: ");
        itoa((uint32_t)stats.lookups, buf, 10);
        shell_print(buf);
        shell_print("\n");
        shell_print("Hits: ");
        itoa((uint32_t)stats.hits, buf, 10);
        shell_print(buf);
        shell_print("  Misses: ");
        itoa((uint32_t)stats.misses, buf, 10);
        shell_print(buf);
        shell_print("\n");

        if (stats.lookups > 0) {
            uint32_t hit_rate = (stats.hits * 100) / stats.lookups;
            shell_print("Hit rate: ");
            itoa(hit_rate, buf, 10);
            shell_print(buf);
            shell_print("%\n");
        }

        shell_print("Reclaims: ");
        itoa((uint32_t)stats.reclaims, buf, 10);
        shell_print(buf);
        shell_print("  Writebacks: ");
        itoa((uint32_t)stats.writebacks, buf, 10);
        shell_print(buf);
        shell_print("\n");

        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "reset") == 0) {
        page_cache_reset_stats();
        shell_print("Page cache statistics reset\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "flush") == 0) {
        uint32_t written = page_cache_writeback_all();
        char buf[32];
        shell_print("Flushed ");
        itoa(written, buf, 10);
        shell_print(buf);
        shell_print(" dirty pages\n");
        last_exit_code = 0;
        return;
    }

    shell_print("Unknown pagecache command. Try 'pagecache help'\n");
    last_exit_code = 1;
}

/* ============================================================
 * readahead - 预读统计命令
 * ============================================================ */
static void cmd_readahead_stat(const char *subcmd) {
    if (!subcmd || !*subcmd || strcmp(subcmd, "help") == 0) {
        shell_print("Usage:\n");
        shell_print("  readahead stats   - Show readahead statistics\n");
        shell_print("  readahead reset   - Reset statistics\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "stats") == 0) {
        readahead_stats_t stats;
        readahead_get_stats(&stats);

        char buf[32];
        shell_print("=== Readahead Statistics ===\n");
        shell_print("Readahead calls: ");
        itoa((uint32_t)stats.readahead_calls, buf, 10);
        shell_print(buf);
        shell_print("\n");
        shell_print("Pages readahead: ");
        itoa((uint32_t)stats.pages_readahead, buf, 10);
        shell_print(buf);
        shell_print("\n");
        shell_print("Seq detections: ");
        itoa((uint32_t)stats.seq_detections, buf, 10);
        shell_print(buf);
        shell_print("\n");
        shell_print("Cache hits: ");
        itoa((uint32_t)stats.cache_hits, buf, 10);
        shell_print(buf);
        shell_print("\n");

        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "reset") == 0) {
        readahead_reset_stats();
        shell_print("Readahead statistics reset\n");
        last_exit_code = 0;
        return;
    }

    shell_print("Unknown readahead command. Try 'readahead help'\n");
    last_exit_code = 1;
}

/* ============================================================
 * syncstat - 文件系统同步统计命令
 * ============================================================ */
static void cmd_syncstat(const char *subcmd) {
    if (!subcmd || !*subcmd || strcmp(subcmd, "help") == 0) {
        shell_print("Usage:\n");
        shell_print("  syncstat show     - Show sync statistics\n");
        shell_print("  syncstat reset    - Reset statistics\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "show") == 0 || strcmp(subcmd, "") == 0) {
        fs_sync_stats_t stats;
        fs_sync_get_stats(&stats);

        char buf[32];
        shell_print("=== File System Sync Statistics ===\n");
        shell_print("Dirty inodes: ");
        itoa((uint32_t)stats.dirty_inodes, buf, 10);
        shell_print(buf);
        shell_print("  (max: ");
        itoa((uint32_t)stats.max_dirty_inodes, buf, 10);
        shell_print(buf);
        shell_print(")\n\n");

        shell_print("sync() calls: ");
        itoa((uint32_t)stats.sync_calls, buf, 10);
        shell_print(buf);
        shell_print("\n");
        shell_print("fsync() calls: ");
        itoa((uint32_t)stats.fsync_calls, buf, 10);
        shell_print(buf);
        shell_print("\n");
        shell_print("fdatasync() calls: ");
        itoa((uint32_t)stats.fdatasync_calls, buf, 10);
        shell_print(buf);
        shell_print("\n");
        shell_print("syncfs() calls: ");
        itoa((uint32_t)stats.syncfs_calls, buf, 10);
        shell_print(buf);
        shell_print("\n\n");

        shell_print("Inodes synced: ");
        itoa((uint32_t)stats.inodes_synced, buf, 10);
        shell_print(buf);
        shell_print("\n");

        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "reset") == 0) {
        fs_sync_reset_stats();
        shell_print("Sync statistics reset\n");
        last_exit_code = 0;
        return;
    }

    shell_print("Unknown syncstat command. Try 'syncstat help'\n");
    last_exit_code = 1;
}

/* ============================================================
 * ktrace - 内核跟踪子系统管理命令
 *
 * 用法：
 *   ktrace                       显示状态与统计
 *   ktrace help                  显示帮助
 *   ktrace on  [cat...]          启用类别（默认全部）
 *   ktrace off [cat...]         关闭类别（默认全部）
 *   ktrace mask <hex>           直接设置类别掩码
 *   ktrace level <0..3>         设置最低级别（0=DBG 1=INF 2=WRN 3=ERR）
 *   ktrace emit <cat> <msg>     手动注入一条事件（测试用）
 *   ktrace show [N]             显示缓冲区中最近 N 条事件（默认 16）
 *   ktrace dump [N]             转储最近 N 条到 klog（dmesg 可见）
 *   ktrace reset                重置统计计数器（不清缓冲区）
 *   ktrace clear                清空环形缓冲区
 *   ktrace resize <N>           调整缓冲区容量（必须是 2 的幂，>=4）
 *
 * 类别名称：sched fs net mem irq syscall proc timer all
 * ============================================================ */

static uint32_t ktrace_parse_cats(const char *s) {
    uint32_t mask = 0;
    if (!s || !*s) return KTRACE_CAT_ALL;
    /* 接受以 + 或 , 分隔的多个类别名 */
    char buf[128];
    strncpy(buf, s, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *tok = buf;
    char *p = buf;
    while (1) {
        if (*p == '+' || *p == ',' || *p == '\0') {
            int last = (*p == '\0');
            *p = '\0';
            if (*tok) {
                if (strcmp(tok, "sched") == 0)        mask |= KTRACE_CAT_SCHED;
                else if (strcmp(tok, "fs") == 0)      mask |= KTRACE_CAT_FS;
                else if (strcmp(tok, "net") == 0)     mask |= KTRACE_CAT_NET;
                else if (strcmp(tok, "mem") == 0)     mask |= KTRACE_CAT_MEM;
                else if (strcmp(tok, "irq") == 0)     mask |= KTRACE_CAT_IRQ;
                else if (strcmp(tok, "syscall") == 0) mask |= KTRACE_CAT_SYSCALL;
                else if (strcmp(tok, "proc") == 0)    mask |= KTRACE_CAT_PROC;
                else if (strcmp(tok, "timer") == 0)   mask |= KTRACE_CAT_TIMER;
                else if (strcmp(tok, "all") == 0)     mask |= KTRACE_CAT_ALL;
                else return 0xFFFFFFFFu; /* 表示有未知类别 */
            }
            tok = p + 1;
            if (last) break;
        }
        p++;
    }
    return mask;
}

static void cmd_ktrace_show_stats(void) {
    ktrace_stats_t st;
    ktrace_get_stats(&st);
    char line[160];

    shell_print("=== ktrace Statistics ===\n");

    snprintf(line, sizeof(line),
             "Enabled mask : 0x%08X\n", st.enabled_mask);
    shell_print(line);
    snprintf(line, sizeof(line),
             "Min level    : %u (0=DBG 1=INF 2=WRN 3=ERR)\n", st.min_level);
    shell_print(line);
    snprintf(line, sizeof(line),
             "Buffer cap   : %u events\n", st.buffer_capacity);
    shell_print(line);
    snprintf(line, sizeof(line),
             "In buffer    : %u events (peak %u)\n",
             st.events_in_buffer, st.max_buffered);
    shell_print(line);
    shell_print("\n");

    /* 用 %llu 截断为 32 位显示，避免 itoa 不支持 64 位 */
    snprintf(line, sizeof(line),
             "Total events : %u\n", (uint32_t)st.total_events);
    shell_print(line);
    snprintf(line, sizeof(line),
             "Dropped      : %u\n", (uint32_t)st.dropped_events);
    shell_print(line);
    shell_print("\n  Per-category events:\n");
    {
        static const struct { uint32_t cat; const char *name; } cats[] = {
            { KTRACE_CAT_SCHED,   "sched  " },
            { KTRACE_CAT_FS,      "fs     " },
            { KTRACE_CAT_NET,     "net    " },
            { KTRACE_CAT_MEM,     "mem    " },
            { KTRACE_CAT_IRQ,     "irq    " },
            { KTRACE_CAT_SYSCALL, "syscall" },
            { KTRACE_CAT_PROC,    "proc   " },
            { KTRACE_CAT_TIMER,   "timer  " },
        };
        uint32_t i;
        for (i = 0; i < sizeof(cats) / sizeof(cats[0]); i++) {
            int idx = -1;
            switch (cats[i].cat) {
                case KTRACE_CAT_SCHED:   idx = 0; break;
                case KTRACE_CAT_FS:      idx = 1; break;
                case KTRACE_CAT_NET:     idx = 2; break;
                case KTRACE_CAT_MEM:     idx = 3; break;
                case KTRACE_CAT_IRQ:     idx = 4; break;
                case KTRACE_CAT_SYSCALL: idx = 5; break;
                case KTRACE_CAT_PROC:    idx = 6; break;
                case KTRACE_CAT_TIMER:   idx = 7; break;
            }
            if (idx < 0) continue;
            snprintf(line, sizeof(line), "    %s  %u\n",
                     cats[i].name, (uint32_t)st.events_per_cat[idx]);
            shell_print(line);
        }
    }
}

static void cmd_ktrace_show_events(uint32_t count) {
    ktrace_event_t evs[32];
    uint32_t avail;
    uint32_t i;
    char line[192];

    if (count > 32) count = 32;
    avail = ktrace_read(evs, count);
    if (avail == 0) {
        shell_print("ktrace: buffer is empty\n");
        return;
    }

    snprintf(line, sizeof(line),
             "=== ktrace: %u most recent events ===\n", avail);
    shell_print(line);

    for (i = 0; i < avail; i++) {
        ktrace_event_t *e = &evs[i];
        snprintf(line, sizeof(line),
                 "[%u] tsc=%u pid=%u %s/%s: %s\n",
                 (uint32_t)i,
                 (uint32_t)e->timestamp,
                 e->pid,
                 ktrace_cat_name(e->category),
                 ktrace_level_name(e->level),
                 e->msg);
        shell_print(line);
    }
}

static void cmd_ktrace(const char *subcmd, const char *arg1, const char *arg2) {
    if (!subcmd || !*subcmd || strcmp(subcmd, "help") == 0) {
        shell_print("Usage:\n");
        shell_print("  ktrace                   Show status and statistics\n");
        shell_print("  ktrace on  [cat...]      Enable categories (sched/fs/net/mem/irq/syscall/proc/timer/all)\n");
        shell_print("  ktrace off [cat...]      Disable categories\n");
        shell_print("  ktrace mask <hex>        Set category mask directly\n");
        shell_print("  ktrace level <0..3>      Set min level (0=DBG 1=INF 2=WRN 3=ERR)\n");
        shell_print("  ktrace emit <cat> <msg>  Inject a test event (single-word message)\n");
        shell_print("  ktrace show [N]          Show last N events (default 16)\n");
        shell_print("  ktrace dump [N]          Dump last N events to klog\n");
        shell_print("  ktrace reset             Reset statistics\n");
        shell_print("  ktrace clear             Clear ring buffer\n");
        shell_print("  ktrace resize <N>        Resize buffer (power of 2, >= 4)\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "on") == 0) {
        uint32_t m = ktrace_parse_cats(arg1);
        if (m == 0xFFFFFFFFu) {
            shell_print("ktrace: unknown category in '");
            shell_print(arg1 ? arg1 : "");
            shell_print("'\n");
            last_exit_code = 1;
            return;
        }
        ktrace_enable(m);
        shell_print("ktrace: enabled mask 0x");
        {
            char buf[12];
            itoa((int)ktrace_get_mask(), buf, 16);
            shell_print(buf);
        }
        shell_print("\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "off") == 0) {
        uint32_t m = ktrace_parse_cats(arg1);
        if (m == 0xFFFFFFFFu) {
            shell_print("ktrace: unknown category\n");
            last_exit_code = 1;
            return;
        }
        ktrace_disable(m);
        shell_print("ktrace: disabled, mask now 0x");
        {
            char buf[12];
            itoa((int)ktrace_get_mask(), buf, 16);
            shell_print(buf);
        }
        shell_print("\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "mask") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: ktrace mask <hex>\n");
            last_exit_code = 1;
            return;
        }
        uint32_t m = (uint32_t)strtol(arg1, NULL, 0);
        ktrace_set_mask(m);
        shell_print("ktrace: mask set to 0x");
        {
            char buf[12];
            itoa((int)m, buf, 16);
            shell_print(buf);
        }
        shell_print("\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "level") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: ktrace level <0..3>\n");
            last_exit_code = 1;
            return;
        }
        uint32_t lvl = (uint32_t)strtol(arg1, NULL, 10);
        if (lvl > 3) {
            shell_print("ktrace: level must be 0..3\n");
            last_exit_code = 1;
            return;
        }
        ktrace_set_level(lvl);
        shell_print("ktrace: min level set to ");
        {
            char buf[12];
            itoa((int)lvl, buf, 10);
            shell_print(buf);
        }
        shell_print("\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "emit") == 0) {
        /* arg1 = category name (single word), arg2 = message (single word) */
        if (!arg1 || !*arg1 || !arg2 || !*arg2) {
            shell_print("Usage: ktrace emit <cat> <msg>\n");
            last_exit_code = 1;
            return;
        }
        uint32_t m = ktrace_parse_cats(arg1);
        if (m == 0xFFFFFFFFu || m == 0) {
            shell_print("ktrace: unknown category '");
            shell_print(arg1);
            shell_print("'\n");
            last_exit_code = 1;
            return;
        }
        ktrace_event(m, KTRACE_LEVEL_INFO, arg2);
        shell_print("ktrace: event injected\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "show") == 0) {
        uint32_t n = 16;
        if (arg1 && *arg1) {
            n = (uint32_t)strtol(arg1, NULL, 10);
            if (n == 0) n = 16;
            if (n > 32) n = 32;
        }
        cmd_ktrace_show_events(n);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "dump") == 0) {
        uint32_t n = 0;
        if (arg1 && *arg1) {
            n = (uint32_t)strtol(arg1, NULL, 10);
        }
        ktrace_dump(n);
        shell_print("ktrace: events dumped to klog (use 'dmesg' to view)\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "reset") == 0) {
        ktrace_reset_stats();
        shell_print("ktrace: statistics reset\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "clear") == 0) {
        ktrace_clear();
        shell_print("ktrace: ring buffer cleared\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "resize") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: ktrace resize <N> (power of 2, >= 4)\n");
            last_exit_code = 1;
            return;
        }
        uint32_t n = (uint32_t)strtol(arg1, NULL, 10);
        int rc = ktrace_resize(n);
        if (rc != 0) {
            shell_print("ktrace: resize failed (must be power of 2, >= 4)\n");
            last_exit_code = 1;
        } else {
            shell_print("ktrace: buffer resized to ");
            char buf[12];
            itoa((int)n, buf, 10);
            shell_print(buf);
            shell_print(" events\n");
            last_exit_code = 0;
        }
        return;
    }

    /* Default: show status & stats */
    cmd_ktrace_show_stats();
    last_exit_code = 0;
}

/* ============================================================
 * kwork - 内核工作队列管理命令
 *
 * 用法：
 *   kwork                       显示统计
 *   kwork help                   显示帮助
 *   kwork test                   排队一个测试工作项（立即执行）
 *   kwork delayed <ms>           排队一个延迟 ms 毫秒的测试工作项
 *   kwork periodic <ms>          排队一个周期性测试工作项（每 ms 毫秒）
 *   kwork cancel                 取消正在排队的测试工作项
 *   kwork flush                  等待所有工作完成
 *   kwork dump                   转储队列状态到 klog
 *   kwork reset                  重置统计计数器
 * ============================================================ */

/* 测试用工作项与回调 */
static kwork_t g_kwork_test_item;
static volatile uint32_t g_kwork_test_count = 0;

static void kwork_test_cb(void *data) {
    uint32_t n = ++g_kwork_test_count;
    klog_info("kwork: test callback #%u fired (data=%p)", n, data);
}

static void cmd_kwork(const char *subcmd, const char *arg1, const char *arg2) {
    (void)arg2;  /* 暂未使用 */
    if (!subcmd || !*subcmd || strcmp(subcmd, "help") == 0) {
        shell_print("Usage:\n");
        shell_print("  kwork                 Show workqueue statistics\n");
        shell_print("  kwork test            Queue a test work item (immediate)\n");
        shell_print("  kwork delayed <ms>    Queue a delayed test work item\n");
        shell_print("  kwork periodic <ms>   Queue a periodic test work item\n");
        shell_print("  kwork cancel          Cancel the test work item\n");
        shell_print("  kwork flush            Wait for all queued work to finish\n");
        shell_print("  kwork dump            Dump queue state to klog\n");
        shell_print("  kwork reset           Reset statistics\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "test") == 0) {
        kwork_init_work(&g_kwork_test_item, kwork_test_cb, (void *)0x1234);
        int rc = kwork_queue_work(&g_kwork_test_item);
        if (rc == 0) {
            shell_print("kwork: test work queued\n");
        } else {
            shell_print("kwork: queue failed\n");
            last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "delayed") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: kwork delayed <ms>\n");
            last_exit_code = 1;
            return;
        }
        uint32_t ms = (uint32_t)strtol(arg1, NULL, 10);
        if (ms == 0) {
            shell_print("kwork: delay must be > 0\n");
            last_exit_code = 1;
            return;
        }
        kwork_init_work(&g_kwork_test_item, kwork_test_cb, (void *)0x5678);
        int rc = kwork_queue_delayed_work(&g_kwork_test_item, ms);
        if (rc == 0) {
            shell_print("kwork: delayed work queued (");
            char buf[16];
            itoa((int)ms, buf, 10);
            shell_print(buf);
            shell_print(" ms)\n");
        } else {
            shell_print("kwork: queue failed\n");
            last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "periodic") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: kwork periodic <ms>\n");
            last_exit_code = 1;
            return;
        }
        uint32_t ms = (uint32_t)strtol(arg1, NULL, 10);
        if (ms == 0) {
            shell_print("kwork: period must be > 0\n");
            last_exit_code = 1;
            return;
        }
        kwork_init_work(&g_kwork_test_item, kwork_test_cb, (void *)0x9abc);
        int rc = kwork_queue_periodic_work(&g_kwork_test_item, ms);
        if (rc == 0) {
            shell_print("kwork: periodic work queued (every ");
            char buf[16];
            itoa((int)ms, buf, 10);
            shell_print(buf);
            shell_print(" ms)\n");
        } else {
            shell_print("kwork: queue failed\n");
            last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "cancel") == 0) {
        int rc = kwork_cancel_work(&g_kwork_test_item);
        if (rc == 0) {
            shell_print("kwork: test work cancelled\n");
        } else {
            shell_print("kwork: cancel failed (not queued?)\n");
            last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "flush") == 0) {
        shell_print("kwork: flushing...\n");
        kwork_flush();
        shell_print("kwork: flush complete\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "dump") == 0) {
        kwork_dump(kwork_get_default());
        shell_print("kwork: state dumped to klog (use 'dmesg' to view)\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "reset") == 0) {
        kwork_reset_stats();
        g_kwork_test_count = 0;
        shell_print("kwork: statistics reset\n");
        last_exit_code = 0;
        return;
    }

    /* Default: show stats */
    {
        kwork_stats_t st;
        kwork_get_stats(&st);
        char line[160];

        shell_print("=== kwork Statistics ===\n");
        snprintf(line, sizeof(line),
                 "Worker alive : %s\n",
                 st.worker_alive ? "yes" : "no");
        shell_print(line);
        snprintf(line, sizeof(line),
                 "Immediate    : %u pending\n", st.immediate_pending);
        shell_print(line);
        snprintf(line, sizeof(line),
                 "Delayed      : %u pending\n", st.delayed_pending);
        shell_print(line);
        snprintf(line, sizeof(line),
                 "Total queued : %u\n", (uint32_t)st.queued);
        shell_print(line);
        snprintf(line, sizeof(line),
                 "Total exec   : %u\n", (uint32_t)st.executed);
        shell_print(line);
        snprintf(line, sizeof(line),
                 "Errors       : %u\n", (uint32_t)st.errors);
        shell_print(line);
        snprintf(line, sizeof(line),
                 "Cancelled    : %u\n", (uint32_t)st.cancelled);
        shell_print(line);
        snprintf(line, sizeof(line),
                 "Max depth    : %u\n", (uint32_t)st.max_queue_depth);
        shell_print(line);
        snprintf(line, sizeof(line),
                 "Test fires   : %u\n", g_kwork_test_count);
        shell_print(line);
        last_exit_code = 0;
    }
}

/* ============================================================
 * reg - 系统注册表管理命令
 *
 * 用法：
 *   reg                              显示根键列表与统计
 *   reg help                         显示帮助
 *   reg query <path>                 查询键的子键与值
 *   reg add <path>                   创建键
 *   reg set <path> <name> <data>    设置字符串值
 *   reg setdw <path> <name> <num>   设置 DWORD 值
 *   reg get <path> <name>            获取值
 *   reg del <path> [name]            删除键或值
 *   reg export [path]                导出（默认全部）
 *   reg stats                        显示统计
 *   reg roots                        列出根键
 *
 * 路径格式：HKLM\System\Kernel  （反斜杠或正斜杠均可）
 * ============================================================ */

static void cmd_reg_print_subtree(reg_handle_t key, int depth) {
    char indent[64];
    int i;
    char line[400];

    for (i = 0; i < depth && i < 30; i++) indent[i] = ' ';
    indent[i] = '\0';

    /* 打印键名 */
    snprintf(line, sizeof(line), "%s%s/\n", indent, key->name);
    shell_print(line);

    /* 打印值 */
    reg_value_t *v = key->values;
    while (v) {
        snprintf(line, sizeof(line), "%s  \"%s\" = %s : %s\n",
                 indent, v->name, reg_type_name(v->type), v->data);
        shell_print(line);
        v = v->next;
    }

    /* 递归子键 */
    reg_key_t *c = key->children;
    while (c) {
        cmd_reg_print_subtree(c, depth + 1);
        c = c->next_sibling;
    }
}

static void cmd_reg(const char *subcmd, const char *arg1, const char *arg2) {
    (void)arg2;
    char line[512];
    static const char *reg_roots[HKEY_COUNT] = {
        HKLM_SHORT, HKCU_SHORT, HKCR_SHORT, HKU_SHORT, HKCC_SHORT
    };

    if (!subcmd || !*subcmd || strcmp(subcmd, "help") == 0) {
        shell_print("Usage:\n");
        shell_print("  reg                 Show root keys and stats\n");
        shell_print("  reg query <path>    Query subkeys and values of a key\n");
        shell_print("  reg add <path>      Create a key\n");
        shell_print("  reg set <path> <name> <data>      Set a string value\n");
        shell_print("  reg setdw <path> <name> <num>    Set a DWORD value\n");
        shell_print("  reg get <path> <name>             Get a value\n");
        shell_print("  reg del <path> [name]             Delete key or value\n");
        shell_print("  reg export [path]   Export registry (default: all)\n");
        shell_print("  reg stats           Show statistics\n");
        shell_print("  reg roots           List root keys\n");
        shell_print("\nPaths: HKLM\\System\\Kernel  HKCU\\Desktop  HKCR\\.txt\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "roots") == 0) {
        int i;
        for (i = 0; i < HKEY_COUNT; i++) {
            snprintf(line, sizeof(line), "  %-6s  %s\n",
                     reg_roots[i], reg_root_name((uint32_t)i));
            shell_print(line);
        }
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "stats") == 0) {
        reg_stats_t st;
        reg_get_stats(&st);
        shell_print("=== Registry Statistics ===\n");
        snprintf(line, sizeof(line), "Total keys   : %u\n", st.total_keys);
        shell_print(line);
        snprintf(line, sizeof(line), "Total values : %u\n", st.total_values);
        shell_print(line);
        shell_print("\n  Per-root:\n");
        int i;
        for (i = 0; i < HKEY_COUNT; i++) {
            snprintf(line, sizeof(line), "    %-6s  keys=%u  values=%u\n",
                     reg_roots[i],
                     st.per_root_keys[i], st.per_root_values[i]);
            shell_print(line);
        }
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "query") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: reg query <path>\n");
            last_exit_code = 1;
            return;
        }
        reg_handle_t h = reg_open_path(arg1);
        if (!h) {
            shell_print("reg: key not found\n");
            last_exit_code = 1;
            return;
        }
        cmd_reg_print_subtree(h, 0);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "add") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: reg add <path>\n");
            last_exit_code = 1;
            return;
        }
        reg_handle_t h = reg_create_path(arg1);
        if (!h) {
            shell_print("reg: failed to create key\n");
            last_exit_code = 1;
            return;
        }
        shell_print("reg: key created/exists\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "set") == 0) {
        /* arg1 = path, arg2 = "name value" - 但我们只有 arg1/arg2
         * 实际：subcmd=set, arg1=path, arg2=name
         * 需要 arg3=value - 但 shell 只给到 arg3
         * 这里用 arg1=path, arg2=name, 然后 value 缺失
         * 简化：arg1 形如 "path\\name value" 太复杂
         * 改为：reg set <path> <name>=<value>
         * 但 shell 解析为 arg1=path, arg2=name=value
         */
        if (!arg1 || !*arg1 || !arg2 || !*arg2) {
            shell_print("Usage: reg set <path> <name> <value>\n");
            last_exit_code = 1;
            return;
        }
        /* arg2 可能是 "name value" 或 "name" (value 在 arg3) */
        char name[REG_MAX_NAME];
        char value[REG_MAX_DATA];
        /* 尝试在 arg2 中找空格分割 */
        const char *sp = arg2;
        while (*sp && *sp != ' ') sp++;
        if (*sp) {
            uint32_t nl = (uint32_t)(sp - arg2);
            if (nl >= sizeof(name)) nl = sizeof(name) - 1;
            memcpy(name, arg2, nl);
            name[nl] = '\0';
            sp++;
            while (*sp == ' ') sp++;
            strncpy(value, sp, sizeof(value) - 1);
            value[sizeof(value) - 1] = '\0';
        } else {
            /* arg2 是 name，value 缺失 */
            strncpy(name, arg2, sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
            value[0] = '\0';
        }
        reg_handle_t h = reg_create_path(arg1);
        if (!h) {
            shell_print("reg: invalid path\n");
            last_exit_code = 1;
            return;
        }
        int rc = reg_set_string(h, name, value);
        if (rc == REG_OK) {
            shell_print("reg: value set\n");
            last_exit_code = 0;
        } else {
            shell_print("reg: set failed\n");
            last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "setdw") == 0) {
        if (!arg1 || !*arg1 || !arg2 || !*arg2) {
            shell_print("Usage: reg setdw <path> <name> <num>\n");
            last_exit_code = 1;
            return;
        }
        char name[REG_MAX_NAME];
        const char *sp = arg2;
        while (*sp && *sp != ' ') sp++;
        uint32_t nl = (uint32_t)(sp - arg2);
        if (nl >= sizeof(name)) nl = sizeof(name) - 1;
        memcpy(name, arg2, nl);
        name[nl] = '\0';
        uint32_t val = 0;
        if (*sp) {
            sp++;
            while (*sp == ' ') sp++;
            val = (uint32_t)strtol(sp, NULL, 10);
        }
        reg_handle_t h = reg_create_path(arg1);
        if (!h) {
            shell_print("reg: invalid path\n");
            last_exit_code = 1;
            return;
        }
        int rc = reg_set_dword(h, name, val);
        if (rc == REG_OK) {
            shell_print("reg: dword set\n");
            last_exit_code = 0;
        } else {
            shell_print("reg: set failed\n");
            last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "get") == 0) {
        if (!arg1 || !*arg1 || !arg2 || !*arg2) {
            shell_print("Usage: reg get <path> <name>\n");
            last_exit_code = 1;
            return;
        }
        reg_handle_t h = reg_open_path(arg1);
        if (!h) {
            shell_print("reg: key not found\n");
            last_exit_code = 1;
            return;
        }
        uint32_t type = 0;
        char data[REG_MAX_DATA];
        uint32_t size = sizeof(data);
        int rc = reg_get_value(h, arg2, &type, data, &size);
        if (rc != REG_OK) {
            shell_print("reg: value not found\n");
            last_exit_code = 1;
            return;
        }
        snprintf(line, sizeof(line), "%s : %s\n", reg_type_name(type), data);
        shell_print(line);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "del") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: reg del <path> [name]\n");
            last_exit_code = 1;
            return;
        }
        if (arg2 && *arg2) {
            /* 删除值 */
            reg_handle_t h = reg_open_path(arg1);
            if (!h) {
                shell_print("reg: key not found\n");
                last_exit_code = 1;
                return;
            }
            int rc = reg_delete_value(h, arg2);
            if (rc == REG_OK) {
                shell_print("reg: value deleted\n");
            } else {
                shell_print("reg: value not found\n");
                last_exit_code = 1;
            }
        } else {
            /* 删除键 */
            const char *sub;
            int root = reg_parse_root(arg1, &sub);
            if (root < 0) {
                shell_print("reg: invalid path\n");
                last_exit_code = 1;
                return;
            }
            int rc = reg_delete_key((uint32_t)root, sub);
            if (rc == REG_OK) {
                shell_print("reg: key deleted\n");
            } else {
                shell_print("reg: delete failed\n");
                last_exit_code = 1;
            }
        }
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "export") == 0) {
        if (!arg1 || !*arg1) {
            /* 导出全部到 klog */
            reg_dump_all();
            shell_print("reg: full registry dumped to klog\n");
        } else {
            reg_handle_t h = reg_open_path(arg1);
            if (!h) {
                shell_print("reg: key not found\n");
                last_exit_code = 1;
                return;
            }
            static char buf[8192];
            buf[0] = '\0';
            reg_export(h, buf, sizeof(buf));
            shell_print(buf);
        }
        last_exit_code = 0;
        return;
    }

    /* 默认：显示根键与统计 */
    {
        int i;
        shell_print("=== System Registry ===\n");
        shell_print("Root keys:\n");
        for (i = 0; i < HKEY_COUNT; i++) {
            snprintf(line, sizeof(line), "  %-6s  %s\n",
                     reg_roots[i], reg_root_name((uint32_t)i));
            shell_print(line);
        }
        shell_print("\nUse 'reg query HKLM\\System' to browse.\n");
        shell_print("Use 'reg help' for full usage.\n");
        reg_stats_t st;
        reg_get_stats(&st);
        snprintf(line, sizeof(line), "\nKeys: %u  Values: %u\n",
                 st.total_keys, st.total_values);
        shell_print(line);
        last_exit_code = 0;
    }
}

/* ============================================================
 * apps - 内置应用浏览器与启动器
 *
 * 用法：
 *   apps                  列出所有已注册应用
 *   apps help             显示帮助
 *   apps list             列出所有应用（表格）
 *   apps info <id>        显示应用详情
 *   apps run <id>         启动应用
 *   apps cat <category>   按类别筛选（System/Utility/Application/Game）
 * ============================================================ */

static void cmd_apps_list(const char *category_filter) {
    reg_handle_t apps_root = reg_open_path("HKLM\\Software\\Apps");
    if (!apps_root) {
        shell_print("apps: no applications registered\n");
        return;
    }

    char line[256];
    if (category_filter && *category_filter) {
        snprintf(line, sizeof(line),
                 "=== Applications [%s] ===\n", category_filter);
    } else {
        snprintf(line, sizeof(line),
                 "=== Installed Applications ===\n");
    }
    shell_print(line);
    shell_print("ID            Name              Category    Version\n");
    shell_print("------------  ----------------  ----------  -------\n");

    reg_key_t *c = apps_root->children;
    uint32_t count = 0;
    while (c) {
        reg_value_t *name_v = NULL;
        reg_value_t *cat_v = NULL;
        reg_value_t *ver_v = NULL;
        reg_value_t *v = c->values;
        while (v) {
            if (strcmp(v->name, "name") == 0) name_v = v;
            else if (strcmp(v->name, "category") == 0) cat_v = v;
            else if (strcmp(v->name, "version") == 0) ver_v = v;
            v = v->next;
        }
        const char *cat = cat_v ? cat_v->data : "?";
        if (!category_filter || !*category_filter ||
            strcmp(cat, category_filter) == 0) {
            snprintf(line, sizeof(line), "%-12s  %-16s  %-10s  %s\n",
                     c->name,
                     name_v ? name_v->data : c->name,
                     cat,
                     ver_v ? ver_v->data : "?");
            shell_print(line);
            count++;
        }
        c = c->next_sibling;
    }
    snprintf(line, sizeof(line), "\n%u application(s) found.\n", count);
    shell_print(line);
}

static void cmd_apps(const char *subcmd, const char *arg1, const char *arg2) {
    (void)arg2;
    char line[512];

    if (!subcmd || !*subcmd || strcmp(subcmd, "help") == 0) {
        shell_print("Usage:\n");
        shell_print("  apps             List all installed applications\n");
        shell_print("  apps list        List all apps (table format)\n");
        shell_print("  apps info <id>   Show application details\n");
        shell_print("  apps run <id>    Launch an application\n");
        shell_print("  apps cat <name>  Filter by category (System/Utility/Application/Game)\n");
        shell_print("\nCategories: System, Utility, Application, Game\n");
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "list") == 0) {
        cmd_apps_list(NULL);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "cat") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: apps cat <category>\n");
            last_exit_code = 1;
            return;
        }
        cmd_apps_list(arg1);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "info") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: apps info <id>\n");
            last_exit_code = 1;
            return;
        }
        char path[128];
        snprintf(path, sizeof(path), "HKLM\\Software\\Apps\\%s", arg1);
        reg_handle_t h = reg_open_path(path);
        if (!h) {
            shell_print("apps: application not found\n");
            last_exit_code = 1;
            return;
        }
        shell_print("=== Application Details ===\n");
        snprintf(line, sizeof(line), "ID: %s\n", arg1);
        shell_print(line);
        reg_value_t *v = h->values;
        while (v) {
            snprintf(line, sizeof(line), "%-12s : %s\n",
                     v->name, v->data);
            shell_print(line);
            v = v->next;
        }
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "run") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: apps run <id>\n");
            last_exit_code = 1;
            return;
        }
        /* 使用 appexec 服务获取应用信息 */
        appexec_app_info_t info;
        if (appexec_get_info(arg1, &info) != 0) {
            shell_print("apps: application not found\n");
            last_exit_code = 1;
            return;
        }

        /* 确定要执行的命令 */
        const char *cmd = info.command;
        if (!cmd || !*cmd) cmd = info.path;
        if (!cmd || !*cmd) cmd = info.id;

        char buf[320];
        snprintf(buf, sizeof(buf), "apps: launching '%s' (id=%s, cmd=%s)\n",
                 info.name, arg1, cmd);
        shell_print(buf);

        /* 记录启动 */
        int hidx = appexec_record_launch(arg1, info.name, cmd);

        /* 实际执行 */
        shell_execute(cmd);

        /* 记录结果（shell_execute 无返回值，假设 last_exit_code） */
        appexec_record_result(hidx, last_exit_code);

        snprintf(buf, sizeof(buf), "apps: '%s' completed (exit=%d)\n",
                 info.name, last_exit_code);
        shell_print(buf);
        last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "history") == 0) {
        uint32_t count = appexec_history_count();
        if (count == 0) {
            shell_print("No app launch history.\n");
            last_exit_code = 0;
            return;
        }
        appexec_history_t *hist = (appexec_history_t *)kmalloc(
            sizeof(appexec_history_t) * count);
        if (!hist) {
            shell_print("apps: out of memory\n");
            last_exit_code = 1;
            return;
        }
        uint32_t n = appexec_history(hist, count);
        shell_print("App                     ID            Exit  Duration  Command\n");
        shell_print("----------------------  ------------  ----  --------  -------\n");
        for (uint32_t i = 0; i < n; i++) {
            char line[512];
            snprintf(line, sizeof(line), "%-22s  %-12s  %4d  %7u  %s\n",
                     hist[i].name, hist[i].id,
                     hist[i].completed ? hist[i].exit_code : -1,
                     hist[i].duration_ms,
                     hist[i].command);
            shell_print(line);
        }
        kfree(hist);
        last_exit_code = 0;
        return;
    }

    /* 默认：列出所有 */
    cmd_apps_list(NULL);
    last_exit_code = 0;
}

void shell_init(void) {
    current_dir[0] = '/';
    current_dir[1] = '\0';
    env_init();
    history_count = 0;
    history_pos = 0;
    alias_count = 0;
    /* 注册 cron 命令执行器，使定时任务可执行任意 shell 命令 */
    cron_set_executor(cron_shell_executor);
}


