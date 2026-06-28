#include "dhcp.h"
#include "udp.h"
#include "ip.h"
#include "arp.h"
#include "ethernet.h"
#include "kheap.h"
#include "string.h"
#include "timer.h"
#include "sync.h"
#include "stddef.h"

static dhcp_lease_t   lease;
static net_interface_t *bound_iface;
static uint8_t         pending_options[64];
static uint8_t         pending_options_len;
static uint8_t         dhcp_configured;

static void arp_send_gratuitous(net_interface_t *iface, ipv4_addr_t ip) {
    if (!iface) return;
    arp_header_t arp;
    memset(&arp, 0, sizeof(arp));
    arp.htype = ARP_HW_ETHERNET;
    arp.ptype = ARP_PROTO_IP;
    arp.hlen = 6;
    arp.plen = 4;
    arp.opcode = ARP_OP_REQUEST;
    arp.sender_mac = iface->mac;
    arp.sender_ip = ip;
    arp.target_ip = ip;
    arp.target_mac.bytes[0] = 0xFF; arp.target_mac.bytes[1] = 0xFF;
    arp.target_mac.bytes[2] = 0xFF; arp.target_mac.bytes[3] = 0xFF;
    arp.target_mac.bytes[4] = 0xFF; arp.target_mac.bytes[5] = 0xFF;

    mac_addr_t broadcast;
    broadcast.bytes[0] = 0xFF; broadcast.bytes[1] = 0xFF;
    broadcast.bytes[2] = 0xFF; broadcast.bytes[3] = 0xFF;
    broadcast.bytes[4] = 0xFF; broadcast.bytes[5] = 0xFF;

    ethernet_send(iface, broadcast, ETH_P_ARP, &arp, sizeof(arp_header_t));
}

static uint32_t now_ms(void) { return timer_get_ticks() * 10U; }

static void dhcp_send_discover(void);
static void dhcp_send_request(void);

void dhcp_client_init(void) {
    memset(&lease, 0, sizeof(lease));
    bound_iface = NULL;
    lease.state = DHCP_STATE_IDLE;
    dhcp_configured = 0;
}

int dhcp_client_start(net_interface_t *iface) {
    if (!iface) return -1;
    bound_iface = iface;
    memset(&lease, 0, sizeof(lease));
    lease.state        = DHCP_STATE_INIT;
    lease.xid          = DHCP_XID_DEFAULT + (now_ms() & 0xFFFF);
    lease.started_ms   = now_ms();
    lease.attempts     = 0;
    dhcp_configured    = 0;
    pending_options_len = 0;
    pending_options[pending_options_len++] = DHCP_OPT_SUBNET;
    pending_options[pending_options_len++] = DHCP_OPT_ROUTER;
    pending_options[pending_options_len++] = DHCP_OPT_DNS;
    pending_options[pending_options_len++] = DHCP_OPT_LEASE;
    pending_options[pending_options_len++] = DHCP_OPT_SERVER;
    return 0;
}

void dhcp_client_stop(void) {
    lease.state = DHCP_STATE_IDLE;
    bound_iface = NULL;
}

int dhcp_client_is_bound(void) {
    return lease.state == DHCP_STATE_BOUND ||
           lease.state == DHCP_STATE_RENEWING;
}

const dhcp_lease_t *dhcp_client_get_lease(void) { return &lease; }

/* ------------------------------------------------------------------ */
/*  Sending                                                            */
/* ------------------------------------------------------------------ */

