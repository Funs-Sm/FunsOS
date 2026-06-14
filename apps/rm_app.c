#include "stdio.h"
#include "string.h"
#include "vfs.h"
#include "path.h"

int app_rm_main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: rm <file>\n");
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        if (vfs_unlink(argv[i]) == 0) {
            printf("Removed: %s\n", argv[i]);
        } else {
            printf("rm: cannot remove '%s'\n", argv[i]);
        }
    }
    return 0;
}
