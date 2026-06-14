#include "user_syscall.h"
#include "string.h"

static char buf[4096];

int main(int argc, char *argv[])
{
    if (argc < 3) {
        sys_write(2, "Usage: mv <src> <dst>\n", 22);
        sys_exit(1);
    }

    int src = sys_open(argv[1], O_RDONLY);
    if (src < 0) {
        sys_write(2, "mv: cannot open source\n", 23);
        sys_exit(1);
    }

    int dst = sys_open(argv[2], O_CREAT | O_TRUNC | O_WRONLY);
    if (dst < 0) {
        sys_write(2, "mv: cannot open dest\n", 21);
        sys_close(src);
        sys_exit(1);
    }

    while (1) {
        int n = sys_read(src, buf, 4096);
        if (n <= 0) break;
        sys_write(dst, buf, n);
    }

    sys_close(src);
    sys_close(dst);

    sys_open(argv[1], O_TRUNC);

    sys_exit(0);
    return 0;
}
