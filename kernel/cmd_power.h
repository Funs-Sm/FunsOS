/*
 * kernel/cmd_power.h - Power management commands
 *   reboot, halt, shutdown, poweroff
 */
#ifndef _KERNEL_CMD_POWER_H
#define _KERNEL_CMD_POWER_H

void cmd_reboot(const char *args);
void cmd_halt(const char *args);
void cmd_shutdown(const char *args);
void cmd_poweroff(const char *args);

#endif
