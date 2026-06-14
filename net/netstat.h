#ifndef NETSTAT_H
#define NETSTAT_H

#include "stdint.h"

/* Format /proc/net/{tcp,udp,arp,dev,if_inet6,route} snapshots into
 * out[].  Returns the number of bytes written, or -1 if `kind` is
 * unknown.  `cap` is the capacity of the output buffer. */
enum {
    NETSTAT_TCP      = 1,
    NETSTAT_UDP      = 2,
    NETSTAT_ARP      = 3,
    NETSTAT_DEV      = 4,
    NETSTAT_ROUTE    = 5,
    NETSTAT_IF_INET6 = 6,
    NETSTAT_SOCKSTAT = 7
};

int  netstat_format(int kind, char *out, uint32_t cap);
int  netstat_register_procfs(void);

#endif
