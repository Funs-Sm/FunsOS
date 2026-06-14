#include "user_syscall.h"
#include "string.h"

int main(int argc, char *argv[])
{
    if (argc < 2) {
        sys_write(2, "Usage: touch <file>\n", 20);
        sys_exit(1);
    }

    for (int i = 1; i < argc; i++) {
        int fd = sys_open(argv[i], O_CREAT | O_WRONLY);
        if (fd >= 0) {
            sys_close(fd);
        } else {
            sys_write(2, "touch: failed\n", 14);
        }
    }

    sys_exit(0);
    return 0;
}
