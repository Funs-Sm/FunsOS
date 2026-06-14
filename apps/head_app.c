#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include "vfs.h"
#include "path.h"

int app_head_main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: head [-n <lines>] <file>\n");
        return 1;
    }

    int n = 10;
    int file_arg = 1;

    if (argc > 2 && argv[1][0] == '-' && argv[1][1] == 'n') {
        n = atoi(argv[2]);
        if (n <= 0) n = 10;
        file_arg = 3;
    }

    if (file_arg >= argc) {
        printf("head: missing file argument\n");
        return 1;
    }

    const char *path = argv[file_arg];
    file_t *f = 0;
    if (vfs_open(path, FILE_MODE_READ, &f) != 0 || !f) {
        printf("head: '%s': No such file\n", path);
        return 1;
    }

    char buf[256];
    int32_t nr;
    int line_count = 0;
    int buf_pos = 0;
    while ((nr = vfs_read(f, buf + buf_pos, 255 - buf_pos)) > 0) {
        buf_pos += nr;
        buf[buf_pos] = '\0';
        char *line_start = buf;
        char *newline;
        while ((newline = strchr(line_start, '\n')) != 0 && line_count < n) {
            *newline = '\0';
            printf("%s\n", line_start);
            line_count++;
            line_start = newline + 1;
        }
        if (line_count >= n) break;
        uint32_t remaining = (uint32_t)buf_pos - (uint32_t)(line_start - buf);
        memmove(buf, line_start, remaining);
        buf_pos = (int)remaining;
    }
    if (line_count < n && buf_pos > 0) {
        buf[buf_pos] = '\0';
        printf("%s\n", buf);
    }
    vfs_close(f);
    return 0;
}
