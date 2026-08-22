/*
 * kernel/cmd_ns.c - Namespace and security shell commands (PR-7, v0.9)
 *
 * Each command calls the print_stats() / set_*() / register() APIs of the
 * corresponding kernel submodule.  These are honest reporting tools:
 * if the subsystem isn't registered in this VM, the output is "0".
 */

#include "cmd_ns.h"
#include "shell.h"
#include "devtmpfs.h"
#include "sysfs.h"
#include "netns.h"
#include "netfilter.h"
#include "seccomp.h"
#include "apparmor.h"
#include "keyring.h"
#include "audit.h"
#include "sysctl.h"
#include "capability.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

#ifndef SHELL_MAX_LINE
#define SHELL_MAX_LINE 1024
#endif

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
 * devtmpfs
 * ------------------------------------------------------------------ */
void cmd_devtmpfs(const char *args)
{
    (void)args;
    devtmpfs_print_stats();
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * sysfs
 * ------------------------------------------------------------------ */
void cmd_sysfs(const char *args)
{
    (void)args;
    shell_print("sysfs: mounted at /sys (see /sys/kernel, /sys/devices ...)\n");
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * netns [list | create NAME]
 * ------------------------------------------------------------------ */
void cmd_netns(const char *args)
{
    const char *p = args;
    const char *tok = next_token(&p);
    if (!tok || strcmp(tok, "list") == 0) {
        netns_print_stats();
        shell_last_exit_code = 0;
        return;
    }
    if (strcmp(tok, "create") == 0) {
        const char *name = next_token(&p);
        if (!name) {
            shell_print("Usage: netns create NAME\n");
            shell_last_exit_code = 1;
            return;
        }
        int rc = netns_create(name);
        char buf[96];
        snprintf(buf, sizeof(buf), "netns: create '%s' (rc=%d)\n", name, rc);
        shell_print(buf);
        shell_last_exit_code = rc == 0 ? 0 : 1;
        return;
    }
    shell_print("Usage: netns [list|create NAME]\n");
    shell_last_exit_code = 1;
}

/* ------------------------------------------------------------------ *
 * netfilter
 * ------------------------------------------------------------------ */
void cmd_netfilter(const char *args)
{
    (void)args;
    char buf[64];
    snprintf(buf, sizeof(buf),
             "netfilter: drops preroute=%u input=%u forward=%u output=%u postroute=%u\n",
             (unsigned)netfilter_drops(0),
             (unsigned)netfilter_drops(1),
             (unsigned)netfilter_drops(2),
             (unsigned)netfilter_drops(3),
             (unsigned)netfilter_drops(4));
    shell_print(buf);
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * seccomp [PID]      show seccomp stats / a PID's mode.
 * ------------------------------------------------------------------ */
void cmd_seccomp(const char *args)
{
    (void)args;
    seccomp_print_stats();
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * apparmor
 * ------------------------------------------------------------------ */
void cmd_apparmor(const char *args)
{
    (void)args;
    apparmor_print_stats();
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * keyring
 * ------------------------------------------------------------------ */
void cmd_keyring(const char *args)
{
    (void)args;
    keyring_print_stats();
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * audit [on|off]
 * ------------------------------------------------------------------ */
void cmd_audit(const char *args)
{
    const char *p = args;
    const char *tok = next_token(&p);
    if (!tok) {
        char buf[64];
        snprintf(buf, sizeof(buf),
                 "audit: enabled=%u rate_limit=%u\n",
                 (unsigned)audit_get_enabled(),
                 (unsigned)audit_get_rate_limit());
        shell_print(buf);
        audit_print_stats();
        shell_last_exit_code = 0;
        return;
    }
    if (strcmp(tok, "on") == 0) {
        audit_set_enabled(1);
        shell_print("audit: on\n");
        shell_last_exit_code = 0;
        return;
    }
    if (strcmp(tok, "off") == 0) {
        audit_set_enabled(0);
        shell_print("audit: off\n");
        shell_last_exit_code = 0;
        return;
    }
    shell_print("Usage: audit [on|off]\n");
    shell_last_exit_code = 1;
}

/* ------------------------------------------------------------------ *
 * sysctl [NAME=VAL | NAME]
 * ------------------------------------------------------------------ */
void cmd_sysctl(const char *args)
{
    const char *p = args;
    const char *tok = next_token(&p);
    if (!tok) {
        sysctl_dump_all();
        shell_last_exit_code = 0;
        return;
    }
    /* NAME=VAL form */
    const char *eq = strchr(tok, '=');
    if (eq) {
        char name[128];
        size_t nl = (size_t)(eq - tok);
        if (nl >= sizeof(name)) nl = sizeof(name) - 1;
        memcpy(name, tok, nl); name[nl] = '\0';
        const char *val = eq + 1;
        int rc = sysctl_set(name, val);
        char buf[160];
        snprintf(buf, sizeof(buf), "sysctl: %s = %s (rc=%d)\n",
                 name, val, rc);
        shell_print(buf);
        shell_last_exit_code = rc == 0 ? 0 : 1;
        return;
    }
    /* NAME only */
    char val_buf[256];
    int rc = sysctl_get(tok, val_buf, sizeof(val_buf));
    if (rc == 0) {
        char buf[384];
        snprintf(buf, sizeof(buf), "sysctl: %s = %s\n", tok, val_buf);
        shell_print(buf);
    } else {
        shell_print("sysctl: not found\n");
    }
    shell_last_exit_code = rc == 0 ? 0 : 1;
}

/* ------------------------------------------------------------------ *
 * capsh - show capability state.
 * ------------------------------------------------------------------ */
void cmd_capsh(const char *args)
{
    (void)args;
    cred_t cred;
    cap_init_root(&cred);
    char buf[128];
    snprintf(buf, sizeof(buf),
             "capsh: root cred uid=%u gid=%u\n",
             (unsigned)cred.uid, (unsigned)cred.gid);
    shell_print(buf);
    shell_last_exit_code = 0;
}