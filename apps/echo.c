#include "user_syscall.h"
#include "string.h"

int main(int argc, char *argv[])
{
    int no_newline = 0;
    int start = 1;

    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'n') {
        no_newline = 1;
        start = 2;
    }

    for (int i = start; i < argc; i++) {
        sys_write(1, argv[i], strlen(argv[i]));
        if (i < argc - 1) {
            sys_write(1, " ", 1);
        }
    }

    if (!no_newline) {
        sys_write(1, "\n", 1);
    }

    sys_exit(0);
    return 0;
}
