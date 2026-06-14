#include "user_syscall.h"
#include "string.h"

int main(int argc, char *argv[])
{
    char *path = ".";
    if (argc > 1) {
        path = argv[1];
    }

    int fd = sys_open(path, O_DIRECTORY);
    if (fd < 0) {
        sys_write(2, "ls: cannot open directory\n", 26);
        sys_exit(1);
    }

    char buf[512];
    while (1) {
        int n = sys_readdir(fd, buf, 512);
        if (n <= 0) break;
        sys_write(1, buf, strlen(buf));
        sys_write(1, "\n", 1);
    }

    sys_close(fd);
    sys_exit(0);
    return 0;
}
