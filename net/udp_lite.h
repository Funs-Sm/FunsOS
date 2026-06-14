#ifndef UDP_LITE_H
#define UDP_LITE_H

#include "net.h"
#include "stdint.h"

/* UDP-Lite (RFC 3828) - lightweight UDP variant for delay-sensitive
 * applications that prefer partial checksums over silent packet
 * discard.  UDP-Lite replaces the UDP length field with a "Coverage
 * Length" (number of bytes protected by the checksum) and accepts
 * packets with a Checksum Coverage of 0 (no checksum).
 *
 * This module implements both sender and receiver paths.  Sender:
 *   - udp_lite_send() builds a UDP-Lite header and passes the
 *     datagram to ip_send() with protocol 136.
 *   - If the coverage is 0, the checksum is set to 0 (and the
 *     IPv4 pseudo-header checksum is still included for IPv4).
 *
 * Receiver:
 *   - udp_lite_receive() is called from ip_dispatch_payload() when
 *     a packet with protocol 136 arrives.  The packet is delivered
 *     to the matching UDP socket (port-based) just like UDP, but
 *     the "length" field is replaced by the coverage so the user
 *     receives the full datagram and is expected to know how to
 *     handle the unprotected trailer. */

#define IP_PROTO_UDP_LITE 136
#define UDP_LITE_COVERAGE_NONE 0

uint16_t udp_lite_checksum(ipv4_addr_t src, ipv4_addr_t dst,
                           const void *data, uint32_t len,
                           uint16_t coverage);
int      udp_lite_send(net_interface_t *iface, ipv4_addr_t dst,
                        uint16_t dst_port, uint16_t src_port,
                        const void *data, uint32_t len, uint16_t coverage);
void     udp_lite_receive(net_buffer_t *buf);

#endif
