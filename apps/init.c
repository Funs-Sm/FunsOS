#include "stdio.h"
#include "stdlib.h"
#include "string.h"

int app_init_main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    printf("Funs Core v0.2 - Init starting...\n");
    printf("Mounting root filesystem...\n");
    printf("Starting shell...\n");
    /* In a real init, we would fork and exec the shell.
       In our kernel-linked model, the shell is already running. */
    while (1) {
        /* Run shell - in kernel mode, shell_run() is called from main */
        break;
    }
    return 0;
}
