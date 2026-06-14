#include "stdio.h"
#include "string.h"
#include "vfs.h"
#include "path.h"

int app_grep_main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: grep <pattern> <file>\n");
        return 1;
    }

    const char *pattern = argv[1];
    const char *path = argv[2];

    file_t *f = 0;
    if (vfs_open(path, FILE_MODE_READ, &f) != 0 || !f) {
        printf("grep: '%s': No such file\n", path);
        return 1;
    }

    char buf[256];
    int32_t nr;
    int buf_pos = 0;
    int found = 0;
    while ((nr = vfs_read(f, buf + buf_pos, 255 - buf_pos)) > 0) {
        buf_pos += nr;
        buf[buf_pos] = '\0';
        char *line_start = buf;
        char *newline;
        while ((newline = strchr(line_start, '\n')) != 0) {
            *newline = '\0';
            if (strstr(line_start, pattern) != 0) {
                printf("%s\n", line_start);
                found++;
            }
            line_start = newline + 1;
        }
        uint32_t remaining = (uint32_t)buf_pos - (uint32_t)(line_start - buf);
        memmove(buf, line_start, remaining);
        buf_pos = (int)remaining;
    }
    if (buf_pos > 0) {
        buf[buf_pos] = '\0';
        if (strstr(buf, pattern) != 0) {
            printf("%s\n", buf);
            found++;
        }
    }
    vfs_close(f);
    return (found > 0) ? 0 : 1;
}
