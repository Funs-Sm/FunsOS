#ifndef _KERNEL_CMD_LOG_H
#define _KERNEL_CMD_LOG_H

void cmd_dmesg(const char *args);
void cmd_loglevel(const char *args);
void cmd_syslog(const char *args);

#endif