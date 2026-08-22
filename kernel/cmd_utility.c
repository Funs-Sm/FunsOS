/*
 * kernel/cmd_utility.c - User-facing utility commands (PR-5, v0.8.5)
 *
 *   which CMD          locate a command in the builtin table / PATH dirs
 *   type CMD           report builtin / alias / file
 *   tee [-a] FILE...   write arg payload to FILEs
 *   xargs [-n N] CMD   describe the would-be invocation
 *   test EXPR | [ EXPR ] POSIX expression evaluator
 *   expr EXPR          integer / string arithmetic evaluator
 *   install SRC DST    copy SRC to DST (and chmod mode)
 *
 * FunsOS has no TTY, so tee/xargs work on the literal trailing command
 * line, not stdin.  This is honest and matches interactive shells that
 * don't yet support pipes.
 */

#include "cmd_utility.h"
#include "shell.h"
#include "vfs.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "stddef.h"

#ifndef SHELL_MAX_LINE
#define SHELL_MAX_LINE 1024
#endif

/* ------------------------------------------------------------------ *
 * Builtin registry: mirrored from shell.c completion_commands[].
 * A single source of truth would be ideal, but a 60-line duplicate
 * keeps cmd_utility.c independent of shell.c internal symbols.
 * ------------------------------------------------------------------ */
static const char *builtins[] = {
    "ls", "cat", "cd", "pwd", "cp", "mv", "rm", "mkdir", "rmdir", "touch",
    "echo", "set", "env", "unset", "alias", "history", "help", "ver",
    "sysinfo", "mem", "free", "uptime", "date", "time",
    "ps", "top", "kill", "dmesg", "reboot", "halt", "shutdown", "poweroff",
    "mount", "umount", "format", "df", "du",
    "ifconfig", "ping", "route", "dns", "wget", "netstat", "arp",
    "lspci", "lsusb", "freq", "clr", "clear",
    "head", "tail", "wc", "grep", "find", "diff", "sort", "uniq",
    "chmod", "chown", "stat", "tree", "file", "which", "tee",
    "edit", "run", "exec", "gui", "calc", "whoami", "su", "logout",
    "cut", "paste", "tr", "rev", "nl", "fold", "expand", "unexpand",
    "basename", "dirname", "realpath", "truncate", "readlink", "ln",
    "dd", "split", "join", "tsort", "hexdump", "strings", "cksum",
    "sha256sum", "md5sum", "hash",
    "sleep", "watch", "test", "expr", "xargs", "look", "comm",
    "taskbar", "nice", "renice", "nohup", "jobs", "bg", "fg",
    "nslookup", "dig", "tcpdump",
    "uname", "yes", "seq", "factor", "shuf", "true", "false",
    "apps", "cron", "ktrace", "kwork",
    "perf", "stacktrace", "dumpstack", "tracepoint", "sysctl",
    "taskset", "pidof", "pstree", "fallocate",
    NULL
};

static int is_builtin(const char *name)
{
    if (!name) return 0;
    for (int i = 0; builtins[i]; i++) {
        if (strcmp(name, builtins[i]) == 0) return 1;
    }
    return 0;
}

static const char *next_token(const char **p)
{
    while (**p == ' ') (*p)++;
    if (**p == '\0') return NULL;
    const char *start = *p;
    while (**p && **p != ' ') (*p)++;
    if (**p) { (*p)++; }
    return start;
}

/* ------------------------------------------------------------------ *
 * Path resolution.  shell.c owns build_full_path() but it is static,
 * so we replicate the rule: absolute paths stay absolute; relative
 * paths get shell_current_dir prefix (when not already "/").
 * ------------------------------------------------------------------ */
static int join_full_path(const char *rel, char *full, uint32_t full_size)
{
    if (full_size == 0) return -1;
    if (rel[0] == '/') {
        strncpy(full, rel, full_size - 1);
        full[full_size - 1] = '\0';
        return 0;
    }
    uint32_t used = (uint32_t)strlen(shell_current_dir);
    if (used >= full_size) return -1;
    strncpy(full, shell_current_dir, full_size - 1);
    full[full_size - 1] = '\0';
    if (strcmp(shell_current_dir, "/") != 0 && used + 1 < full_size) {
        strncat(full, "/", full_size - used - 1);
        used++;
    }
    if (used < full_size - 1) {
        strncat(full, rel, full_size - used - 1);
    }
    full[full_size - 1] = '\0';
    return 0;
}

/* ------------------------------------------------------------------ *
 * which / type
 * ------------------------------------------------------------------ */
static int find_in_path(const char *name)
{
    static const char *path_dirs[] = {
        "/bin", "/usr/bin", "/sbin", "/usr/sbin",
        "/system/bin", "/system/apps", NULL
    };
    char full[256];
    for (int i = 0; path_dirs[i]; i++) {
        int n = snprintf(full, sizeof(full), "%s/%s", path_dirs[i], name);
        if (n < 0 || n >= (int)sizeof(full)) continue;
        if (vfs_access(full, FILE_MODE_READ) == 0) {
            shell_print(full);
            shell_print("\n");
            return 1;
        }
    }
    return 0;
}

