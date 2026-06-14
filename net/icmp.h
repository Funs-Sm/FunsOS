#ifndef ICMP_H
#define ICMP_H

#include "stdint.h"
#include "net.h"

/* ICMP types and codes - RFC 792 */
#define ICMP_TYPE_ECHO_REPLY         0
#define ICMP_TYPE_DEST_UNREACH       3
#define ICMP_TYPE_SOURCE_QUENCH      4
#define ICMP_TYPE_REDIRECT           5
#define ICMP_TYPE_ECHO_REQUEST       8
#define ICMP_TYPE_TIME_EXCEEDED      11
#define ICMP_TYPE_PARAM_PROBLEM      12
#define ICMP_TYPE_TIMESTAMP_REQUEST  13
#define ICMP_TYPE_TIMESTAMP_REPLY    14
#define ICMP_TYPE_ADDRESS_MASK_REQ   17
#define ICMP_TYPE_ADDRESS_MASK_REPLY 18

/* Destination Unreachable codes (RFC 792) */
#define ICMP_CODE_NET_UNREACH        0
#define ICMP_CODE_HOST_UNREACH       1
#define ICMP_CODE_PROTO_UNREACH      2
#define ICMP_CODE_PORT_UNREACH       3
#define ICMP_CODE_FRAG_NEEDED        4
#define ICMP_CODE_SR_FAILED          5
#define ICMP_CODE_NET_UNKNOWN        6
#define ICMP_CODE_HOST_UNKNOWN       7
#define ICMP_CODE_HOST_ISOLATED      8
#define ICMP_CODE_NET_PROHIB         9
#define ICMP_CODE_HOST_PROHIB        10
#define ICMP_CODE_NET_TOS            11
#define ICMP_CODE_HOST_TOS           12

/* Time Exceeded codes */
#define ICMP_CODE_TTL_EXCEEDED       0
#define ICMP_CODE_FRAG_REASSEMBLY     1

/* Redirect codes */
#define ICMP_CODE_REDIR_NET          0
#define ICMP_CODE_REDIR_HOST         1
#define ICMP_CODE_REDIR_TOS_NET      2
#define ICMP_CODE_REDIR_TOS_HOST     3

typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t identifier;
    uint16_t sequence;
} icmp_header_t;

/* ICMP statistics (RFC 1213 + extensions) */
typedef struct {
    uint32_t echo_requests_sent;
    uint32_t echo_requests_rcvd;
    uint32_t echo_replies_sent;
    uint32_t echo_replies_rcvd;
    uint32_t dest_unreach_sent;
    uint32_t dest_unreach_rcvd;
    uint32_t time_exceeded_sent;
    uint32_t time_exceeded_rcvd;
    uint32_t redirect_sent;
    uint32_t redirect_rcvd;
    uint32_t param_problem_sent;
    uint32_t param_problem_rcvd;
    uint32_t source_quench_sent;
    uint32_t source_quench_rcvd;
    uint32_t timestamp_sent;
    uint32_t timestamp_rcvd;
    uint32_t address_mask_sent;
    uint32_t address_mask_rcvd;
    uint32_t checksum_errors;
    uint32_t bad_type;
} icmp_stats_t;

void icmp_init(void);
int  icmp_send_echo_request(net_interface_t *iface, ipv4_addr_t dst,
                            uint16_t id, uint16_t seq);
int  icmp_send_echo_reply(net_interface_t *iface, ipv4_addr_t dst,
                          uint16_t id, uint16_t seq);

/* Error-type helpers - used by TCP, UDP, IP to signal failures. */
int  icmp_send_dest_unreach(net_interface_t *iface, ipv4_addr_t dst,
                            uint8_t code, net_buffer_t *orig);
int  icmp_send_time_exceeded(net_interface_t *iface, ipv4_addr_t dst,
                             uint8_t code, net_buffer_t *orig);
int  icmp_send_redirect(net_interface_t *iface, ipv4_addr_t dst,
                        uint8_t code, ipv4_addr_t gw, net_buffer_t *orig);
int  icmp_send_param_problem(net_interface_t *iface, ipv4_addr_t dst,
                             uint8_t pointer, net_buffer_t *orig);

void icmp_receive(net_buffer_t *buf);
const icmp_stats_t *icmp_get_stats(void);

/* Ping result tracking */
#define ICMP_PING_MAX_RESULTS 64

typedef struct {
    ipv4_addr_t dst;
    uint16_t identifier;
    uint16_t sequence;
    uint32_t send_time_ms;
    uint32_t recv_time_ms;
    uint32_t rtt_ms;
    uint8_t  received;
} icmp_ping_result_t;

int  icmp_ping(net_interface_t *iface, ipv4_addr_t dst, uint16_t id, uint16_t seq);
uint32_t icmp_get_ping_results(const icmp_ping_result_t **results);

#endif
