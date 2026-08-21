/*
 * kernel/cmd_path.c - P3: Path utility commands
 * basename, dirname, realpath, truncate
 */

#include "cmd_path.h"
#include "shell.h"
#include "shell_error.h"
#include "string.h"
#include "stdio.h"

void cmd_basename(const char *path, const char *suffix) {
    if (!path || !*path) {
        shell_print("basename - strip directory and suffix from filenames\n");
        shell_print("Usage: basename PATH [SUFFIX]\n");
        shell_print("  PATH     A pathname (any separators are normalized)\n");
        shell_print("  SUFFIX   If PATH ends with SUFFIX, also strip it\n");
        shell_print("\nExamples:\n");
        shell_print("  basename /usr/lib/libc.so       -> libc.so\n");
        shell_print("  basename /usr/lib/libc.so .so   -> libc\n");
        shell_last_exit_code = 1;
        return;
    }
    const char *p = path + strlen(path) - 1;
    while (p > path && p[-1] != '/' && p[-1] != '\\') p--;
    char buf[256];
    strncpy(buf, p, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    if (suffix && *suffix) {
        size_t len = strlen(buf);
        size_t slen = strlen(suffix);
        if (len > slen && strcmp(buf + len - slen, suffix) == 0) {
            buf[len - slen] = '\0';
        }
    }
    shell_print(buf);
    shell_print("\n");
    shell_last_exit_code = 0;
}

void cmd_dirname(const char *path) {
    if (!path || !*path) {
        shell_print("dirname - strip the last component from a pathname\n");
        shell_print("Usage: dirname PATH\n");
        shell_print("  Returns the directory portion of PATH.\n");
        shell_print("  If PATH contains no '/', output '.'.\n");
        shell_print("\nExamples:\n");
        shell_print("  dirname /usr/lib/libc.so   -> /usr/lib\n");
        shell_print("  dirname /home/user/         -> /home\n");
        shell_print("  dirname foo                 -> .\n");
        shell_last_exit_code = 1;
        return;
    }
    char buf[256];
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    size_t len = strlen(buf);
    while (len > 0 && (buf[len-1] == '/' || buf[len-1] == '\\')) len--;
    while (len > 0 && buf[len-1] != '/' && buf[len-1] != '\\') len--;
    if (len == 0) {
        shell_print(".\n");
        shell_last_exit_code = 0;
        return;
    }
    while (len > 1 && (buf[len-1] == '/' || buf[len-1] == '\\')) len--;
    buf[len] = '\0';
    shell_print(buf);
    shell_print("\n");
    shell_last_exit_code = 0;
}

void cmd_realpath(const char *path) {
    if (!path || !*path) {
        shell_print("realpath - print the resolved absolute path\n");
        shell_print("Usage: realpath [OPTION] FILE\n");
        shell_print("  -e            All path components must exist\n");
        shell_print("  -m            No path components need exist (canonicalize mode)\n");
        shell_print("  -q            Do not print a trailing newline\n");
        shell_print("  -s            Do not expand symlinks\n");
        shell_print("  -z            End each output line with NUL, not newline\n");
        shell_print("  --relative-to=DIR   Print result relative to DIR\n");
        shell_print("  --relative-base=DIR Print absolute paths unless below DIR\n");
        shell_last_exit_code = 1;
        return;
    }
    /* Stub: prefix-with-current-dir if relative (no real canonicalise yet) */
    const char *p = path;
    if (p[0] == '/' || p[0] == '\\') {
        shell_print(p);
    } else {
        shell_print("(unresolved) ");
        shell_print(p);
    }
    shell_print("\n");
    shell_print("  Note: full VFS-based resolution not yet implemented.\n");
    shell_last_exit_code = 0;
}

void cmd_truncate(const char *path, const char *size_str) {
    if (!path || !*path || !size_str || !*size_str) {
        shell_print("truncate - shrink or extend a file to a specified size\n");
        shell_print("Usage: truncate [OPTION]... SIZE FILE\n");
        shell_print("  SIZE may be absolute (e.g. 10K, 2M) or relative (+100, -1K).\n");
        shell_print("\nOptions:\n");
        shell_print("  -c, --no-create   Do not create the file if it does not exist\n");
        shell_print("  -o, --io-blocks   Treat SIZE as number of I/O blocks\n");
        shell_print("  -r, --reference=RFILE    Base size on RFILE\n");
        shell_print("  -s, --size=SIZE   Required: SIZE to set/extend\n");
        shell_print("\nSize suffixes: K=1024, M=1024K, G=1024M, T=1024G.\n");
        shell_last_exit_code = 1;
        return;
    }
    /* Best-effort: print what would happen */
    shell_print("truncate: stub (file system truncate not yet implemented)\n");
    shell_print("  would set '");
    shell_print(path);
    shell_print("' to size '");
    shell_print(size_str);
    shell_print("'\n");
    shell_last_exit_code = 1;
}