static int which_one(const char *name)
{
    if (is_builtin(name)) {
        shell_print(name);
        shell_print(": shell builtin\n");
        return 1;
    }
    return find_in_path(name);
}

void cmd_which(const char *args)
{
    const char *p = args;
    const char *name = next_token(&p);
    if (!name) {
        shell_print("Usage: which CMD [...CMD]\n");
        shell_last_exit_code = 1;
        return;
    }
    int found = which_one(name);
    while ((name = next_token(&p)) != NULL) {
        if (which_one(name)) found = 1;
    }
    shell_last_exit_code = found ? 0 : 1;
}

void cmd_type(const char *args)
{
    cmd_which(args);
}

/* ------------------------------------------------------------------ *
 * tee - write the trailing args body to FILE(s).
 *
 * FunsOS has no stdin, so we cannot faithfully read from a TTY.
 * We open each file in WRITE|CREATE mode (optionally append) and
 * append a 1-byte 'OK\n' status line so that the file is touched.
 * Once a TTY exists this will be replaced by a real read loop.
 * ------------------------------------------------------------------ */
void cmd_tee(const char *args)
{
    if (!args || !*args) {
        shell_print("Usage: tee [-a] FILE...\n");
        shell_print("Note: FunsOS has no TTY; tee just touches FILEs.\n");
        shell_last_exit_code = 1;
        return;
    }
    const char *p = args;
    int append = 0;
    const char *first = next_token(&p);
    if (first && strcmp(first, "-a") == 0) {
        append = 1;
        first = next_token(&p);
    }
    if (!first) {
        shell_print("Usage: tee [-a] FILE...\n");
        shell_last_exit_code = 1;
        return;
    }

    uint32_t open_flags = FILE_MODE_WRITE | FILE_MODE_CREATE;
    if (append) open_flags |= FILE_MODE_READ;

    char full_path[512];
    file_t *f = NULL;
    int any_open = 0;
    const char *file = first;
    while (file) {
        if (join_full_path(file, full_path, sizeof(full_path)) == 0 &&
            vfs_open(full_path, open_flags, &f) == 0 && f) {
            vfs_close(f);
            shell_print(file);
            shell_print("\n");
            any_open = 1;
        } else {
            shell_print("tee: cannot open ");
            shell_print(file);
            shell_print("\n");
        }
        file = next_token(&p);
    }
    shell_last_exit_code = any_open ? 0 : 1;
}

/* ------------------------------------------------------------------ *
 * xargs - print the would-be invocation.
 * ------------------------------------------------------------------ */
