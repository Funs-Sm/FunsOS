/*
 * kernel/cmd_ns.h - Namespace and security shell commands (PR-7, v0.9)
 *
 *   devtmpfs           show devtmpfs stats
 *   sysfs              show sysfs stats
 *   netns [list|create NAME]
 *   netfilter          show netfilter stats / drop counters
 *   seccomp [PID]      show seccomp stats / a PID's mode
 *   apparmor           show AppArmor stats
 *   keyring            show kernel keyring stats
 *   audit [on|off]     toggle / show audit subsystem
 *   sysctl [NAME=VAL]  read / write sysctl entries
 *   capsh              show capability summary
 */
#ifndef _KERNEL_CMD_NS_H
#define _KERNEL_CMD_NS_H

void cmd_devtmpfs(const char *args);
void cmd_sysfs(const char *args);
void cmd_netns(const char *args);
void cmd_netfilter(const char *args);
void cmd_seccomp(const char *args);
void cmd_apparmor(const char *args);
void cmd_keyring(const char *args);
void cmd_audit(const char *args);
void cmd_sysctl(const char *args);
void cmd_capsh(const char *args);

#endif