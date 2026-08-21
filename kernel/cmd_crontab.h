/*
 * kernel/cmd_crontab.h - PR-N (crontab command modularization)
 * cmd_crontab 模块对外接口
 */
#ifndef _KERNEL_CMD_CRONTAB_H
#define _KERNEL_CMD_CRONTAB_H

void cmd_crontab(const char *subcmd, const char *arg1, const char *arg2);

#endif /* _KERNEL_CMD_CRONTAB_H */
