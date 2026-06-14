#include "stdio.h"
#include "string.h"
#include "vfs.h"
#include "path.h"

int app_mv_main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: mv <src> <dst>\n");
        return 1;
    }

    const char *src_path = argv[1];
    const char *dst_path = argv[2];

    /* Read source file */
    file_t *sf = 0;
    if (vfs_open(src_path, FILE_MODE_READ, &sf) != 0 || !sf) {
        printf("mv: cannot open '%s'\n", src_path);
        return 1;
    }

    /* Create destination file */
    file_t *df = 0;
    if (vfs_open(dst_path, FILE_MODE_WRITE, &df) != 0 || !df) {
        printf("mv: cannot create '%s'\n", dst_path);
        vfs_close(sf);
        return 1;
    }

    /* Copy content */
    char buf[256];
    int32_t n;
    while ((n = vfs_read(sf, buf, 255)) > 0) {
        vfs_write(df, buf, (uint32_t)n);
    }

    vfs_close(sf);
    vfs_close(df);

    /* Delete source */
    vfs_unlink(src_path);
    printf("Moved %s to %s\n", src_path, dst_path);
    return 0;
}
