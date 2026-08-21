/*
 * kernel/cmd_service.h - PR-N (service command modularization)
 * cmd_service 模块对外接口
 */
#ifndef _KERNEL_CMD_SERVICE_H
#define _KERNEL_CMD_SERVICE_H

void cmd_service(const char *subcmd, const char *arg1, const char *arg2);

#endif /* _KERNEL_CMD_SERVICE_H */