static void dhcp_send_message(uint8_t msg_type) {
    if (!bound_iface) return;
    dhcp_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.op    = DHCP_OP_REQUEST;
    pkt.htype = DHCP_HTYPE_ETHER;
    pkt.hlen  = DHCP_HLEN_ETHER;
    pkt.hops  = 0;
    pkt.xid   = lease.xid;
    pkt.secs  = (uint16_t)((now_ms() - lease.started_ms) / 1000U);
    pkt.flags = 0x8000; /* broadcast */
    pkt.ciaddr = (msg_type == DHCP_MSG_REQUEST) ? lease.offered_ip : (ipv4_addr_t){0};
    pkt.yiaddr = (ipv4_addr_t){0};
    pkt.siaddr = (ipv4_addr_t){0};
    pkt.giaddr = (ipv4_addr_t){0};
    memcpy(pkt.chaddr, bound_iface->mac.bytes, 6);
    pkt.magic_cookie = DHCP_MAGIC_COOKIE;

    uint8_t *o = pkt.options;
    *o++ = DHCP_OPT_MSGTYPE; *o++ = 1; *o++ = msg_type;
    if (msg_type == DHCP_MSG_REQUEST) {
        *o++ = DHCP_OPT_REQ_IP;   *o++ = 4;
        *o++ = (lease.offered_ip.addr >> 24) & 0xFF;
        *o++ = (lease.offered_ip.addr >> 16) & 0xFF;
        *o++ = (lease.offered_ip.addr >>  8) & 0xFF;
        *o++ = (lease.offered_ip.addr      ) & 0xFF;
        *o++ = DHCP_OPT_SERVER;   *o++ = 4;
        *o++ = (lease.server_id.addr >> 24) & 0xFF;
        *o++ = (lease.server_id.addr >> 16) & 0xFF;
        *o++ = (lease.server_id.addr >>  8) & 0xFF;
        *o++ = (lease.server_id.addr      ) & 0xFF;
    }
    *o++ = DHCP_OPT_PARAMS;  *o++ = pending_options_len;
    for (uint8_t i = 0; i < pending_options_len; i++) *o++ = pending_options[i];
    *o++ = DHCP_OPT_END;

    /* Broadcast to 255.255.255.255:67. */
    udp_send_broadcast(bound_iface, DHCP_DEFAULT_PORT, DHCP_CLIENT_PORT,
                       &pkt, sizeof(pkt));
}

static void dhcp_send_discover(void) { dhcp_send_message(DHCP_MSG_DISCOVER); }
static void dhcp_send_request(void)  { dhcp_send_message(DHCP_MSG_REQUEST); }

/* ------------------------------------------------------------------ */
/*  Parsing options                                                    */
/* ------------------------------------------------------------------ */

static const uint8_t *dhcp_find_option(const uint8_t *opt, uint32_t len, uint8_t want) {
    uint32_t i = 0;
    while (i < len) {
        uint8_t k = opt[i++];
        if (k == DHCP_OPT_PAD) continue;
        if (k == DHCP_OPT_END) return NULL;
        if (i >= len) return NULL;
        uint8_t l = opt[i++];
        if (k == want) return &opt[i - 1];   /* points to len byte */
        if (i + l > len) return NULL;
        i += l;
    }
    return NULL;
}

static ipv4_addr_t read_be32(const uint8_t *p) {
    ipv4_addr_t a;
    a.addr = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
             ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
    return a;
}

/* ------------------------------------------------------------------ */
/*  Receive                                                            */
/* ------------------------------------------------------------------ */

