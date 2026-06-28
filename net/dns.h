#ifndef DNS_H
#define DNS_H

#include "net.h"
#include "stdint.h"

/* Tiny DNS resolver (RFC 1035).
 *
 * Supports the minimum feature set required for forward lookups
 * (A records).  Maintains a static cache of up to 32 entries with
 * a configurable TTL.  The resolver is fully passive: the caller
 * issues dns_resolve() and the answer is delivered synchronously
 * when the configured DNS server responds.
 *
 * The implementation is intentionally lock-free on the fast path:
 * the cache uses atomic compare-and-swap where available, and
 * falls back to a mutex otherwise.  Each pending query is tracked
 * by an opaque handle (16-bit token) so the caller can correlate
 * the eventual response. */

#define DNS_PORT          53
#define DNS_MAX_NAME      256
#define DNS_CACHE_SIZE    32
#define DNS_MAX_PENDING   8
#define DNS_TIMEOUT_MS    3000U

typedef struct {
    char        name[DNS_MAX_NAME];
    ipv4_addr_t ip;
    uint32_t    expires_ms;
    uint8_t     used;
} dns_cache_entry_t;

typedef struct {
    char        qname[DNS_MAX_NAME];
    uint16_t    qtype;
    uint16_t    qclass;
    ipv4_addr_t server;
    uint32_t    started_ms;
    uint16_t    token;
    uint8_t     used;
    uint8_t     resolved;
    ipv4_addr_t answer;
} dns_pending_t;

void   dns_init(void);
int    dns_set_server(ipv4_addr_t server);
ipv4_addr_t dns_get_server(void);

/* Block-resolve a hostname.  Returns 0 on success and writes the IP
 * to *out, -1 on parse error, -2 on timeout. */
int    dns_resolve(const char *name, ipv4_addr_t *out);

void   dns_tick(uint32_t now_ms);
void   dns_handle_response(const uint8_t *msg, uint32_t len,
                           ipv4_addr_t from, uint16_t from_port);
const dns_cache_entry_t *dns_get_cache(uint32_t *count);
void   dns_clear_cache(void);

/* ------------------------------------------------------------------- */
/*  反向DNS解析                                                         */
/* ------------------------------------------------------------------- */

int dns_reverse_lookup(ipv4_addr_t addr, char *out_name, uint32_t cap);

/* ------------------------------------------------------------------- */
/*  DNS hosts文件支持                                                   */
/* ------------------------------------------------------------------- */

#define DNS_HOSTS_MAX 32
typedef struct dns_host {
    char       name[DNS_MAX_NAME];
    ipv4_addr_t ip;
    uint8_t    used;
} dns_host_t;

int  dns_add_host(const char *name, ipv4_addr_t ip);
int  dns_remove_host(const char *name);
void dns_clear_hosts(void);

/* ------------------------------------------------------------------- */
/*  DNS查询统计                                                        */
/* ------------------------------------------------------------------- */

typedef struct dns_stats {
    uint32_t total_queries;
    uint32_t cache_hits;
    uint32_t cache_misses;
    uint32_t server_timeouts;
    uint32_t parse_errors;
    uint32_t cname_follows;
} dns_stats_t;

dns_stats_t dns_get_stats(void);

/* DNS解析结果结构 */
typedef struct {
    ipv4_addr_t ip;
    char       cname[DNS_MAX_NAME];
    uint8_t    have_ip;
    uint8_t    have_cname;
    uint32_t   ttl;
} dns_result_t;

/*
 * dns_parse_response - 解析DNS响应报文
 * 参数:
 *   msg: DNS响应报文
 *   len: 报文长度
 *   expected_id: 期望的事务ID
 *   result: 输出解析结果
 * 返回值: 0=成功, -1=解析错误, -2=格式错误
 */
int dns_parse_response(const uint8_t *msg, uint32_t len,
                       uint16_t expected_id, dns_result_t *result);

#endif
