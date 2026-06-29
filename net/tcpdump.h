#ifndef TCPDUMP_H
#define TCPDUMP_H

#include "stdint.h"

#define TCPDUMP_MAX_PACKET  1518
#define TCPDUMP_MAX_HISTORY 64

#define TCPDUMP_OPT_VERBOSE    0x01
#define TCPDUMP_OPT_HEX        0x02
#define TCPDUMP_OPT_ASCII      0x04
#define TCPDUMP_OPT_ETH        0x08
#define TCPDUMP_OPT_IP         0x10
#define TCPDUMP_OPT_TCP        0x20
#define TCPDUMP_OPT_UDP        0x40
#define TCPDUMP_OPT_ICMP       0x80

typedef struct {
    uint32_t timestamp_ms;
    uint16_t length;
    uint8_t  data[TCPDUMP_MAX_PACKET];
} tcpdump_packet_t;

typedef struct {
    uint32_t options;
    uint32_t max_packets;
    uint32_t snaplen;
    char     filter_expr[256];
    uint16_t port_filter;
    uint8_t  proto_filter;
} tcpdump_config_t;

typedef struct {
    tcpdump_packet_t packets[TCPDUMP_MAX_HISTORY];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint32_t total_captured;
    uint32_t total_dropped;
    int      running;
    tcpdump_config_t config;
} tcpdump_t;

void tcpdump_init(void);
tcpdump_t *tcpdump_create(void);
void tcpdump_destroy(tcpdump_t *td);

int  tcpdump_start(tcpdump_t *td, tcpdump_config_t *config);
void tcpdump_stop(tcpdump_t *td);

int  tcpdump_capture(tcpdump_t *td, const void *data, uint32_t len);
tcpdump_packet_t *tcpdump_next(tcpdump_t *td);

int  tcpdump_format_packet(tcpdump_packet_t *pkt, tcpdump_config_t *cfg,
                            char *out, uint32_t cap);
int  tcpdump_format_hex(const uint8_t *data, uint32_t len, char *out, uint32_t cap);

int  tcpdump_parse_ethernet(const uint8_t *data, uint32_t len, char *out, uint32_t cap);
int  tcpdump_parse_ip(const uint8_t *data, uint32_t len, char *out, uint32_t cap);
int  tcpdump_parse_tcp(const uint8_t *data, uint32_t len, char *out, uint32_t cap);
int  tcpdump_parse_udp(const uint8_t *data, uint32_t len, char *out, uint32_t cap);
int  tcpdump_parse_icmp(const uint8_t *data, uint32_t len, char *out, uint32_t cap);

uint32_t tcpdump_get_count(tcpdump_t *td);
void tcpdump_clear(tcpdump_t *td);

#endif
