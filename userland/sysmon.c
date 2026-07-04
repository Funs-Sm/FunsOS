#include "user_syscall.h"
#include "string.h"

static char buf[8192];

static void print_str(const char *s) {
    sys_write(1, s, strlen(s));
}

static void print_num(uint32_t val) {
    char tmp[16];
    int len = 0;
    if (val == 0) {
        sys_write(1, "0", 1);
        return;
    }
    while (val > 0) {
        tmp[len++] = '0' + (val % 10);
        val /= 10;
    }
    for (int i = len - 1; i >= 0; i--) {
        sys_write(1, &tmp[i], 1);
    }
}

static void print_hex(uint32_t val) {
    char tmp[16];
    int len = 0;
    if (val == 0) {
        sys_write(1, "0x0", 3);
        return;
    }
    while (val > 0) {
        int d = val % 16;
        if (d < 10) tmp[len++] = '0' + d;
        else tmp[len++] = 'a' + d - 10;
        val /= 16;
    }
    sys_write(1, "0x", 2);
    for (int i = len - 1; i >= 0; i--) {
        sys_write(1, &tmp[i], 1);
    }
}

static char *read_line(const char *data, char *line, int maxlen) {
    int i = 0;
    while (*data && *data != '\n' && i < maxlen - 1) {
        line[i++] = *data++;
    }
    line[i] = '\0';
    if (*data == '\n') data++;
    return (char *)data;
}

static void print_header(void) {
    print_str("\033[2J\033[H");
    print_str("========== System Monitor ==========\n");
}

static void read_meminfo(void) {
    int fd = sys_open("/proc/meminfo", 0);
    if (fd < 0) return;
    int n = sys_read(fd, buf, sizeof(buf) - 1);
    sys_close(fd);
    if (n <= 0) return;
    buf[n] = '\0';

    print_str("--- Memory ---\n");
    char line[256];
    char *p = buf;
    while (*p && *p != '\0') {
        p = read_line(p, line, sizeof(line));
        if (strncmp(line, "MemTotal:", 9) == 0 ||
            strncmp(line, "MemFree:", 8) == 0 ||
            strncmp(line, "MemAvailable:", 13) == 0 ||
            strncmp(line, "Buffers:", 8) == 0 ||
            strncmp(line, "Cached:", 7) == 0 ||
            strncmp(line, "SwapTotal:", 10) == 0 ||
            strncmp(line, "SwapFree:", 9) == 0) {
            print_str("  ");
            print_str(line);
            print_str("\n");
        }
    }
}

static void read_cpuinfo(void) {
    int fd = sys_open("/proc/cpuinfo", 0);
    if (fd < 0) return;
    int n = sys_read(fd, buf, sizeof(buf) - 1);
    sys_close(fd);
    if (n <= 0) return;
    buf[n] = '\0';

    print_str("--- CPU ---\n");
    char line[256];
    char *p = buf;
    while (*p) {
        p = read_line(p, line, sizeof(line));
        if (strncmp(line, "model name", 10) == 0 ||
            strncmp(line, "cpu MHz", 7) == 0 ||
            strncmp(line, "cache size", 10) == 0 ||
            strncmp(line, "processor", 9) == 0) {
            print_str("  ");
            print_str(line);
            print_str("\n");
        }
    }
}

static void read_uptime(void) {
    int fd = sys_open("/proc/uptime", 0);
    if (fd < 0) return;
    int n = sys_read(fd, buf, sizeof(buf) - 1);
    sys_close(fd);
    if (n <= 0) return;
    buf[n] = '\0';

    print_str("--- Uptime ---\n  ");
    print_str(buf);
}

static void read_loadavg(void) {
    int fd = sys_open("/proc/loadavg", 0);
    if (fd < 0) return;
    int n = sys_read(fd, buf, sizeof(buf) - 1);
    sys_close(fd);
    if (n <= 0) return;
    buf[n] = '\0';

    print_str("--- Load Average ---\n  ");
    print_str(buf);
}

static void print_processes(void) {
    print_str("--- Processes ---\n");
    print_str("  PID   NAME          STATE\n");
    print_str("  --------------------------\n");

    for (int pid = 1; pid < 256; pid++) {
        char path[64];
        char *pp = path;
        const char *s = "/proc/";
        while (*s) *pp++ = *s++;
        if (pid >= 100) *pp++ = '0' + pid / 100;
        if (pid >= 10) *pp++ = '0' + (pid / 10) % 10;
        *pp++ = '0' + pid % 10;
        s = "/status";
        while (*s) *pp++ = *s++;
        *pp = '\0';

        int fd = sys_open(path, 0);
        if (fd < 0) continue;
        int n = sys_read(fd, buf, sizeof(buf) - 1);
        sys_close(fd);
        if (n <= 0) continue;
        buf[n] = '\0';

        char name[32] = {0};
        char state[16] = {0};
        char line[256];
        char *p = buf;
        while (*p) {
            p = read_line(p, line, sizeof(line));
            if (strncmp(line, "Name:", 5) == 0) {
                const char *v = line + 5;
                while (*v == ' ' || *v == '\t') v++;
                strncpy(name, v, sizeof(name) - 1);
            } else if (strncmp(line, "State:", 6) == 0) {
                const char *v = line + 6;
                while (*v == ' ' || *v == '\t') v++;
                strncpy(state, v, sizeof(state) - 1);
            }
        }

        print_str("  ");
        print_num(pid);
        int sp = 6;
        if (pid >= 10) sp--;
        if (pid >= 100) sp--;
        for (int i = 0; i < sp; i++) print_str(" ");
        print_str(name);
        int nl = strlen(name);
        for (int i = nl; i < 14; i++) print_str(" ");
        print_str(state);
        print_str("\n");
    }
}

int main(int argc, char *argv[]) {
    int refresh_ms = 2000;
    int iterations = 0;
    int one_shot = 0;

    if (argc >= 2) {
        if (strcmp(argv[1], "-n") == 0) {
            one_shot = 1;
        } else if (strcmp(argv[1], "-d") == 0 && argc >= 3) {
            int d = 0;
            const char *s = argv[2];
            while (*s >= '0' && *s <= '9') {
                d = d * 10 + (*s - '0');
                s++;
            }
            if (d > 0) refresh_ms = d * 1000;
        } else if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            print_str("Usage: sysmon [-n] [-d seconds]\n");
            print_str("  -n       one shot, no refresh\n");
            print_str("  -d N     refresh every N seconds\n");
            return 0;
        }
    }

    do {
        print_header();
        read_uptime();
        read_loadavg();
        read_meminfo();
        read_cpuinfo();
        print_processes();
        print_str("\nPress Q to quit\n");

        if (one_shot) break;
        iterations++;

        sys_sleep(refresh_ms);

        if (iterations > 1000) break;
    } while (1);

    return 0;
}
