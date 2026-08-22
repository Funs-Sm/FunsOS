/*
 * kernel/cmd_quota.c - Quota subsystem shell commands (FunsOS v0.9)
 *
 * Implements:
 *   quota [on|off|status]        - enable/disable/show quota subsystem
 *   setquota -u UID -b SOFT,HARD -i SOFT,HARD  - set user quota limits
 *   setquota -g GID -b SOFT,HARD -i SOFT,HARD  - set group quota limits
 *   repquota [-u|-g]              - report quota usage
 *   quota -u UID | -g GID         - show specific user/group quota
 */

#include "cmd_quota.h"
#include "shell.h"
#include "quota.h"
#include "quota_db.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "klog.h"

static const char *next_token(const char **p)
{
    while (**p == ' ') (*p)++;
    if (**p == '\0') return NULL;
    const char *start = *p;
    while (**p && **p != ' ') (*p)++;
    return start;
}

static uint64_t parse_uint64(const char *s)
{
    if (!s) return 0;
    uint64_t val = 0;
    while (*s) {
        if (*s >= '0' && *s <= '9') {
            val = val * 10 + (*s - '0');
        } else {
            return 0;
        }
        s++;
    }
    return val;
}

static int parse_limits(const char *s, uint64_t *soft, uint64_t *hard)
{
    if (!s || !soft || !hard) return -1;
    const char *comma = NULL;
    while (*s) {
        if (*s == ',') { comma = s; break; }
        s++;
    }
    if (!comma) {
        *soft = parse_uint64(s);
        *hard = 0;  /* 0 = no limit */
        return 0;
    }
    char buf[32];
    size_t len = comma - s;
    if (len >= sizeof(buf)) return -1;
    memcpy(buf, s, len);
    buf[len] = '\0';
    *soft = parse_uint64(buf);
    *hard = parse_uint64(comma + 1);
    return 0;
}

/* ---- quota [on|off|status] ---- */
void cmd_quota_status(const char *args)
{
    const char *p = args;
    const char *tok = next_token(&p);

    if (!tok || strcmp(tok, "status") == 0 || strcmp(tok, "on") == 0 || strcmp(tok, "off") == 0) {
        quota_stats_t st;
        quota_get_stats(&st);
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "quota: %s  users=%u groups=%u  blocks=%llu  inodes=%llu\n"
                 "       grace: blocks=%us inodes=%us\n",
                 st.enabled ? "enabled" : "disabled",
                 (unsigned)st.user_entries,
                 (unsigned)st.group_entries,
                 (unsigned long long)st.total_blocks,
                 (unsigned long long)st.total_inodes,
                 (unsigned)st.grace_btime,
                 (unsigned)st.grace_itime);
        shell_print(buf);

        if (tok && strcmp(tok, "on") == 0) {
            quota_enable(1);
            shell_print("quota: enabled\n");
        } else if (tok && strcmp(tok, "off") == 0) {
            quota_enable(0);
            shell_print("quota: disabled\n");
        }
        shell_last_exit_code = 0;
        return;
    }

    shell_print("Usage: quota [on|off|status]\n");
    shell_last_exit_code = 1;
}

