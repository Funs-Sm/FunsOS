#ifndef DHCP_H
#define DHCP_H

#include "net.h"
#include "stdint.h"

/* Minimal DHCPv4 client (RFC 2131 / 2132).
 *
 * Implements a 4-message DISCOVER -> OFFER -> REQUEST -> ACK exchange
 * used to obtain an IPv4 address lease, default gateway, subnet mask
 * and DNS server list from a DHCP server.  Designed to be polled from
 * the periodic net_tick() callback.
 *
 * The client is fully passive: it does not start a state machine on
 * its own.  Applications trigger it by calling dhcp_client_start()
 * and the kernel drives the remaining transitions on their behalf.
 * The lease is renewed automatically at half the lease time. */

#define DHCP_MAGIC_COOKIE  0x63538263UL
#define DHCP_DEFAULT_PORT  67
#define DHCP_CLIENT_PORT   68

#define DHCP_OP_REQUEST    1
#define DHCP_OP_REPLY      2

#define DHCP_HTYPE_ETHER   1
#define DHCP_HLEN_ETHER    6
#define DHCP_HOPS          0
#define DHCP_XID_DEFAULT   0xfeedbeefUL

#define DHCP_OPT_PAD       0
#define DHCP_OPT_SUBNET    1
#define DHCP_OPT_ROUTER    3
#define DHCP_OPT_DNS       6
#define DHCP_OPT_REQ_IP    50
#define DHCP_OPT_LEASE     51
#define DHCP_OPT_MSGTYPE   53
#define DHCP_OPT_SERVER    54
#define DHCP_OPT_PARAMS    55
#define DHCP_OPT_END       255

#define DHCP_MSG_DISCOVER  1
#define DHCP_MSG_OFFER     2
#define DHCP_MSG_REQUEST   3
#define DHCP_MSG_DECLINE   4
#define DHCP_MSG_ACK       5
#define DHCP_MSG_NAK       6
#define DHCP_MSG_RELEASE   7
#define DHCP_MSG_INFORM    8

#define DHCP_STATE_INIT       0
#define DHCP_STATE_SELECTING  1
#define DHCP_STATE_REQUESTING 2
#define DHCP_STATE_BOUND      3
#define DHCP_STATE_RENEWING   4
#define DHCP_STATE_REBINDING  5
#define DHCP_STATE_IDLE       6

#define DHCP_MAX_DNS          4

typedef struct {
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    ipv4_addr_t ciaddr;
    ipv4_addr_t yiaddr;
    ipv4_addr_t siaddr;
    ipv4_addr_t giaddr;
    uint8_t  chaddr[16];
    uint8_t  sname[64];
    uint8_t  file[128];
    uint32_t magic_cookie;
    uint8_t  options[312];
} dhcp_packet_t;

typedef struct {
    uint8_t      state;
    uint32_t     xid;
    uint32_t     started_ms;
    uint32_t     lease_obtained_ms;
    uint32_t     lease_t1_ms;        /* renewal time, sec -> ms       */
    uint32_t     lease_t2_ms;        /* rebinding time                */
    uint32_t     lease_expire_ms;    /* absolute expiry in kernel time */
    ipv4_addr_t  server_id;
    ipv4_addr_t  offered_ip;
    ipv4_addr_t  subnet;
    ipv4_addr_t  router;
    ipv4_addr_t  dns[DHCP_MAX_DNS];
    uint8_t      dns_count;
    uint8_t      attempts;
    uint8_t      retries;
} dhcp_lease_t;

void dhcp_client_init(void);
int  dhcp_client_start(net_interface_t *iface);
void dhcp_client_stop(void);
void dhcp_client_tick(uint32_t now_ms);
int  dhcp_client_is_bound(void);
const dhcp_lease_t *dhcp_client_get_lease(void);
ipv4_addr_t dhcp_get_ip(void);

#endif