void dhcp_udp_recv(void *data, uint32_t length, ipv4_addr_t src_ip, uint16_t src_port) {
    (void)src_port;
    const dhcp_packet_t *pkt = (const dhcp_packet_t *)data;
    if (!pkt || length < sizeof(dhcp_packet_t)) return;

    if (lease.state == DHCP_STATE_INIT || lease.state == DHCP_STATE_BOUND) return;

    if (pkt->op != DHCP_OP_REPLY) return;
    if (pkt->xid != lease.xid) return;
    if (!bound_iface) return;
    if (pkt->hlen != 6) return;
    if (memcmp(pkt->chaddr, bound_iface->mac.bytes, 6) != 0) return;

    const uint8_t *opt = pkt->options + 4;
    uint32_t opt_len = sizeof(pkt->options) - 4;
    uint8_t msg_type = 0;
    ipv4_addr_t subnet_mask = {0};
    ipv4_addr_t router = {0};
    ipv4_addr_t dns[DHCP_MAX_DNS];
    uint8_t dns_count = 0;
    uint32_t lease_secs = 0;
    ipv4_addr_t server_id = {0};
    memset(dns, 0, sizeof(dns));

    uint32_t i = 0;
    while (i < opt_len) {
        uint8_t k = opt[i++];
        if (k == DHCP_OPT_PAD) continue;
        if (k == DHCP_OPT_END) break;
        if (i >= opt_len) break;
        uint8_t l = opt[i++];
        if (i + l > opt_len) break;
        switch (k) {
        case DHCP_OPT_MSGTYPE:
            if (l >= 1) msg_type = opt[i];
            break;
        case DHCP_OPT_SUBNET:
            if (l == 4) subnet_mask = read_be32(&opt[i]);
            break;
        case DHCP_OPT_ROUTER:
            if (l == 4) router = read_be32(&opt[i]);
            break;
        case DHCP_OPT_DNS:
            if (l >= 4 && (l % 4) == 0) {
                dns_count = l / 4;
                if (dns_count > DHCP_MAX_DNS) dns_count = DHCP_MAX_DNS;
                for (uint8_t j = 0; j < dns_count; j++)
                    dns[j] = read_be32(&opt[i + j * 4]);
            }
            break;
        case DHCP_OPT_LEASE:
            if (l == 4) {
                lease_secs = ((uint32_t)opt[i] << 24) | ((uint32_t)opt[i+1] << 16) |
                             ((uint32_t)opt[i+2] << 8) | (uint32_t)opt[i+3];
            }
            break;
        case DHCP_OPT_SERVER:
            if (l == 4) server_id = read_be32(&opt[i]);
            break;
        default:
            break;
        }
        i += l;
    }

    if (lease.state == DHCP_STATE_SELECTING) {
        if (msg_type == DHCP_MSG_OFFER) {
            lease.offered_ip = pkt->yiaddr;
            if (server_id.addr != 0) lease.server_id = server_id;
            lease.state = DHCP_STATE_REQUESTING;
            lease.retries = 0;
            dhcp_send_request();
        }
        return;
    }

    if (lease.state == DHCP_STATE_REQUESTING) {
        if (msg_type == DHCP_MSG_ACK) {
            lease.offered_ip = pkt->yiaddr;
            if (server_id.addr != 0) lease.server_id = server_id;
            if (subnet_mask.addr != 0) lease.subnet = subnet_mask;
            if (router.addr != 0) lease.router = router;
            for (uint8_t j = 0; j < dns_count; j++) lease.dns[j] = dns[j];
            lease.dns_count = dns_count;
            if (lease_secs > 0) {
                lease.lease_obtained_ms = now_ms();
                lease.lease_expire_ms = lease.lease_obtained_ms + lease_secs * 1000U;
                lease.lease_t1_ms = lease.lease_obtained_ms + (lease_secs / 2U) * 1000U;
                lease.lease_t2_ms = lease.lease_obtained_ms + (lease_secs * 7U / 8U) * 1000U;
            }
            net_set_interface_address(bound_iface, lease.offered_ip, lease.subnet, lease.router);
            arp_send_gratuitous(bound_iface, lease.offered_ip);
            dhcp_configured = 1;
            lease.state = DHCP_STATE_BOUND;
            lease.retries = 0;
        } else if (msg_type == DHCP_MSG_NAK) {
            lease.state = DHCP_STATE_INIT;
            lease.retries = 0;
            lease.attempts++;
            dhcp_configured = 0;
            if (lease.attempts < 4) dhcp_send_discover();
        }
        return;
    }
}

void dhcp_handle_packet(const dhcp_packet_t *pkt) {
    if (!pkt || pkt->op != DHCP_OP_REPLY) return;
    if (pkt->xid != lease.xid) return;
    if (pkt->hlen != 6) return;
    if (!bound_iface) return;
    if (memcmp(pkt->chaddr, bound_iface->mac.bytes, 6) != 0) return;

    const uint8_t *opt = pkt->options;
    const uint8_t *t   = dhcp_find_option(opt, sizeof(pkt->options), DHCP_OPT_MSGTYPE);
    if (!t || t[1] != 1) return;
    uint8_t msg_type = t[2];

    if (msg_type == DHCP_MSG_OFFER && lease.state == DHCP_STATE_SELECTING) {
        lease.offered_ip = pkt->yiaddr;
        const uint8_t *s = dhcp_find_option(opt, sizeof(pkt->options), DHCP_OPT_SERVER);
        if (s && s[1] == 4) lease.server_id = read_be32(&s[2]);
        /* Move to REQUESTING. */
        lease.state    = DHCP_STATE_REQUESTING;
        lease.retries  = 0;
        dhcp_send_request();
        return;
    }

    if (msg_type == DHCP_MSG_ACK) {
        lease.offered_ip = pkt->yiaddr;
        /* Update interface addresses. */
        const uint8_t *s = dhcp_find_option(opt, sizeof(pkt->options), DHCP_OPT_SERVER);
        if (s && s[1] == 4) lease.server_id = read_be32(&s[2]);
        const uint8_t *m = dhcp_find_option(opt, sizeof(pkt->options), DHCP_OPT_SUBNET);
        if (m && m[1] == 4) lease.subnet = read_be32(&m[2]);
        const uint8_t *r = dhcp_find_option(opt, sizeof(pkt->options), DHCP_OPT_ROUTER);
        if (r && r[1] == 4) lease.router = read_be32(&r[2]);
        const uint8_t *l = dhcp_find_option(opt, sizeof(pkt->options), DHCP_OPT_LEASE);
        if (l && l[1] == 4) {
            uint32_t secs = ((uint32_t)l[2] << 24) | ((uint32_t)l[3] << 16) |
                            ((uint32_t)l[4] <<  8) |  (uint32_t)l[5];
            lease.lease_obtained_ms = now_ms();
            lease.lease_expire_ms   = lease.lease_obtained_ms + secs * 1000U;
            lease.lease_t1_ms       = lease.lease_obtained_ms + (secs / 2U) * 1000U;
            lease.lease_t2_ms       = lease.lease_obtained_ms + (secs * 7U / 8U) * 1000U;
        }
        const uint8_t *d = dhcp_find_option(opt, sizeof(pkt->options), DHCP_OPT_DNS);
        if (d && d[1] >= 4 && (d[1] % 4) == 0) {
            lease.dns_count = d[1] / 4;
            if (lease.dns_count > DHCP_MAX_DNS) lease.dns_count = DHCP_MAX_DNS;
            for (uint8_t i = 0; i < lease.dns_count; i++)
                lease.dns[i] = read_be32(&d[2 + i * 4]);
        }
        /* Apply the configuration. */
        net_set_interface_address(bound_iface, lease.offered_ip, lease.subnet, lease.router);
        lease.state    = DHCP_STATE_BOUND;
        lease.retries  = 0;
        return;
    }

    if (msg_type == DHCP_MSG_NAK) {
        lease.state    = DHCP_STATE_INIT;
        lease.retries  = 0;
        lease.attempts++;
        if (lease.attempts < 4) dhcp_send_discover();
        return;
    }
}

