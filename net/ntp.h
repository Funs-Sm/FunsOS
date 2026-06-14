#ifndef NTP_H
#define NTP_H

#include "stdint.h"

/* NTP port (RFC 5905) */
#define NTP_PORT        123

/* NTP version */
#define NTP_VERSION     4

/* NTP modes */
#define NTP_MODE_RESERVED 0
#define NTP_MODE_SYMMETRIC_ACTIVE 1
#define NTP_MODE_SYMMETRIC_PASSIVE 2
#define NTP_MODE_CLIENT  3
#define NTP_MODE_SERVER  4
#define NTP_MODE_BROADCAST 5

/* Seconds between NTP epoch (1900-01-01) and Unix epoch (1970-01-01) */
#define NTP_UNIX_OFFSET  2208988800UL

/* NTP timeout in ticks (~5 seconds assuming 10 ms/tick) */
#define NTP_TIMEOUT_TICKS 500

/* NTP max retries */
#define NTP_MAX_RETRIES  3

typedef struct __attribute__((packed)) {
    uint8_t  li_vn_mode;       /* LI(2) | VN(3) | Mode(3) */
    uint8_t  stratum;          /* stratum level */
    uint8_t  poll;             /* poll interval (log2 seconds) */
    int8_t   precision;        /* precision (log2 seconds) */
    uint32_t root_delay;       /* total round-trip delay */
    uint32_t root_dispersion;  /* max error relative to primary source */
    uint32_t reference_id;     /* reference identifier */
    uint32_t ref_timestamp_sec;  /* reference timestamp seconds */
    uint32_t ref_timestamp_frac; /* reference timestamp fraction */
    uint32_t orig_timestamp_sec;  /* originate timestamp seconds */
    uint32_t orig_timestamp_frac; /* originate timestamp fraction */
    uint32_t rx_timestamp_sec;    /* receive timestamp seconds */
    uint32_t rx_timestamp_frac;   /* receive timestamp fraction */
    uint32_t tx_timestamp_sec;    /* transmit timestamp seconds */
    uint32_t tx_timestamp_frac;   /* transmit timestamp fraction */
} ntp_packet_t;

/**
 * Query an NTP server for the current time.
 *
 * @param server_ip  IP address string of the NTP server (e.g. "pool.ntp.org")
 * @param seconds    Pointer to receive the Unix-epoch seconds
 * @param fraction   Pointer to receive the fractional seconds (2^-32 units)
 * @return 0 on success, negative on error
 *   -1  invalid argument
 *   -2  DNS resolution failed
 *   -3  no network interface
 *   -4  socket creation failed
 *   -5  bind failed
 *   -6  send failed
 *   -7  timeout
 */
int ntp_get_time(const char *server_ip, uint32_t *seconds, uint32_t *fraction);

#endif