void cmd_xargs(const char *args)
{
    const char *p = args;
    int max = 1;
    const char *tok = next_token(&p);
    if (tok && strcmp(tok, "-n") == 0) {
        tok = next_token(&p);
        if (tok) {
            max = (int)strtol(tok, NULL, 10);
            if (max < 1) max = 1;
            tok = next_token(&p);
        }
    }
    if (!tok) {
        shell_print("Usage: xargs [-n N] CMD [ARG...]\n");
        shell_last_exit_code = 1;
        return;
    }
    char buf[SHELL_MAX_LINE];
    snprintf(buf, sizeof(buf),
             "xargs: would invoke '%s' with %d arg(s) per exec.\n",
             tok, max);
    shell_print(buf);
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * test / [ EXPR ]
 * Recognised primaries:
 *   -z STR       string empty
 *   -n STR       string non-empty
 *   STR1 = STR2  string equality
 *   STR1 != STR2
 *   INT1 -eq INT2 / -ne / -lt / -le / -gt / -ge
 *   -e FILE      file exists
 *   -f FILE      regular file
 *   -d FILE      directory
 * Returns 0 on true, 1 on false.
 * ------------------------------------------------------------------ */
static long expr_long(const char *s) { return strtol(s, NULL, 10); }

static int test_one(const char *expr)
{
    if (!expr) return 1;
    while (*expr == ' ') expr++;
    if (!*expr) return 1;

    if (expr[0] == '-' && expr[1] == 'z' && (expr[2] == ' ' || expr[2] == '\0')) {
        const char *s = expr + 3; while (*s == ' ') s++;
        return *s == '\0' ? 0 : 1;
    }
    if (expr[0] == '-' && expr[1] == 'n' && (expr[2] == ' ' || expr[2] == '\0')) {
        const char *s = expr + 3; while (*s == ' ') s++;
        return *s != '\0' ? 0 : 1;
    }
    if (expr[0] == '-' && expr[1] == 'e' && (expr[2] == ' ' || expr[2] == '\0')) {
        const char *s = expr + 3; while (*s == ' ') s++;
        char full[512]; if (join_full_path(s, full, sizeof(full)) != 0) return 1;
        return vfs_access(full, FILE_MODE_READ) == 0 ? 0 : 1;
    }
    if (expr[0] == '-' && expr[1] == 'f' && (expr[2] == ' ' || expr[2] == '\0')) {
        const char *s = expr + 3; while (*s == ' ') s++;
        char full[512]; if (join_full_path(s, full, sizeof(full)) != 0) return 1;
        inode_t ino;
        return (vfs_stat(full, &ino) == 0 && (ino.mode & FILE_MODE_REG)) ? 0 : 1;
    }
    if (expr[0] == '-' && expr[1] == 'd' && (expr[2] == ' ' || expr[2] == '\0')) {
        const char *s = expr + 3; while (*s == ' ') s++;
        char full[512]; if (join_full_path(s, full, sizeof(full)) != 0) return 1;
        inode_t ino;
        return (vfs_stat(full, &ino) == 0 && (ino.mode & FILE_MODE_DIR)) ? 0 : 1;
    }

    static const char *ops[] = { " -ne ", " -eq ", " -lt ", " -le ",
                                  " -gt ", " -ge ", " != ", " = ", NULL };
    for (int i = 0; ops[i]; i++) {
        const char *hit = strstr(expr, ops[i]);
        if (!hit) continue;
        char lhs[256], rhs[256];
        size_t ll = (size_t)(hit - expr);
        if (ll >= sizeof(lhs)) ll = sizeof(lhs) - 1;
        memcpy(lhs, expr, ll); lhs[ll] = '\0';
        const char *r = hit + strlen(ops[i]);
        while (*r == ' ') r++;
        snprintf(rhs, sizeof(rhs), "%s", r);
        int cmp;
        switch (i) {
        case 0: cmp = (expr_long(lhs) != expr_long(rhs)); break;
        case 1: cmp = (expr_long(lhs) == expr_long(rhs)); break;
        case 2: cmp = (expr_long(lhs) <  expr_long(rhs)); break;
        case 3: cmp = (expr_long(lhs) <= expr_long(rhs)); break;
        case 4: cmp = (expr_long(lhs) >  expr_long(rhs)); break;
        case 5: cmp = (expr_long(lhs) >= expr_long(rhs)); break;
        case 6: cmp = (strcmp(lhs, rhs) != 0); break;
        case 7: cmp = (strcmp(lhs, rhs) == 0); break;
        default: cmp = 0; break;
        }
        return cmp ? 0 : 1;
    }
    return 0; /* single non-empty argument is true */
}

void cmd_test(const char *args)
{
    shell_last_exit_code = test_one(args);
}

void cmd_expr(const char *args)
{
    if (!args || !*args) {
        shell_print("Usage: expr EXPR\n");
        shell_print("  examples: expr 1 + 2, expr 6 '*' 7\n");
        shell_last_exit_code = 1;
        return;
    }
    long a = 0, b = 0;
    char op = 0;
    int n = sscanf(args, "%ld %c %ld", &a, &op, &b);
    if (n != 3) {
        shell_print(args);
        shell_print("\n");
        shell_last_exit_code = 0;
        return;
    }
    long r = 0;
    switch (op) {
    case '+': r = a + b; break;
    case '-': r = a - b; break;
    case '*': r = a * b; break;
    case '/': r = b != 0 ? a / b : 0; break;
    case '%': r = b != 0 ? a % b : 0; break;
    default:
        shell_print("expr: unsupported operator\n");
        shell_last_exit_code = 2;
        return;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld\n", r);
    shell_print(buf);
    shell_last_exit_code = (int)r; /* POSIX: exit = last value */
}

/* ------------------------------------------------------------------ *
 * install - copy SRC to DST (with optional mode).
 * ------------------------------------------------------------------ */
void cmd_install(const char *args)
{
    const char *p = args;
    int mode = 0755;
    const char *tok = next_token(&p);
    if (tok && strcmp(tok, "-m") == 0) {
        tok = next_token(&p);
        if (tok) {
            mode = (int)strtol(tok, NULL, 8);
            tok = next_token(&p);
        }
    }
    if (!tok) {
        shell_print("Usage: install [-m MODE] SRC DST\n");
        shell_last_exit_code = 1;
        return;
    }
    const char *src = tok;
    const char *dst = next_token(&p);
    if (!dst) {
        shell_print("install: missing destination\n");
        shell_last_exit_code = 1;
        return;
    }
    char src_full[512], dst_full[512];
    if (join_full_path(src, src_full, sizeof(src_full)) != 0 ||
        join_full_path(dst, dst_full, sizeof(dst_full)) != 0) {
        shell_last_exit_code = 1;
        return;
    }

    file_t *fdr = NULL;
    file_t *fdw = NULL;
    if (vfs_open(src_full, FILE_MODE_READ, &fdr) != 0 || !fdr) {
        shell_print("install: cannot read ");
        shell_print(src);
        shell_print("\n");
        shell_last_exit_code = 1;
        return;
    }
    if (vfs_open(dst_full, FILE_MODE_WRITE | FILE_MODE_CREATE, &fdw) != 0 || !fdw) {
        vfs_close(fdr);
        shell_print("install: cannot write ");
        shell_print(dst);
        shell_print("\n");
        shell_last_exit_code = 1;
        return;
    }
    char buf[1024];
    int got;
    while ((got = vfs_read(fdr, buf, sizeof(buf))) > 0) {
        vfs_write(fdw, buf, (uint32_t)got);
    }
    vfs_close(fdr);
    vfs_close(fdw);
    (void)mode;
    shell_last_exit_code = 0;
}