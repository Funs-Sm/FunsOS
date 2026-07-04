#include "user_syscall.h"
#include "string.h"

static char buf[4096];

static void print_str(const char *s) {
    sys_write(1, s, strlen(s));
}

static void print_num(int n) {
    char tmp[16];
    int len = 0;
    if (n < 0) { sys_write(1, "-", 1); n = -n; }
    if (n == 0) { sys_write(1, "0", 1); return; }
    while (n > 0) { tmp[len++] = '0' + (n % 10); n /= 10; }
    for (int i = len - 1; i >= 0; i--) sys_write(1, &tmp[i], 1);
}

static void print_usage(void) {
    print_str("Usage: fsearch <path> <pattern> [options]\n");
    print_str("Search for files matching pattern.\n");
    print_str("\n");
    print_str("Options:\n");
    print_str("  -r        recursive search\n");
    print_str("  -i        case insensitive\n");
    print_str("  -t type   file type: f=file d=dir a=all\n");
    print_str("  -n name   search by name only\n");
    print_str("  -e ext    search by extension\n");
    print_str("  -h, --help show this help\n");
}

static int match_pattern(const char *str, const char *pattern, int case_sensitive) {
    if (!case_sensitive) {
        while (*pattern) {
            char c1 = *str;
            char c2 = *pattern;
            if (c1 >= 'A' && c1 <= 'Z') c1 = c1 - 'A' + 'a';
            if (c2 >= 'A' && c2 <= 'Z') c2 = c2 - 'A' + 'a';
            if (c2 == '*') {
                pattern++;
                if (*pattern == '\0') return 1;
                while (*str) {
                    if (match_pattern(str, pattern, case_sensitive)) return 1;
                    str++;
                }
                return 0;
            }
            if (c2 == '?') {
                if (*str == '\0') return 0;
                str++;
                pattern++;
                continue;
            }
            if (c1 != c2) return 0;
            str++;
            pattern++;
        }
        return *str == '\0';
    } else {
        while (*pattern) {
            if (*pattern == '*') {
                pattern++;
                if (*pattern == '\0') return 1;
                while (*str) {
                    if (match_pattern(str, pattern, case_sensitive)) return 1;
                    str++;
                }
                return 0;
            }
            if (*pattern == '?') {
                if (*str == '\0') return 0;
                str++;
                pattern++;
                continue;
            }
            if (*str != *pattern) return 0;
            str++;
            pattern++;
        }
        return *str == '\0';
    }
}

static void get_ext(const char *name, char *ext, int maxlen) {
    ext[0] = '\0';
    const char *dot = 0;
    while (*name) {
        if (*name == '.') dot = name;
        name++;
    }
    if (dot && dot[1]) {
        int i = 0;
        dot++;
        while (*dot && i < maxlen - 1) ext[i++] = *dot++;
        ext[i] = '\0';
    }
}

static void search_dir(const char *path, const char *pattern,
                       int recursive, int case_sensitive,
                       char type_filter, const char *ext_filter,
                       int *total) {
    char tmp_path[512];
    char entry_buf[8192];

    int fd = sys_open(path, 0);
    if (fd < 0) return;

    int n = sys_readdir(fd, entry_buf, sizeof(entry_buf));
    sys_close(fd);

    if (n <= 0) return;

    char *p = entry_buf;
    while (p < entry_buf + n) {
        char name[256];
        int nl = 0;
        while (p + nl < entry_buf + n && p[nl] != '\0' && p[nl] != '|' && nl < 255) {
            name[nl] = p[nl];
            nl++;
        }
        name[nl] = '\0';
        if (nl == 0) break;

        int is_dir = (p[nl] == '|') ? (p[nl + 1] == 'd') : 0;
        p += nl + 1;
        if (p < entry_buf + n && *p == '|') p++;
        p++;
        while (p < entry_buf + n && *p != '\0') p++;
        if (p < entry_buf + n) p++;

        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

        int match = 1;

        if (type_filter == 'f' && is_dir) match = 0;
        if (type_filter == 'd' && !is_dir) match = 0;

        if (match && pattern && *pattern) {
            match = match_pattern(name, pattern, case_sensitive);
        }

        if (match && ext_filter && *ext_filter) {
            char ext[64];
            get_ext(name, ext, sizeof(ext));
            if (strlen(ext) == 0 || strcmp(ext, ext_filter) != 0) match = 0;
        }

        if (match) {
            print_str(path);
            if (strcmp(path, "/") != 0) print_str("/");
            print_str(name);
            if (is_dir) print_str("/");
            print_str("\n");
            (*total)++;
        }

        if (recursive && is_dir) {
            strncpy(tmp_path, path, sizeof(tmp_path) - 1);
            int pl = strlen(tmp_path);
            if (pl > 0 && tmp_path[pl - 1] != '/') {
                tmp_path[pl++] = '/';
            }
            strncpy(tmp_path + pl, name, sizeof(tmp_path) - pl - 1);
            search_dir(tmp_path, pattern, recursive, case_sensitive,
                       type_filter, ext_filter, total);
        }
    }
}

int main(int argc, char *argv[]) {
    const char *path = ".";
    const char *pattern = "*";
    int recursive = 0;
    int case_sensitive = 1;
    char type_filter = 'a';
    const char *ext_filter = NULL;

    if (argc < 2) {
        print_usage();
        return 1;
    }

    int i = 1;
    while (i < argc) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "-r") == 0) {
                recursive = 1;
            } else if (strcmp(argv[i], "-i") == 0) {
                case_sensitive = 0;
            } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
                i++;
                type_filter = argv[i][0];
            } else if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) {
                i++;
                ext_filter = argv[i];
            } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
                print_usage();
                return 0;
            }
        } else if (i == 1) {
            path = argv[i];
        } else if (i == 2) {
            pattern = argv[i];
        }
        i++;
    }

    if (argc >= 3) pattern = argv[2];
    if (argc >= 2) path = argv[1];

    int total = 0;
    search_dir(path, pattern, recursive, case_sensitive,
               type_filter, ext_filter, &total);

    print_str("\n");
    print_num(total);
    print_str(" files found\n");

    return 0;
}
