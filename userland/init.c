#include "user_syscall.h"
#include "string.h"

int main(void) {
    sys_write(1, "init: starting...\n", 18);

    sys_mount("/proc", "proc", 0);
    sys_mount("/sys", "sysfs", 0);
    sys_mount("/dev", "devfs", 0);

    int desktop = 0;

    int fd = sys_open("/etc/inittab", 0);
    if (fd >= 0) {
        char buf[256];
        int n = sys_read(fd, buf, 255);
        if (n > 0) {
            buf[n] = '\0';
            if (strstr(buf, "desktop") || strstr(buf, "gui")) {
                desktop = 1;
            }
        }
        sys_close(fd);
    }

    while (1) {
        int pid = sys_fork();
        if (pid == 0) {
            if (desktop) {
                char *argv[] = { "/bin/desktop", 0 };
                sys_execve("/bin/desktop", argv, 0);
            } else {
                char *argv[] = { "/bin/shell", 0 };
                sys_execve("/bin/shell", argv, 0);
            }
            sys_exit(1);
        }

        if (desktop) {
            int pid2 = sys_fork();
            if (pid2 == 0) {
                char *argv2[] = { "/bin/terminal", 0 };
                sys_execve("/bin/terminal", argv2, 0);
                sys_exit(1);
            }
        }

        int status;
        int exited = sys_waitpid(-1, &status);

        if (exited == pid) {
            sys_write(1, "init: shell exited, restarting...\n", 33);
        }
    }

    return 0;
}