/* ---- setquota -u UID -b SOFT,HARD -i SOFT,HARD ---- */
void cmd_setquota(const char *args)
{
    const char *p = args;
    const char *tok;
    int type = QUOTA_TYPE_USER;
    uint32_t id = 0;
    uint64_t bsoft = 0, bhard = 0, isoft = 0, ihard = 0;
    int have_block = 0, have_inode = 0;

    while ((tok = next_token(&p)) != NULL) {
        if (strcmp(tok, "-u") == 0) {
            tok = next_token(&p);
            if (!tok) { shell_print("setquota: missing UID\n"); shell_last_exit_code = 1; return; }
            type = QUOTA_TYPE_USER;
            id = (uint32_t)parse_uint64(tok);
        } else if (strcmp(tok, "-g") == 0) {
            tok = next_token(&p);
            if (!tok) { shell_print("setquota: missing GID\n"); shell_last_exit_code = 1; return; }
            type = QUOTA_TYPE_GROUP;
            id = (uint32_t)parse_uint64(tok);
        } else if (strcmp(tok, "-b") == 0) {
            tok = next_token(&p);
            if (!tok) { shell_print("setquota: missing block limits\n"); shell_last_exit_code = 1; return; }
            if (parse_limits(tok, &bsoft, &bhard) != 0) { shell_print("setquota: invalid block limits\n"); shell_last_exit_code = 1; return; }
            have_block = 1;
        } else if (strcmp(tok, "-i") == 0) {
            tok = next_token(&p);
            if (!tok) { shell_print("setquota: missing inode limits\n"); shell_last_exit_code = 1; return; }
            if (parse_limits(tok, &isoft, &ihard) != 0) { shell_print("setquota: invalid inode limits\n"); shell_last_exit_code = 1; return; }
            have_inode = 1;
        } else {
            char buf[64];
            snprintf(buf, sizeof(buf), "setquota: unknown option '%s'\n", tok);
            shell_print(buf);
            shell_last_exit_code = 1;
            return;
        }
    }

    if (id == 0) {
        shell_print("Usage: setquota -u UID | -g GID [-b SOFT,HARD] [-i SOFT,HARD]\n");
        shell_last_exit_code = 1;
        return;
    }

    int ret;
    if (type == QUOTA_TYPE_USER) {
        ret = quota_set_user(id, bsoft, bhard, isoft, ihard);
    } else {
        ret = quota_set_group(id, bsoft, bhard, isoft, ihard);
    }

    if (ret == 0) {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "setquota: %s %u set  b=%llu/%llu i=%llu/%llu\n",
                 type == QUOTA_TYPE_USER ? "user" : "group",
                 (unsigned)id,
                 (unsigned long long)bsoft,
                 (unsigned long long)bhard,
                 (unsigned long long)isoft,
                 (unsigned long long)ihard);
        shell_print(buf);
        /* Sync to persistent storage */
        quota_sync();
        shell_last_exit_code = 0;
    } else {
        shell_print("setquota: failed to set quota\n");
        shell_last_exit_code = 1;
    }
}

/* ---- repquota [-u|-g] ---- */
void cmd_repquota(const char *args)
{
    const char *p = args;
    const char *tok = next_token(&p);
    int type = QUOTA_TYPE_USER;  /* default: user */

    if (tok) {
        if (strcmp(tok, "-u") == 0) {
            type = QUOTA_TYPE_USER;
        } else if (strcmp(tok, "-g") == 0) {
            type = QUOTA_TYPE_GROUP;
        } else if (strcmp(tok, "-a") == 0) {
            /* Report all: first users, then groups */
            shell_print("*** User quota report ***\n");
            type = QUOTA_TYPE_USER;
            goto print_report;
print_group:
            shell_print("\n*** Group quota report ***\n");
            type = QUOTA_TYPE_GROUP;
            goto print_report;
        } else {
            shell_print("Usage: repquota [-u|-g|-a]\n");
            shell_last_exit_code = 1;
            return;
        }
    }

print_report:
    {
        quota_entry_t entries[64];
        int count = quota_list(entries, 64, type);

        char header[128];
        snprintf(header, sizeof(header),
                 "%-10s %12s %12s %12s %12s %12s %s\n",
                 type == QUOTA_TYPE_USER ? "User" : "Group",
                 "Blocks Used", "Blocks Soft", "Blocks Hard",
                 "Inodes Used", "Inodes Soft", "Inodes Hard");
        shell_print(header);
        shell_print("--------------------------------------------------------------------------\n");

        char line[256];
        for (int i = 0; i < count; i++) {
            quota_entry_t *e = &entries[i];
            const char *flags = "";
            if (e->flags & QUOTA_FLAG_BLOCK_SOFT) flags = " [B-SOFT]";
            else if (e->flags & QUOTA_FLAG_BLOCK_HARD) flags = " [B-HARD]";
            else if (e->flags & QUOTA_FLAG_INODE_SOFT) flags = " [I-SOFT]";
            else if (e->flags & QUOTA_FLAG_INODE_HARD) flags = " [I-HARD]";

            snprintf(line, sizeof(line),
                     "%-10u %12llu %12llu %12llu %12llu %12llu %12llu%s\n",
                     (unsigned)e->id,
                     (unsigned long long)e->blocks_used,
                     (unsigned long long)e->blocks_soft,
                     (unsigned long long)e->blocks_hard,
                     (unsigned long long)e->inodes_used,
                     (unsigned long long)e->inodes_soft,
                     (unsigned long long)e->inodes_hard,
                     flags);
            shell_print(line);
        }

        if (count == 0) {
            shell_print("No quota entries found.\n");
        }

        shell_print("\n");

        if (tok && strcmp(tok, "-a") == 0 && type == QUOTA_TYPE_USER) {
            goto print_group;
        }
    }

    shell_last_exit_code = 0;
}

