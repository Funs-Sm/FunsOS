#include "user_syscall.h"
#include "string.h"

int main(int argc, char *argv[])
{
    if (argc < 2) {
        sys_write(2, "Usage: rm <file>\n", 17);
        sys_exit(1);
    }

    for (int i = 1; i < argc; i++) {
        int result = sys_open(argv[i], O_TRUNC);
        if (result < 0) {
            sys_write(2, "rm: failed\n", 11);
        }
    }

    sys_exit(0);
    return 0;
}
