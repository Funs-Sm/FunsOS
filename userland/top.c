#include "user_syscall.h"
#include "string.h"

static char buf[4096];

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

int main(void) {
    while (1) {
        sys_write(1, "\033[2J\033[H", 7);

        sys_write(1, "=== Process Monitor ===\n", 24);

        int stat_fd = sys_open("/proc/stat", 0);
        if (stat_fd >= 0) {
            int n = sys_read(stat_fd, buf, 4095);
            if (n > 0) {
                buf[n] = '\0';
                char *cpu_line = strstr(buf, "cpu ");
                if (cpu_line) {
                    sys_write(1, "CPU: ", 5);
                    while (*cpu_line && *cpu_line != '\n') {
                        sys_write(1, cpu_line, 1);
                        cpu_line++;
                    }
                    sys_write(1, "\n", 1);
                }
                char *mem_line = strstr(buf, "MemTotal:");
                if (mem_line) {
                    sys_write(1, "Memory: ", 8);
                    while (*mem_line && *mem_line != '\n') {
                        sys_write(1, mem_line, 1);
                        mem_line++;
                    }
                    sys_write(1, "\n", 1);
                }
            }
            sys_close(stat_fd);
        }

        sys_write(1, "\nPID    NAME            STATE   CPU%  MEM%\n", 43);
        sys_write(1, "--------------------------------------------\n", 45);

        for (int pid = 1; pid < 256; pid++) {
            char path[64];
            int plen = 0;
            const char *p1 = "/proc/";
            while (*p1) path[plen++] = *p1++;
            if (pid >= 100) path[plen++] = '0' + pid / 100;
            if (pid >= 10) path[plen++] = '0' + (pid / 10) % 10;
            path[plen++] = '0' + pid % 10;
            const char *p2 = "/status";
            while (*p2) path[plen++] = *p2++;
            path[plen] = '\0';

            int fd = sys_open(path, 0);
            if (fd < 0) continue;

            int n = sys_read(fd, buf, 4095);
            sys_close(fd);

            if (n <= 0) continue;
            buf[n] = '\0';

            print_num(pid);
            sys_write(1, "  ", 2);

            char *name = strstr(buf, "Name:");
            if (name) {
                name += 5;
                while (*name == ' ') name++;
                int nlen = 0;
                while (*name && *name != '\n' && nlen < 16) {
                    sys_write(1, name, 1);
                    name++;
                    nlen++;
                }
                while (nlen < 16) {
                    sys_write(1, " ", 1);
                    nlen++;
                }
            } else {
                sys_write(1, "                ", 16);
            }

            char *state = strstr(buf, "State:");
            if (state) {
                state += 6;
                while (*state == ' ') state++;
                int slen = 0;
                while (*state && *state != '\n' && slen < 6) {
                    sys_write(1, state, 1);
                    state++;
                    slen++;
                }
                while (slen < 6) {
                    sys_write(1, " ", 1);
                    slen++;
                }
            } else {
                sys_write(1, "      ", 6);
            }

            char *cpu = strstr(buf, "CpuUsr:");
            if (cpu) {
                cpu += 7;
                while (*cpu == ' ') cpu++;
                while (*cpu && *cpu != '\n') {
                    sys_write(1, cpu, 1);
                    cpu++;
                }
                sys_write(1, "%", 1);
            } else {
                sys_write(1, "0%", 2);
            }

            sys_write(1, "  ", 2);

            char *mem = strstr(buf, "MemUse:");
            if (mem) {
                mem += 7;
                while (*mem == ' ') mem++;
                while (*mem && *mem != '\n') {
                    sys_write(1, mem, 1);
                    mem++;
                }
                sys_write(1, "%", 1);
            } else {
                sys_write(1, "0%", 2);
            }

            sys_write(1, "\n", 1);
        }

        sys_sleep(2);
    }

    return 0;
}
