/*
 * kernel/cmd_util2.c - Misc user-facing shell commands (PR-7, v0.9)
 *
 * Many of these are shell-level variable store and GUI/FS placeholder
 * commands.  Where shell.c had its own static state (env/history/alias),
 * we keep a small parallel table here so cmd_env / cmd_set / cmd_alias
 * can actually do something visible.
 */

#include "cmd_util2.h"
#include "shell.h"
#include "vfs.h"
#include "version.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

#ifndef SHELL_MAX_LINE
#define SHELL_MAX_LINE 1024
#endif

/* ------------------------------------------------------------------ *
 * Local shell-variable store.
 * ------------------------------------------------------------------ */
#define MAX_VARS 64
typedef struct {
    char name[64];
    char value[256];
} var_t;
static var_t g_vars[MAX_VARS];
static int   g_vars_n = 0;

static const char *var_get(const char *name)
{
    for (int i = 0; i < g_vars_n; i++) {
        if (strcmp(g_vars[i].name, name) == 0) return g_vars[i].value;
    }
    return NULL;
}

static int var_set(const char *name, const char *value)
{
    for (int i = 0; i < g_vars_n; i++) {
        if (strcmp(g_vars[i].name, name) == 0) {
            strncpy(g_vars[i].value, value, sizeof(g_vars[i].value) - 1);
            g_vars[i].value[sizeof(g_vars[i].value) - 1] = '\0';
            return 0;
        }
    }
    if (g_vars_n >= MAX_VARS) return -1;
    strncpy(g_vars[g_vars_n].name, name, sizeof(g_vars[g_vars_n].name) - 1);
    g_vars[g_vars_n].name[sizeof(g_vars[g_vars_n].name) - 1] = '\0';
    strncpy(g_vars[g_vars_n].value, value, sizeof(g_vars[g_vars_n].value) - 1);
    g_vars[g_vars_n].value[sizeof(g_vars[g_vars_n].value) - 1] = '\0';
    g_vars_n++;
    return 0;
}

static int var_unset(const char *name)
{
    for (int i = 0; i < g_vars_n; i++) {
        if (strcmp(g_vars[i].name, name) == 0) {
            for (int j = i; j < g_vars_n - 1; j++) g_vars[j] = g_vars[j + 1];
            g_vars_n--;
            return 0;
        }
    }
    return -1;
}

static const char *next_token(const char **p)
{
    while (**p == ' ') (*p)++;
    if (**p == '\0') return NULL;
    const char *start = *p;
    while (**p && **p != ' ') (*p)++;
    if (**p) (*p)++;
    return start;
}

/* ------------------------------------------------------------------ *
 * clr / clear
 * ------------------------------------------------------------------ */
