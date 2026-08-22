/*
 * kernel/cmd_time.h - Time-related commands (PR-5, v0.8.5)
 *   sleep, watch, time [CMD...]
 */
#ifndef _KERNEL_CMD_TIME_H
#define _KERNEL_CMD_TIME_H

void cmd_sleep(const char *args);
void cmd_watch(const char *args);
void cmd_time(const char *args);          /* time -- no args: show now */
void cmd_time_cmd(const char *args);      /* time CMD ARG... : time child */

#endif
