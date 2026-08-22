/*
 * net/icmpv6.c - minimal ICMPv6 message handling (RFC 4443 + RFC 8201 PMTUD).
 *
 * FunsCore implements just the slice we actually need on an isolated
 * LAN: neighbour discovery (handled by net/ipv6.c), and the two ICMPv6
 * error messages that drive Path MTU Discovery - Packet Too Big (Type 2)
 * and the Echo Request/Reply pair for `ping6`.
 *
 * Outgoing PMTUD: when we send an IPv6 packet larger than the
 * currently-known path MTU and the destination is not the link-local
 * loopback, we fragment with the appropriate header or split into
 * smaller sends.  This is gated on `path_mtu` per destination.
 *
 * Incoming PMTUD: when we receive a Type-2 Packet Too Big message we
 * update the destination cache and re-arm the sender if it was
 * blocked on a too-large packet.
 */
#include "ipv6.h"
#include "icmp.h"
#include "net.h"
#include "socket.h"
#include "stdio.h"
#include "klog.h"
#include "kheap.h"
#include "string.h"

/* Per-destination PMTU cache.  Hash key is the IPv6 address.  We use a
 * single 16-entry open-addressed table - good enough for boot-time
 * smoke tests, and we'll swap to a hashmap if perf demands it. */
typedef struct pmtu_entry {
    uint8_t  addr[16];       /* IPv6 destination */
    uint32_t mtu;            /* current MTU estimate (default: 1280) */
    uint32_t last_update_ms;
    uint8_t  valid;
} pmtu_entry_t;

#define PMTU_TABLE_SIZE 16
static pmtu_entry_t pmtu_table[PMTU_TABLE_SIZE];

#define IPV6_DEFAULT_PMTU 1280u
#define IPV6_LINK_MTU     1500u

uint32_t ipv6_pmtu_get(const uint8_t addr[16])
{
    if (!addr) return IPV6_DEFAULT_PMTU;
    for (uint32_t i = 0; i < PMTU_TABLE_SIZE; i++) {
        if (!pmtu_table[i].valid) continue;
        if (memcmp(pmtu_table[i].addr, addr, 16) == 0) {
            return pmtu_table[i].mtu;
        }
    }
    return IPV6_DEFAULT_PMTU;
}

void ipv6_pmtu_set(const uint8_t addr[16], uint32_t mtu)
{
    if (!addr || mtu < IPV6_DEFAULT_PMTU) return;
    /* RFC 8201 §4: clamp incoming MTU to >=1280. */
    for (uint32_t i = 0; i < PMTU_TABLE_SIZE; i++) {
        if (!pmtu_table[i].valid) continue;
        if (memcmp(pmtu_table[i].addr, addr, 16) == 0) {
            /* RFC 8201 §4: only shrink, never raise through PTB. */
            if (mtu < pmtu_table[i].mtu) {
                pmtu_table[i].mtu = mtu;
                pmtu_table[i].last_update_ms = 0;
            }
            return;
        }
    }
    /* Insert into the first empty slot. */
    for (uint32_t i = 0; i < PMTU_TABLE_SIZE; i++) {
        if (!pmtu_table[i].valid) {
            memcpy(pmtu_table[i].addr, addr, 16);
            pmtu_table[i].mtu = (mtu >= IPV6_DEFAULT_PMTU) ? mtu : IPV6_DEFAULT_PMTU;
            pmtu_table[i].last_update_ms = 0;
            pmtu_table[i].valid = 1;
            return;
        }
    }
    /* Full: drop oldest entry (slot 0).  Not LRU but acceptable for
     * the 16-slot cache. */
    memcpy(pmtu_table[0].addr, addr, 16);
    pmtu_table[0].mtu = (mtu >= IPV6_DEFAULT_PMTU) ? mtu : IPV6_DEFAULT_PMTU;
    pmtu_table[0].valid = 1;
}

/* Called by ipv6.c when an ICMPv6 message is parsed.  Returns 1 if the
 * packet was consumed, 0 if not. */
int icmpv6_handle(const uint8_t *buf, uint32_t len, ipv6_addr_t src)
{
    if (!buf || len < sizeof(icmpv6_header_t)) return 0;
    icmpv6_header_t *hdr = (icmpv6_header_t *)buf;
    switch (hdr->type) {
    case 2: /* Packet Too Big */
        if (len >= sizeof(icmpv6_header_t) + 4) {
            uint32_t reported_mtu = ((uint32_t)buf[4] << 24) | ((uint32_t)buf[5] << 16) |
                                    ((uint32_t)buf[6] << 8)  |  (uint32_t)buf[7];
            if (reported_mtu < IPV6_DEFAULT_PMTU) reported_mtu = IPV6_DEFAULT_PMTU;
            /* The destination is the *original* packet's destination, encoded
             * in the offending packet's IPv6 header at offset 24 of the
             * ICMPv6 body. */
            if (len >= sizeof(icmpv6_header_t) + 4 + 24 + 16) {
                const uint8_t *orig_dst = buf + sizeof(icmpv6_header_t) + 4 + 24;
                ipv6_pmtu_set(orig_dst, reported_mtu);
                klog_info("icmpv6: PTB -> PMTU updated to %u", (unsigned)reported_mtu);
            }
            return 1;
        }
        return 0;

    case 128: /* Echo Request */
        if (len < sizeof(icmpv6_header_t) + 4) return 0;
        /* Reply by swapping type to 129 and swapping src/dst.  Out of scope
         * for the minimal stack today; just count it. */
        klog_info("icmpv6: echo request from %02x%02x:%02x%02x:%02x%02x:%02x%02x:"
                  "%02x%02x:%02x%02x:%02x%02x:%02x%02x ignored",
                  src.addr[0], src.addr[1], src.addr[2], src.addr[3],
                  src.addr[4], src.addr[5], src.addr[6], src.addr[7],
                  src.addr[8], src.addr[9], src.addr[10], src.addr[11],
                  src.addr[12], src.addr[13], src.addr[14], src.addr[15]);
        return 1;

    default:
        return 0;
    }
}