/* ---- quota -u UID | -g GID ---- */
void cmd_quota(const char *args)
{
    const char *p = args;
    const char *tok = next_token(&p);

    if (!tok) {
        cmd_quota_status("status");
        return;
    }

    if (strcmp(tok, "-u") == 0) {
        tok = next_token(&p);
        if (!tok) { shell_print("quota: missing UID\n"); shell_last_exit_code = 1; return; }
        uint32_t uid = (uint32_t)parse_uint64(tok);
        quota_entry_t *e = quota_get_user(uid);
        if (!e) { shell_print("quota: user not found\n"); shell_last_exit_code = 1; return; }
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "User %u:\n"
                 "  Blocks: used=%llu soft=%llu hard=%llu%s\n"
                 "  Inodes: used=%llu soft=%llu hard=%llu%s\n",
                 (unsigned)e->id,
                 (unsigned long long)e->blocks_used,
                 (unsigned long long)e->blocks_soft,
                 (unsigned long long)e->blocks_hard,
                 (e->flags & QUOTA_FLAG_BLOCK_HARD) ? " [EXCEEDED]" :
                 (e->flags & QUOTA_FLAG_BLOCK_SOFT) ? " [SOFT]" : "",
                 (unsigned long long)e->inodes_used,
                 (unsigned long long)e->inodes_soft,
                 (unsigned long long)e->inodes_hard,
                 (e->flags & QUOTA_FLAG_INODE_HARD) ? " [EXCEEDED]" :
                 (e->flags & QUOTA_FLAG_INODE_SOFT) ? " [SOFT]" : "");
        shell_print(buf);
        shell_last_exit_code = 0;
        return;
    }

    if (strcmp(tok, "-g") == 0) {
        tok = next_token(&p);
        if (!tok) { shell_print("quota: missing GID\n"); shell_last_exit_code = 1; return; }
        uint32_t gid = (uint32_t)parse_uint64(tok);
        quota_entry_t *e = quota_get_group(gid);
        if (!e) { shell_print("quota: group not found\n"); shell_last_exit_code = 1; return; }
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "Group %u:\n"
                 "  Blocks: used=%llu soft=%llu hard=%llu%s\n"
                 "  Inodes: used=%llu soft=%llu hard=%llu%s\n",
                 (unsigned)e->id,
                 (unsigned long long)e->blocks_used,
                 (unsigned long long)e->blocks_soft,
                 (unsigned long long)e->blocks_hard,
                 (e->flags & QUOTA_FLAG_BLOCK_HARD) ? " [EXCEEDED]" :
                 (e->flags & QUOTA_FLAG_BLOCK_SOFT) ? " [SOFT]" : "",
                 (unsigned long long)e->inodes_used,
                 (unsigned long long)e->inodes_soft,
                 (unsigned long long)e->inodes_hard,
                 (e->flags & QUOTA_FLAG_INODE_HARD) ? " [EXCEEDED]" :
                 (e->flags & QUOTA_FLAG_INODE_SOFT) ? " [SOFT]" : "");
        shell_print(buf);
        shell_last_exit_code = 0;
        return;
    }

    /* Fallback to status */
    cmd_quota_status(args);
}
