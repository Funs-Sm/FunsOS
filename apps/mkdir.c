#include "user_syscall.h"
#include "string.h"

int main(int argc, char *argv[])
{
    if (argc < 2) {
        sys_write(2, "Usage: mkdir <path>\n", 20);
        sys_exit(1);
    }

    int fd = sys_open(argv[1], O_DIRECTORY | O_CREAT);
    if (fd < 0) {
        sys_write(2, "mkdir: failed\n", 14);
        sys_exit(1);
    }

    sys_close(fd);
    sys_exit(0);
    return 0;
}