/* ------------------------------------------------------------------ */
/*  Periodic tick                                                      */
/* ------------------------------------------------------------------ */

void dhcp_client_tick(uint32_t now) {
    if (!bound_iface || lease.state == DHCP_STATE_IDLE) return;

    if (lease.state == DHCP_STATE_INIT) {
        /* Wait ~1s before sending the first DISCOVER. */
        if ((uint32_t)(now - lease.started_ms) < 1000) return;
        lease.state = DHCP_STATE_SELECTING;
        lease.retries = 0;
        dhcp_send_discover();
        return;
    }
    if (lease.state == DHCP_STATE_SELECTING) {
        /* Retry DISCOVER every 2 seconds (up to 3 attempts). */
        if ((uint32_t)(now - lease.started_ms) > (uint32_t)(2000U * (lease.retries + 1))) {
            if (lease.retries >= 3) {
                lease.attempts++;
                lease.started_ms = now;
                lease.retries = 0;
                if (lease.attempts >= 4) lease.state = DHCP_STATE_IDLE;
                else { lease.state = DHCP_STATE_INIT; lease.started_ms = now; }
                return;
            }
            lease.retries++;
            dhcp_send_discover();
        }
        return;
    }
    if (lease.state == DHCP_STATE_REQUESTING) {
        if ((uint32_t)(now - lease.started_ms) > (uint32_t)(2000U * (lease.retries + 1))) {
            if (lease.retries >= 3) {
                lease.state = DHCP_STATE_INIT;
                lease.started_ms = now;
                lease.retries = 0;
                return;
            }
            lease.retries++;
            dhcp_send_request();
        }
        return;
    }
    if (lease.state == DHCP_STATE_BOUND) {
        if (now >= lease.lease_expire_ms) {
            lease.state = DHCP_STATE_INIT;
            lease.started_ms = now;
            lease.retries = 0;
            lease.attempts = 0;
            return;
        }
        if (now >= lease.lease_t1_ms) {
            /* Move to RENEWING and re-send REQUEST. */
            lease.state = DHCP_STATE_RENEWING;
            lease.retries = 0;
            lease.started_ms = now;
            dhcp_send_request();
        }
        return;
    }
    if (lease.state == DHCP_STATE_RENEWING) {
        if (now >= lease.lease_t2_ms) {
            /* T2 expired: rebind via broadcast. */
            lease.state = DHCP_STATE_REBINDING;
            lease.started_ms = now;
            dhcp_send_request();
            return;
        }
        if ((uint32_t)(now - lease.started_ms) > 2000) {
            lease.started_ms = now;
            lease.retries++;
            if (lease.retries > 5) {
                /* Could not renew, drop the lease. */
                lease.state = DHCP_STATE_INIT;
                lease.started_ms = now;
            } else {
                dhcp_send_request();
            }
        }
    }
}

ipv4_addr_t dhcp_get_ip(void) {
    if (dhcp_client_is_bound()) {
        return lease.offered_ip;
    }
    ipv4_addr_t zero;
    zero.addr = 0;
    return zero;
}
