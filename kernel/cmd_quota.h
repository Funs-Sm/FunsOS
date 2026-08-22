/*
 * kernel/cmd_quota.h - Quota subsystem shell commands (FunsOS v0.9)
 *
 * Commands:
 *   quota [on|off|status]   - manage quota subsystem
 *   setquota -u UID ...     - set user quota
 *   setquota -g GID ...     - set group quota
 *   repquota [-u|-g|-a]     - report quota usage
 */
#ifndef _KERNEL_CMD_QUOTA_H
#define _KERNEL_CMD_QUOTA_H

void cmd_quota(const char *args);
void cmd_quota_status(const char *args);
void cmd_setquota(const char *args);
void cmd_repquota(const char *args);

#endif /* _KERNEL_CMD_QUOTA_H */