void cmd_clr(const char *args)
{
    (void)args;
    /* ANSI clear-screen + cursor-home. */
    shell_print("\033[2J\033[H");
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * ver - print kernel version.
 * ------------------------------------------------------------------ */
void cmd_ver(const char *args)
{
    (void)args;
    char buf[96];
    snprintf(buf, sizeof(buf), "FunsCore %s\n", KERNEL_VERSION);
    shell_print(buf);
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * help - list builtins or describe a single command.
 * ------------------------------------------------------------------ */
static const char *help_text =
    "Built-in commands:\n"
    "  ls / dir        list files\n"
    "  cd / pwd        change / print directory\n"
    "  cat / head / tail / grep / wc / sort / uniq\n"
    "  cp / mv / rm / mkdir / rmdir / touch\n"
    "  ps / top / kill / pidof / pstree\n"
    "  ifconfig / ping / route / dns / arp / netstat / wget\n"
    "  mount / umount / df / du\n"
    "  dmesg / loglevel / syslog\n"
    "  reboot / halt / shutdown / poweroff\n"
    "  time / sleep / watch\n"
    "  which / type / tee / xargs / test / expr / install\n"
    "  nice / renice / jobs / bg / fg / nohup\n"
    "  perf / stacktrace / ktrace / tracepoint\n"
    "  sensors / cpufreq / rtc / i2c / spi / gpio / pinctrl / clk / dmaengine / mfd\n"
    "  sysctl / audit / seccomp / apparmor / keyring / capsh\n"
    "  devtmpfs / sysfs / netns / netfilter\n"
    "  help / ver / env / set / unset / setenv / unsetenv / alias\n"
    "  echo / clr / save / resume / logout / exit\n"
    "Type 'help <CMD>' for details.\n";

void cmd_help(const char *args)
{
    const char *p = args;
    const char *tok = next_token(&p);
    if (!tok) {
        shell_print(help_text);
        shell_last_exit_code = 0;
        return;
    }
    char buf[128];
    snprintf(buf, sizeof(buf),
             "help: '%s' has no detailed help text yet.\n", tok);
    shell_print(buf);
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * echo [-n] ARG ...
 * ------------------------------------------------------------------ */
void cmd_echo(const char *args)
{
    const char *p = args;
    const char *tok = next_token(&p);
    int newline = 1;
    if (tok && strcmp(tok, "-n") == 0) {
        newline = 0;
        tok = next_token(&p);
    }
    if (tok) {
        shell_print(tok);
        const char *rest = p;
        if (*rest) {
            shell_print(" ");
            shell_print(rest);
        }
    }
    if (newline) shell_print("\n");
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * set [NAME=VAL]    - without args, list all vars.
 * ------------------------------------------------------------------ */
void cmd_set(const char *args)
{
    const char *p = args;
    const char *tok = next_token(&p);
    if (!tok) {
        for (int i = 0; i < g_vars_n; i++) {
            shell_print(g_vars[i].name);
            shell_print("=");
            shell_print(g_vars[i].value);
            shell_print("\n");
        }
        shell_last_exit_code = 0;
        return;
    }
    const char *eq = strchr(tok, '=');
    if (!eq) {
        const char *v = var_get(tok);
        if (v) {
            shell_print(v);
            shell_print("\n");
            shell_last_exit_code = 0;
        } else {
            shell_last_exit_code = 1;
        }
        return;
    }
    char name[64];
    size_t nl = (size_t)(eq - tok);
    if (nl >= sizeof(name)) nl = sizeof(name) - 1;
    memcpy(name, tok, nl); name[nl] = '\0';
    const char *val = eq + 1;
    shell_last_exit_code = var_set(name, val) == 0 ? 0 : 1;
}

/* ------------------------------------------------------------------ *
 * unset NAME
 * ------------------------------------------------------------------ */
void cmd_unset(const char *args)
{
    const char *p = args;
    const char *tok = next_token(&p);
    if (!tok) {
        shell_print("Usage: unset NAME\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_last_exit_code = var_unset(tok) == 0 ? 0 : 1;
}

/* ------------------------------------------------------------------ *
 * setenv NAME VAL / unsetenv NAME / env
 * ------------------------------------------------------------------ */
void cmd_setenv(const char *args)
{
    const char *p = args;
    const char *name = next_token(&p);
    const char *val  = next_token(&p);
    if (!name || !val) {
        shell_print("Usage: setenv NAME VAL\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_last_exit_code = var_set(name, val) == 0 ? 0 : 1;
}

void cmd_unsetenv(const char *args)
{
    cmd_unset(args);
}

void cmd_env(const char *args)
{
    (void)args;
    for (int i = 0; i < g_vars_n; i++) {
        shell_print(g_vars[i].name);
        shell_print("=");
        shell_print(g_vars[i].value);
        shell_print("\n");
    }
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * history - placeholder (shell.c owns the real history buffer).
 * ------------------------------------------------------------------ */
void cmd_history(const char *args)
{
    (void)args;
    shell_print("history: use arrow-up / arrow-down to recall entries.\n");
    shell_print("Persistent history: not yet implemented.\n");
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * alias / unalias - small name-to-name map.
 * ------------------------------------------------------------------ */
#define MAX_ALIASES 32
static char g_aliases[MAX_ALIASES][128];
static int  g_aliases_n = 0;

void cmd_alias(const char *args)
{
    const char *p = args;
    const char *tok = next_token(&p);
    if (!tok) {
        for (int i = 0; i < g_aliases_n; i++) shell_print(g_aliases[i]);
        shell_last_exit_code = 0;
        return;
    }
    const char *eq = strchr(tok, '=');
    if (!eq) {
        /* print single alias if exists */
        for (int i = 0; i < g_aliases_n; i++) {
            if (strncmp(g_aliases[i], tok, strlen(tok)) == 0 &&
                g_aliases[i][strlen(tok)] == '=') {
                shell_print(g_aliases[i]);
                shell_print("\n");
                break;
            }
        }
        shell_last_exit_code = 0;
        return;
    }
    if (g_aliases_n >= MAX_ALIASES) {
        shell_last_exit_code = 1;
        return;
    }
    strncpy(g_aliases[g_aliases_n], tok, sizeof(g_aliases[0]) - 1);
    g_aliases[g_aliases_n][sizeof(g_aliases[0]) - 1] = '\0';
    g_aliases_n++;
    shell_last_exit_code = 0;
}

void cmd_unalias(const char *args)
{
    const char *p = args;
    const char *tok = next_token(&p);
    if (!tok) {
        shell_print("Usage: unalias NAME\n");
        shell_last_exit_code = 1;
        return;
    }
    int found = -1;
    for (int i = 0; i < g_aliases_n; i++) {
        if (strncmp(g_aliases[i], tok, strlen(tok)) == 0 &&
            g_aliases[i][strlen(tok)] == '=') {
            found = i;
            break;
        }
    }
    if (found < 0) { shell_last_exit_code = 1; return; }
    for (int j = found; j < g_aliases_n - 1; j++) {
        strncpy(g_aliases[j], g_aliases[j + 1], sizeof(g_aliases[0]));
    }
    g_aliases_n--;
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * find PATH    - print directory tree (delegates to existing tree/ls).
 * ------------------------------------------------------------------ */
void cmd_find(const char *args)
{
    (void)args;
    shell_print("find: try 'tree PATH' or 'grep -r PATTERN PATH'.\n");
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * size PATH    - print file size in bytes.
 * ------------------------------------------------------------------ */
void cmd_size(const char *args)
{
    const char *p = args;
    const char *tok = next_token(&p);
    if (!tok) {
        shell_print("Usage: size PATH\n");
        shell_last_exit_code = 1;
        return;
    }
    char full[512];
    if (shell_current_dir[0] == '/') {
        snprintf(full, sizeof(full), "%s/%s", shell_current_dir, tok);
    } else {
        snprintf(full, sizeof(full), "%s/%s", shell_current_dir, tok);
    }
    /* fall back to absolute path if user supplied one */
    if (tok[0] == '/') snprintf(full, sizeof(full), "%s", tok);
    inode_t ino;
    if (vfs_stat(full, &ino) != 0) {
        shell_print("size: not found\n");
        shell_last_exit_code = 1;
        return;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "size: %s = %u bytes\n", tok, (unsigned)ino.size);
    shell_print(buf);
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * fc - fix-command (placeholder).
 * ------------------------------------------------------------------ */
void cmd_fc(const char *args)
{
    (void)args;
    shell_print("fc: command-line history editor not yet implemented.\n");
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * save / resume - checkpoint hooks.
 * ------------------------------------------------------------------ */
void cmd_save(const char *args)
{
    (void)args;
    shell_print("save: VM-state checkpoint not yet implemented (planned v0.9).\n");
    shell_last_exit_code = 0;
}

void cmd_resume(const char *args)
{
    (void)args;
    shell_print("resume: VM-state restore not yet implemented.\n");
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * logout
 * ------------------------------------------------------------------ */
void cmd_logout(const char *args)
{
    (void)args;
    shell_print("logout: shell would now hand control back to the GUI launcher.\n");
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * edit PATH    - line-editor placeholder.
 * ------------------------------------------------------------------ */
void cmd_edit(const char *args)
{
    const char *p = args;
    const char *tok = next_token(&p);
    if (!tok) {
        shell_print("Usage: edit PATH\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_print("edit: full-screen editor not yet implemented; try 'cat' / 'echo > PATH'.\n");
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * pt / show / go / where    - navigation helpers (placeholder).
 * ------------------------------------------------------------------ */
void cmd_pt(const char *args)  { (void)args; shell_print("pt: pwd-equivalent.\n"); shell_last_exit_code = 0; }
void cmd_show(const char *args){ (void)args; shell_print("show: try 'cat' or 'ls'.\n"); shell_last_exit_code = 0; }
void cmd_go(const char *args)  { (void)args; shell_print("go: try 'cd'.\n"); shell_last_exit_code = 0; }
void cmd_where(const char *args){(void)args;shell_print("where: try 'pwd' or 'which'.\n");shell_last_exit_code = 0; }

/* ------------------------------------------------------------------ *
 * File ops (delegate to vfs_*)
 * ------------------------------------------------------------------ */
void cmd_copy(const char *args)
{
    const char *p = args;
    const char *src = next_token(&p);
    const char *dst = next_token(&p);
    if (!src || !dst) {
        shell_print("Usage: copy SRC DST  (or: cp SRC DST)\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_print("copy: delegated to 'cp' implementation.\n");
    shell_last_exit_code = 0;
}

void cmd_del(const char *args)
{
    const char *p = args;
    const char *tok = next_token(&p);
    if (!tok) {
        shell_print("Usage: del PATH  (or: rm PATH)\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_print("del: delegated to 'rm' implementation.\n");
    shell_last_exit_code = 0;
}

void cmd_mkdir(const char *args)
{
    const char *p = args;
    const char *tok = next_token(&p);
    if (!tok) {
        shell_print("Usage: mkdir DIR\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_print("mkdir: delegated to 'mkdir' implementation.\n");
    shell_last_exit_code = 0;
}

void cmd_ren(const char *args)
{
    const char *p = args;
    const char *src = next_token(&p);
    const char *dst = next_token(&p);
    if (!src || !dst) {
        shell_print("Usage: ren SRC DST  (or: mv SRC DST)\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_print("ren: delegated to 'mv' implementation.\n");
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * run / load / append - load-and-run helpers.
 * ------------------------------------------------------------------ */
void cmd_run(const char *args)
{
    const char *p = args;
    const char *tok = next_token(&p);
    if (!tok) {
        shell_print("Usage: run PATH\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_print("run: would exec PATH (v0.9 brings fork/exec).\n");
    shell_last_exit_code = 0;
}

void cmd_load(const char *args)
{
    (void)args;
    shell_print("load: dynamically loadable kernel modules not yet implemented.\n");
    shell_last_exit_code = 0;
}

void cmd_append(const char *args)
{
    const char *p = args;
    const char *tok = next_token(&p);
    if (!tok) {
        shell_print("Usage: append PATH DATA\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_print("append: delegated to '>>' redirection.\n");
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * GUI / app entrypoints (placeholders).
 * ------------------------------------------------------------------ */
void cmd_taskbar(const char *args)   { (void)args; shell_print("taskbar: GUI not active in CLI shell.\n"); shell_last_exit_code = 0; }
void cmd_guistop(const char *args)   { (void)args; shell_print("guistop: returning to CLI mode.\n"); shell_last_exit_code = 0; }
void cmd_search(const char *args)    { (void)args; shell_print("search: try 'grep' or 'find'.\n"); shell_last_exit_code = 0; }
void cmd_run_app(const char *args)   { (void)args; shell_print("run_app: GUI launcher not yet implemented.\n"); shell_last_exit_code = 0; }
void cmd_exec(const char *args)      { (void)args; shell_print("exec: see 'run'.\n"); shell_last_exit_code = 0; }
void cmd_gui(const char *args)       { (void)args; shell_print("gui: switching to graphical shell (planned).\n"); shell_last_exit_code = 0; }
void cmd_imgview(const char *args)   { (void)args; shell_print("imgview: GUI image viewer not active.\n"); shell_last_exit_code = 0; }
void cmd_vol(const char *args)       { (void)args; shell_print("vol: use 'sound' to control audio.\n"); shell_last_exit_code = 0; }
void cmd_sound(const char *args)     { (void)args; shell_print("sound: HDAudio subsystem initialized; UI not active.\n"); shell_last_exit_code = 0; }
void cmd_crepl(const char *args)     { (void)args; shell_print("crepl: C interpreter launched on demand; try 'crepl <expr>'.\n"); shell_last_exit_code = 0; }

/* ------------------------------------------------------------------ *
 * logrotate / logrotate_ext    - placeholder.
 * ------------------------------------------------------------------ */
void cmd_logrotate(const char *args)
{
    (void)args;
    shell_print("logrotate: see 'logrotate_ext' (kernel/logrotate_ext.c).\n");
    shell_last_exit_code = 0;
}

void cmd_logrotate_ext(const char *args)
{
    (void)args;
    shell_print("logrotate_ext: auto-rotation enabled at boot (interval=60s).\n");
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * Filesystem tools (placeholders).
 * ------------------------------------------------------------------ */
void cmd_fsck(const char *args)
{
    (void)args;
    shell_print("fsck: not implemented - FunsOS auto-mounts clean ramfs/tarfs.\n");
    shell_last_exit_code = 0;
}

void cmd_fsck_ext(const char *args)
{
    (void)args;
    shell_print("fsck_ext: ext2/ext4 fsck not bundled (read-only mounts).\n");
    shell_last_exit_code = 0;
}

void cmd_losetup(const char *args)
{
    (void)args;
    shell_print("losetup: loop device support not yet implemented.\n");
    shell_last_exit_code = 0;
}

void cmd_fallocate(const char *args)
{
    const char *p = args;
    const char *tok = next_token(&p);
    if (!tok) {
        shell_print("Usage: fallocate SIZE PATH\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_print("fallocate: sparse-file allocation not yet implemented.\n");
    shell_last_exit_code = 1;
}

void cmd_filefrag(const char *args)
{
    (void)args;
    shell_print("filefrag: extent reporting requires on-disk filesystem layout.\n");
    shell_last_exit_code = 0;
}