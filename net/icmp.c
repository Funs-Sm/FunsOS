#include "icmp.h"
#include "ip.h"
#include "kheap.h"
#include "string.h"
#include "sync.h"
#include "timer.h"

/* ------------------------------------------------------------------ */
/*  Statistics                                                         */
/* ------------------------------------------------------------------ */

static icmp_stats_t stats;

/* Ping result tracking */
static icmp_ping_result_t ping_results[ICMP_PING_MAX_RESULTS];
static uint32_t ping_result_count = 0;
static uint32_t ping_result_index = 0;

/* Traceroute callback */
static icmp_traceroute_cb traceroute_callback = NULL;

static uint16_t htons(uint16_t v) {
    return ((v >> 8) & 0xFF) | ((v & 0xFF) << 8);
}

void icmp_init(void) {
    memset(&stats, 0, sizeof(stats));
    memset(ping_results, 0, sizeof(ping_results));
    ping_result_count = 0;
    ping_result_index = 0;
    traceroute_callback = NULL;
}

const icmp_stats_t *icmp_get_stats(void) { return &stats; }

void icmp_set_traceroute_callback(icmp_traceroute_cb cb) {
    traceroute_callback = cb;
}

/* ------------------------------------------------------------------ */
/*  Ping interface                                                     */
/* ------------------------------------------------------------------ */

int icmp_ping(net_interface_t *iface, ipv4_addr_t dst, uint16_t id, uint16_t seq) {
    uint32_t idx = ping_result_index % ICMP_PING_MAX_RESULTS;
    ping_results[idx].dst = dst;
    ping_results[idx].identifier = id;
    ping_results[idx].sequence = seq;
    ping_results[idx].send_time_ms = timer_get_ticks() * 10U;
    ping_results[idx].recv_time_ms = 0;
    ping_results[idx].rtt_ms = 0;
    ping_results[idx].received = 0;
    ping_result_index++;
    if (ping_result_count < ICMP_PING_MAX_RESULTS)
        ping_result_count++;

    return icmp_send_echo_request(iface, dst, id, seq);
}

uint32_t icmp_get_ping_results(const icmp_ping_result_t **results) {
    if (results) *results = ping_results;
    return ping_result_count;
}

/* ------------------------------------------------------------------ */
/*  Checksum                                                           */
/* ------------------------------------------------------------------ */

static uint16_t icmp_compute_checksum(const void *data, uint32_t len) {
    return ip_checksum(data, len);
}

/* ------------------------------------------------------------------ */
/*  Generic send                                                       */
/* ------------------------------------------------------------------ */

static int icmp_send_internal(net_interface_t *iface, ipv4_addr_t dst,
                              uint8_t *msg, uint32_t len) {
    uint16_t csum = icmp_compute_checksum(msg, len);
    msg[2] = (uint8_t)(csum & 0xFF);
    msg[3] = (uint8_t)((csum >> 8) & 0xFF);
    int r = ip_send(iface, dst, IP_PROTO_ICMP, msg, len);
    kfree(msg);
    return r;
}

/* ------------------------------------------------------------------ */
/*  Echo request / reply                                               */
/* ------------------------------------------------------------------ */

int icmp_send_echo_request(net_interface_t *iface, ipv4_addr_t dst,
                           uint16_t id, uint16_t seq) {
    uint32_t total = sizeof(icmp_header_t) + 56;
    uint8_t *packet = (uint8_t *)kmalloc(total);
    if (!packet) return -1;

    icmp_header_t *hdr = (icmp_header_t *)packet;
    hdr->type = ICMP_TYPE_ECHO_REQUEST;
    hdr->code = 0;
    hdr->checksum = 0;
    hdr->identifier = htons(id);
    hdr->sequence = htons(seq);
    memset(packet + sizeof(icmp_header_t), 0xAB, 56);
    int r = icmp_send_internal(iface, dst, packet, total);
    if (r == 0) stats.echo_requests_sent++;
    return r;
}

int icmp_send_echo_reply(net_interface_t *iface, ipv4_addr_t dst,
                         uint16_t id, uint16_t seq,
                         const void *data, uint32_t data_len) {
    uint32_t payload_len = data_len > 0 ? data_len : 56;
    uint32_t total = sizeof(icmp_header_t) + payload_len;
    uint8_t *packet = (uint8_t *)kmalloc(total);
    if (!packet) return -1;

    icmp_header_t *hdr = (icmp_header_t *)packet;
    hdr->type = ICMP_TYPE_ECHO_REPLY;
    hdr->code = 0;
    hdr->checksum = 0;
    hdr->identifier = htons(id);
    hdr->sequence = htons(seq);

    if (data && data_len > 0) {
        memcpy(packet + sizeof(icmp_header_t), data, data_len);
    } else {
        memset(packet + sizeof(icmp_header_t), 0xAB, 56);
    }

    int r = icmp_send_internal(iface, dst, packet, total);
    if (r == 0) stats.echo_replies_sent++;
    return r;
}

