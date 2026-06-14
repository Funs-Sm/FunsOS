#include "stdio.h"
#include "string.h"
#include "vfs.h"
#include "path.h"

int app_cat_main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: cat <file>\n");
        return 1;
    }

    const char *path = argv[1];
    file_t *f = 0;
    if (vfs_open(path, FILE_MODE_READ, &f) != 0 || !f) {
        printf("cat: '%s': No such file\n", path);
        return 1;
    }

    char buf[256];
    int32_t n;
    while ((n = vfs_read(f, buf, 255)) > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }
    vfs_close(f);
    return 0;
}
