#include "stdio.h"
#include "string.h"
#include "vfs.h"
#include "path.h"

int app_wc_main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: wc <file>\n");
        return 1;
    }

    const char *path = argv[1];
    file_t *f = 0;
    if (vfs_open(path, FILE_MODE_READ, &f) != 0 || !f) {
        printf("wc: '%s': No such file\n", path);
        return 1;
    }

    uint32_t lines = 0, words = 0, chars = 0;
    int in_word = 0;
    char buf[256];
    int32_t n;
    while ((n = vfs_read(f, buf, 255)) > 0) {
        chars += (uint32_t)n;
        for (int32_t i = 0; i < n; i++) {
            if (buf[i] == '\n') lines++;
            if (buf[i] == ' ' || buf[i] == '\n' || buf[i] == '\t') {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                words++;
            }
        }
    }
    vfs_close(f);

    printf("  %u  %u  %u %s\n", lines, words, chars, path);
    return 0;
}
