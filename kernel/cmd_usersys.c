/*
 * kernel/cmd_usersys.c
 * cmd_id, cmd_whoami, cmd_users, cmd_who, cmd_groups, cmd_umask
 *
 * User / identity / group reporting commands.
 */

#include "cmd_usersys.h"
#include "shell.h"
#include "shell_error.h"
#include "user.h"
#include "stdio.h"
#include "string.h"

extern uint32_t user_get_current_uid(void);
extern user_t *user_get_current(void);
extern user_t *user_get_by_index(uint32_t index);
extern uint32_t user_count(void);
extern uint32_t group_count(void);
extern group_t *group_get_by_index(uint32_t index);
extern int user_get_groups(uint32_t uid, uint32_t *groups, uint32_t max_groups);
extern int user_in_group(uint32_t uid, uint32_t gid);

static void print_user_line(user_t *u) {
    if (!u) return;
    char buf[256];
    snprintf(buf, sizeof(buf),
             "  %-16s uid=%-5u gid=%-5u%s home=%s\n",
             u->username,
             u->uid,
             u->gid,
             u->is_admin ? " [admin]" : "",
             u->home);
    shell_print(buf);
}

void cmd_id(const char *args) {
    user_t *u = user_get_current();
    if (!u) {
        shell_print("id: no user logged in\n");
        shell_last_exit_code = 1;
        return;
    }
    if (args && *args) {
        user_t *target = user_find_by_name(args);
        if (!target) {
            shell_print("id: no such user: ");
            shell_print(args);
            shell_print("\n");
            shell_last_exit_code = 1;
            return;
        }
        u = target;
    }
    char buf[256];
    snprintf(buf, sizeof(buf), "uid=%u(%s) gid=%u groups=", u->uid, u->username, u->gid);
    shell_print(buf);

    uint32_t gids[16];
    int n = user_get_groups(u->uid, gids, 16);
    if (n <= 0) {
        shell_print("(none)");
    } else {
        for (int i = 0; i < n; i++) {
            group_t *g = group_find_by_gid(gids[i]);
            if (i > 0) shell_print(",");
            if (g) {
                snprintf(buf, sizeof(buf), "%u(%s)", gids[i], g->name);
            } else {
                snprintf(buf, sizeof(buf), "%u", gids[i]);
            }
            shell_print(buf);
        }
    }
    if (u->is_admin) {
        shell_print(" [admin]");
    }
    shell_print("\n");
    shell_last_exit_code = 0;
}

void cmd_whoami(const char *args) {
    (void)args;
    user_t *u = user_get_current();
    if (!u) {
        shell_print("whoami: no user logged in\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_print(u->username);
    shell_print("\n");
    shell_last_exit_code = 0;
}

void cmd_users(const char *args) {
    (void)args;
    uint32_t count = user_count();
    if (count == 0) {
        shell_print("users: no users in database\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_print("Login users:\n");
    for (uint32_t i = 0; i < count; i++) {
        user_t *u = user_get_by_index(i);
        print_user_line(u);
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "Total: %u user(s)\n", count);
    shell_print(buf);
    shell_last_exit_code = 0;
}

void cmd_who(const char *args) {
    (void)args;
    shell_print("  USER      TTY           LOGIN@\n");
    /* Single pseudo-TTY session for now */
    user_t *u = user_get_current();
    if (u) {
        char buf[128];
        snprintf(buf, sizeof(buf), "  %-9s tty1          system\n", u->username);
        shell_print(buf);
    }
    shell_last_exit_code = 0;
}

void cmd_groups(const char *args) {
    user_t *u = user_get_current();
    if (args && *args) {
        u = user_find_by_name(args);
        if (!u) {
            shell_print("groups: no such user: ");
            shell_print(args);
            shell_print("\n");
            shell_last_exit_code = 1;
            return;
        }
    }
    if (!u) {
        shell_print("groups: no user logged in\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_print(u->username);
    shell_print(" : ");
    uint32_t gids[16];
    int n = user_get_groups(u->uid, gids, 16);
    if (n <= 0) {
        shell_print("(no groups)\n");
    } else {
        for (int i = 0; i < n; i++) {
            group_t *g = group_find_by_gid(gids[i]);
            if (i > 0) shell_print(" ");
            if (g) {
                char buf[64];
                snprintf(buf, sizeof(buf), "%u(%s)", g->gid, g->name);
                shell_print(buf);
            } else {
                char buf[32];
                snprintf(buf, sizeof(buf), "%u", gids[i]);
                shell_print(buf);
            }
        }
        shell_print("\n");
    }
    shell_last_exit_code = 0;
}

void cmd_umask(const char *args) {
    /* Simple in-memory umask; just print current default */
    if (!args || !*args) {
        shell_print("umask - display or set file creation mask\n");
        shell_print("Usage: umask [MASK]\n");
        shell_print("  MASK is a 3-digit octal value (e.g. 022, 077).\n");
        shell_print("  With no argument, prints the current mask.\n");
        shell_print("\n  Note: persistent umask is not yet wired - use 'chmod' to\n");
        shell_print("  set permissions on freshly created files.\n");
        shell_print("\nCurrent mask: 022 (default)\n");
        shell_last_exit_code = 0;
        return;
    }
    /* Validate octal */
    int len = 0;
    for (const char *p = args; *p; p++) {
        if (*p < '0' || *p > '7') {
            shell_print("umask: invalid octal mask: ");
            shell_print(args);
            shell_print("\n");
            shell_last_exit_code = 1;
            return;
        }
        len++;
    }
    if (len < 3 || len > 4) {
        shell_print("umask: mask must be 3 or 4 octal digits\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_print("umask: set to ");
    shell_print(args);
    shell_print(" (not persistently applied yet)\n");
    shell_last_exit_code = 0;
}