/* ------------------------------------------------------------------ */
/*  Error messages (RFC 792)                                           */
/* ------------------------------------------------------------------ */

static int icmp_send_error(net_interface_t *iface, ipv4_addr_t dst,
                           uint8_t type, uint8_t code, uint32_t extra,
                           net_buffer_t *orig) {
    if (!orig) return -1;
    if (orig->len < 20) return -1;

    uint32_t copy = orig->len < 28 ? (uint32_t)orig->len : 28;
    uint32_t total = sizeof(icmp_header_t) + 4 + copy;
    uint8_t *msg = (uint8_t *)kmalloc(total);
    if (!msg) return -1;

    memset(msg, 0, total);
    msg[0] = type;
    msg[1] = code;

    if (type == ICMP_TYPE_DEST_UNREACH && code == ICMP_CODE_FRAG_NEEDED) {
        msg[6] = (uint8_t)((extra >> 8) & 0xFF);
        msg[7] = (uint8_t)(extra & 0xFF);
    } else if (type == ICMP_TYPE_PARAM_PROBLEM) {
        msg[4] = (uint8_t)(extra & 0xFF);
    }

    memcpy(msg + 8, orig->data + orig->offset, copy);

    int r = icmp_send_internal(iface, dst, msg, total);
    if (r == 0) {
        switch (type) {
        case ICMP_TYPE_DEST_UNREACH:  stats.dest_unreach_sent++;  break;
        case ICMP_TYPE_TIME_EXCEEDED: stats.time_exceeded_sent++; break;
        case ICMP_TYPE_REDIRECT:      stats.redirect_sent++;      break;
        case ICMP_TYPE_PARAM_PROBLEM: stats.param_problem_sent++; break;
        case ICMP_TYPE_SOURCE_QUENCH: stats.source_quench_sent++; break;
        }
    }
    return r;
}

int icmp_send_dest_unreach(net_interface_t *iface, ipv4_addr_t dst,
                           uint8_t code, net_buffer_t *orig) {
    return icmp_send_error(iface, dst, ICMP_TYPE_DEST_UNREACH, code, 0, orig);
}

int icmp_send_time_exceeded(net_interface_t *iface, ipv4_addr_t dst,
                            uint8_t code, net_buffer_t *orig) {
    return icmp_send_error(iface, dst, ICMP_TYPE_TIME_EXCEEDED, code, 0, orig);
}

int icmp_send_redirect(net_interface_t *iface, ipv4_addr_t dst,
                       uint8_t code, ipv4_addr_t gw, net_buffer_t *orig) {
    if (!orig) return -1;
    if (orig->len < 20) return -1;

    uint32_t copy = orig->len < 28 ? (uint32_t)orig->len : 28;
    uint32_t total = sizeof(icmp_header_t) + 4 + copy;
    uint8_t *msg = (uint8_t *)kmalloc(total);
    if (!msg) return -1;

    memset(msg, 0, total);
    msg[0] = ICMP_TYPE_REDIRECT;
    msg[1] = code;
    msg[4] = (uint8_t)((gw.addr >> 24) & 0xFF);
    msg[5] = (uint8_t)((gw.addr >> 16) & 0xFF);
    msg[6] = (uint8_t)((gw.addr >>  8) & 0xFF);
    msg[7] = (uint8_t)(gw.addr & 0xFF);
    memcpy(msg + 8, orig->data + orig->offset, copy);

    int r = icmp_send_internal(iface, dst, msg, total);
    if (r == 0) stats.redirect_sent++;
    return r;
}

int icmp_send_param_problem(net_interface_t *iface, ipv4_addr_t dst,
                            uint8_t pointer, net_buffer_t *orig) {
    return icmp_send_error(iface, dst, ICMP_TYPE_PARAM_PROBLEM, 0,
                           (uint32_t)pointer, orig);
}

/* ------------------------------------------------------------------ */
/*  Receive                                                            */
/* ------------------------------------------------------------------ */

