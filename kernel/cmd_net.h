/*
 * kernel/cmd_net.h - PR-2 iter2+iter3+iter4+iter5
 * cmd_net 模块对外接口
 */
#ifndef _KERNEL_CMD_NET_H
#define _KERNEL_CMD_NET_H

/* 网络命令实现 (从 kernel/shell.c 迁移) */
void cmd_ping(const char *ip_str);
void cmd_ifconfig(void);
void cmd_route(void);
void cmd_dns(const char *host);
void cmd_arp(void);

/* PR-2 iter5: 13 additional functions */
void cmd_wget(const char *url, const char *outfile);
void cmd_netstat(void);
void cmd_traceroute(const char *ip);
void cmd_telnet(const char *arg);
void cmd_mtr(const char *ip_str);
void cmd_nslookup(const char *host);
void cmd_dig(const char *host);
void cmd_dhcp(const char *iface_name);
void cmd_nmap(const char *target);
void cmd_lanscan(void);
void cmd_sockstat(void);
void cmd_tcpdump(const char *filter);
void cmd_nc(const char *host, const char *port_str);

/* 模块探针 */
void cmd_net_module_init(void);

#endif /* _KERNEL_CMD_NET_H */