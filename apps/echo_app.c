#include "stdio.h"
#include "string.h"

int app_echo_main(int argc, char *argv[]) {
    int no_newline = 0;
    int start = 1;

    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'n') {
        no_newline = 1;
        start = 2;
    }

    for (int i = start; i < argc; i++) {
        printf("%s", argv[i]);
        if (i < argc - 1) {
            printf(" ");
        }
    }

    if (!no_newline) {
        printf("\n");
    }

    return 0;
}