static uint16_t ntohs(uint16_t v) {
    return ((v >> 8) & 0xFF) | ((v & 0xFF) << 8);
}

void icmp_receive(net_buffer_t *buf) {
    if (!buf || buf->len < (int)sizeof(icmp_header_t)) return;

    uint8_t *icmp_base = buf->data + buf->offset;
    icmp_header_t *hdr = (icmp_header_t *)icmp_base;

    uint16_t recv_cksum = hdr->checksum;
    hdr->checksum = 0;
    uint16_t calc_cksum = icmp_compute_checksum(hdr, (uint32_t)buf->len);
    hdr->checksum = recv_cksum;

    if (recv_cksum != calc_cksum) {
        stats.checksum_errors++;
        return;
    }

    if (buf->offset < 20) return;
    ip_header_t *ip_hdr = (ip_header_t *)(buf->data + buf->offset - 20);

    switch (hdr->type) {
    case ICMP_TYPE_ECHO_REQUEST: {
        stats.echo_requests_rcvd++;
        uint32_t payload_len = (uint32_t)buf->len - sizeof(icmp_header_t);
        const void *payload = icmp_base + sizeof(icmp_header_t);
        uint16_t id = ntohs(hdr->identifier);
        uint16_t seq = ntohs(hdr->sequence);
        icmp_send_echo_reply(buf->iface, ip_hdr->src_ip, id, seq,
                             payload, payload_len);
        break;
    }
    case ICMP_TYPE_ECHO_REPLY: {
        stats.echo_replies_rcvd++;
        uint32_t now = timer_get_ticks() * 10U;
        uint16_t id = ntohs(hdr->identifier);
        uint16_t seq = ntohs(hdr->sequence);
        for (uint32_t k = 0; k < ICMP_PING_MAX_RESULTS; k++) {
            if (!ping_results[k].received &&
                ping_results[k].identifier == id &&
                ping_results[k].sequence == seq) {
                ping_results[k].recv_time_ms = now;
                ping_results[k].rtt_ms = now - ping_results[k].send_time_ms;
                ping_results[k].received = 1;
                break;
            }
        }
        break;
    }
    case ICMP_TYPE_DEST_UNREACH:
        stats.dest_unreach_rcvd++;
        {
            extern void tcp_handle_icmp_error(ipv4_addr_t src, uint8_t type, uint8_t code);
            tcp_handle_icmp_error(ip_hdr->src_ip, hdr->type, hdr->code);
        }
        break;
    case ICMP_TYPE_TIME_EXCEEDED: {
        stats.time_exceeded_rcvd++;
        if (traceroute_callback && buf->len >= (int)(sizeof(icmp_header_t) + 4 + 28)) {
            uint8_t *embedded_ip = icmp_base + 8;
            ip_header_t *inner_ip = (ip_header_t *)embedded_ip;
            if (inner_ip->protocol == IP_PROTO_UDP || inner_ip->protocol == IP_PROTO_ICMP) {
                uint16_t inner_sport = 0;
                if (buf->len >= (int)(sizeof(icmp_header_t) + 4 + 20 + 4)) {
                    uint8_t *inner_transport = embedded_ip + 20;
                    inner_sport = ((uint16_t)inner_transport[0] << 8) | inner_transport[1];
                }
                traceroute_callback(ip_hdr->src_ip, hdr->code,
                                    inner_ip->dst_ip, inner_sport);
            }
        }
        {
            extern void tcp_handle_icmp_error(ipv4_addr_t src, uint8_t type, uint8_t code);
            tcp_handle_icmp_error(ip_hdr->src_ip, hdr->type, hdr->code);
        }
        break;
    }
    case ICMP_TYPE_REDIRECT:
        stats.redirect_rcvd++;
        break;
    case ICMP_TYPE_PARAM_PROBLEM:
        stats.param_problem_rcvd++;
        break;
    case ICMP_TYPE_SOURCE_QUENCH:
        stats.source_quench_rcvd++;
        break;
    case ICMP_TYPE_TIMESTAMP_REQUEST:
        stats.timestamp_rcvd++;
        break;
    case ICMP_TYPE_TIMESTAMP_REPLY:
        stats.timestamp_rcvd++;
        break;
    case ICMP_TYPE_ADDRESS_MASK_REQ:
        stats.address_mask_rcvd++;
        break;
    case ICMP_TYPE_ADDRESS_MASK_REPLY:
        stats.address_mask_rcvd++;
        break;
    default:
        stats.bad_type++;
        break;
    }
}
