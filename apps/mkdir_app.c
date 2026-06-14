#include "stdio.h"
#include "string.h"
#include "vfs.h"
#include "path.h"
#include "dentry.h"

int app_mkdir_main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: mkdir <directory>\n");
        return 1;
    }

    if (vfs_mkdir(argv[1], FILE_MODE_DIR | FILE_MODE_READ | FILE_MODE_WRITE) == 0) {
        printf("Created directory: %s\n", argv[1]);
        return 0;
    } else {
        printf("mkdir: cannot create '%s'\n", argv[1]);
        return 1;
    }
}
