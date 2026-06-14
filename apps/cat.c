#include "user_syscall.h"
#include "string.h"

static char buf[512];

int main(int argc, char *argv[])
{
    if (argc < 2) {
        while (1) {
            int n = sys_read(0, buf, 512);
            if (n <= 0) break;
            sys_write(1, buf, n);
        }
        sys_exit(0);
    }

    for (int i = 1; i < argc; i++) {
        int fd = sys_open(argv[i], O_RDONLY);
        if (fd < 0) {
            sys_write(2, "cat: ", 5);
            sys_write(2, argv[i], strlen(argv[i]));
            sys_write(2, ": cannot open\n", 14);
            continue;
        }
        while (1) {
            int n = sys_read(fd, buf, 512);
            if (n <= 0) break;
            sys_write(1, buf, n);
        }
        sys_close(fd);
    }

    sys_exit(0);
    return 0;
}
