#ifndef TCP_H
#define TCP_H

#include "stdint.h"
#include "net.h"
#include "sync.h"

typedef struct pcb_t pcb_t;

#ifndef MSS
#define MSS 1460
#endif

#define TCP_MAX_WINDOW          65535U
#define TCP_INITIAL_CWND        1U
#define TCP_SLOW_START_THRESHOLD 65535U
#define TCP_RETRANS_TIMEOUT     3000U
#define TCP_TIME_WAIT_MS        2000U
#define TCP_KEEPALIVE_IDLE_MS   7200000U
#define TCP_KEEPALIVE_INTVL_MS  75000U
#define TCP_KEEPALIVE_PROBES    9U
#define TCP_ZERO_WINDOW_PROBE_MS 1000U
#define TCP_DELACK_MS           40U
#define TCP_SYN_BACKLOG_DEFAULT 16
#define TCP_MAX_RETRIES         12

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10
#define TCP_URG 0x20
#define TCP_ECE 0x40
#define TCP_CWR 0x80

#define TCP_OPT_EOL       0
#define TCP_OPT_NOP       1
#define TCP_OPT_MSS       2
#define TCP_OPT_WSCALE    3
#define TCP_OPT_SACK_OK   4
#define TCP_OPT_SACK      5
#define TCP_OPT_TIMESTAMP 8

#define TCP_STATE_CLOSED       0
#define TCP_STATE_LISTEN       1
#define TCP_STATE_SYN_SENT     2
#define TCP_STATE_SYN_RECEIVED 3
#define TCP_STATE_ESTABLISHED  4
#define TCP_STATE_FIN_WAIT1    5
#define TCP_STATE_FIN_WAIT2    6
#define TCP_STATE_CLOSE_WAIT   7
#define TCP_STATE_CLOSING      8
#define TCP_STATE_LAST_ACK     9
#define TCP_STATE_TIME_WAIT    10

#define TCP_SOCK_FLAG_BOUND        0x0001
#define TCP_SOCK_FLAG_LISTEN       0x0002
#define TCP_SOCK_FLAG_CONNECTED    0x0004
#define TCP_SOCK_FLAG_CLOSING      0x0008
#define TCP_SOCK_FLAG_NONBLOCK     0x0010
#define TCP_SOCK_FLAG_NODELAY      0x0020
#define TCP_SOCK_FLAG_KEEPALIVE    0x0040
#define TCP_SOCK_FLAG_NEEDS_ACK    0x0080
#define TCP_SOCK_FLAG_ZERO_PROBE   0x0100
#define TCP_SOCK_FLAG_PASSIVE      0x0200
#define TCP_SOCK_FLAG_RST_RCVD     0x0400
#define TCP_SOCK_FLAG_URG_PRESENT  0x0800
#define TCP_SOCK_FLAG_DEFER_ACCEPT 0x1000
#define TCP_SOCK_FLAG_QUICKACK     0x2000
#define TCP_SOCK_FLAG_FASTOPEN     0x4000

/* ---- Congestion Control Algorithms (RFC 5681 / RFC 8312) ---- */
#define TCP_CC_RENO        0
#define TCP_CC_NEWRENO     1
#define TCP_CC_CUBIC       2

/* CUBIC-specific per-connection state (RFC 8312) */
typedef struct {
    uint32_t  tcp_friendliness;   /* 1 = use TCP-friendly region */
    uint32_t  fast_convergence;   /* 1 = apply fast convergence heuristic */
    float     beta_cubic;         /* multiplicative decrease factor (default 0.7) */
    float     C;                  /* CUBIC scaling constant (default 0.4) */
    uint32_t  W_max;              /* window size just before last loss event */
    uint32_t  W_last_max;         /* W_max before the most recent decrease */
    uint32_t  epoch_start;        /* start time of current epoch (ms) */
    uint32_t  origin_point;       /* time origin = epoch_start */
    uint32_t  dMin;               /* minimum RTT observed (ms) */
    uint32_t  cwnd_cnt;           /* ACKs received since last cwnd update */
    uint32_t  K;                  /* time to grow from 0 to W_max: cubic_root(W_max * beta_cubic / C) */
    uint32_t  last_cwnd;          /* cwnd value at last ACK (for CUBIC) */
    uint32_t  tcp_cwnd;           /* TCP-friendly cwnd estimate */
} tcp_cubic_state_t;

/* Global CC statistics */
typedef struct {
    uint32_t reno_slow_starts;
    uint32_t reno_fast_retrans;
    uint32_t newreno_partial_acks;
    uint32_t newreno_recoveries;
    uint32_t cubic_epochs;
    uint32_t cubic_fast_convergences;
    uint32_t cubic_tcp_friendly_wins;
} tcp_cc_stats_t;

#define TCP_RCVBUF_SIZE  (64 * 1024)
#define TCP_SNDBUF_SIZE  (64 * 1024)
#define TCP_BACKLOG_MAX  128

typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t  data_offset_flags;
    uint16_t window_size;
    uint16_t checksum;
    uint16_t urgent_ptr;
} tcp_header_t;

typedef struct tcp_segment {
    uint32_t seq;
    uint32_t len;
    uint32_t send_ts;
    uint8_t  flags;
    uint8_t  *data;
    struct tcp_segment *next;
} tcp_segment_t;

typedef struct {
    uint32_t left;
    uint32_t right;
} sack_range_t;

typedef struct tcp_socket {
    uint32_t state;
    uint32_t flags;

    uint16_t local_port;
    uint16_t remote_port;
    ipv4_addr_t local_ip;
    ipv4_addr_t remote_ip;
    net_interface_t *iface;

    /* Sequence / acknowledgement numbers */
    uint32_t snd_una;
    uint32_t snd_nxt;
    uint32_t snd_wnd;
    uint32_t snd_wl1;
    uint32_t snd_wl2;
    uint32_t rcv_nxt;
    uint32_t rcv_wnd;
    uint32_t rcv_adv;
    uint32_t urg_ptr;

    /* Congestion control */
    uint32_t cwnd;
    uint32_t ssthresh;
    uint32_t partial_bytes_acked;
    uint32_t dup_acks;
    uint8_t  fast_recovery;

    /* Congestion control algorithm selection */
    uint8_t   cc_algo;           /* TCP_CC_RENO / TCP_CC_NEWRENO / TCP_CC_CUBIC */
    tcp_cubic_state_t cubic;     /* CUBIC state (valid when cc_algo==TCP_CC_CUBIC) */

    /* RTT estimation (RFC 6298) */
    uint32_t srtt;
    uint32_t rttvar;
    uint32_t rto;
    uint32_t rto_min;
    uint32_t rto_max;
    int      rtt_measure_seq;
    uint32_t rtt_send_time;
    int      rtt_pending;

    /* Retransmission */
    uint32_t retrans_cnt;
    uint32_t max_retries;
    uint32_t last_send_time;
    uint32_t last_ack_time;
    uint32_t time_wait_expire;
    uint32_t keepalive_time;
    uint32_t keepalive_probes;
    uint32_t zero_probe_time;

    /* Options negotiated with peer */
    uint16_t mss_peer;
    uint16_t mss_local;
    uint8_t  wscale_local;
    uint8_t  wscale_peer;
    uint8_t  sack_enabled;
    uint32_t ts_recent;
    uint32_t ts_recent_tick;
    uint8_t  ts_option;

    /* SACK cache */
    sack_range_t sack_blocks[4];
    uint8_t      sack_count;

    /* Buffers */
    uint8_t  *send_buf;
    uint32_t  send_buf_size;
    uint32_t  send_buf_head;
    uint32_t  send_buf_tail;
    uint32_t  send_buf_len;

    uint8_t  *recv_buf;
    uint32_t  recv_buf_size;
    uint32_t  recv_buf_head;
    uint32_t  recv_buf_tail;
    uint32_t  recv_buf_len;

    /* Out-of-order reassembly queue (sorted by seq) */
    tcp_segment_t *ooo_head;

    /* Retransmission queue (segments sent but not yet acked) */
    tcp_segment_t *rtx_head;
    tcp_segment_t *rtx_tail;

    /* Listen / accept queue */
    uint8_t  backlog_max;
    uint8_t  backlog_cur;
    struct tcp_socket *accept_head;
    struct tcp_socket *accept_tail;
    struct tcp_socket *next;          /* hash-bucket chain */
    struct tcp_socket *parent;
    struct tcp_socket *next_all;      /* global linked list    */

    /* Owner process / wakeup list pointer for select() */
    pcb_t   *owner;
    pcb_t   *wait_recv;
    pcb_t   *wait_accept;
    pcb_t   *wait_send;
    pcb_t   *wait_close;

    /* Per-socket spinlock (small critical sections) */
    spinlock_t lock;
    uint32_t   refcount;
} tcp_socket_t;

typedef struct {
    uint8_t kind;
    uint8_t len;
    uint8_t data[38];
} tcp_option_t;

void tcp_init(void);
tcp_socket_t *tcp_socket_create(void);
uint16_t tcp_checksum(ipv4_addr_t src_ip, ipv4_addr_t dst_ip, uint8_t proto, const void *data, uint32_t len);
void tcp_handle_state(tcp_socket_t *sock, uint8_t flags, const void *data, uint32_t len);
void tcp_retransmit_check(tcp_socket_t *sock);
void tcp_handle_icmp_error(ipv4_addr_t src, uint8_t type, uint8_t code);
void tcp_mark_error(tcp_socket_t *sock);

tcp_socket_t *tcp_connect(net_interface_t *iface, ipv4_addr_t dst, uint16_t dst_port, uint16_t src_port);
tcp_socket_t *tcp_listen(uint16_t port);
int            tcp_listen_with_backlog(tcp_socket_t *sock, int backlog);
tcp_socket_t *tcp_accept(tcp_socket_t *listener);
tcp_socket_t *tcp_accept_nonblock(tcp_socket_t *listener);
int  tcp_send(tcp_socket_t *sock, const void *data, uint32_t len);
int  tcp_recv(tcp_socket_t *sock, void *buf, uint32_t len);
int  tcp_peek(tcp_socket_t *sock, void *buf, uint32_t len);
int  tcp_send_available(tcp_socket_t *sock);
int  tcp_recv_available(tcp_socket_t *sock);
void tcp_shutdown(tcp_socket_t *sock, int how);
void tcp_set_nodelay(tcp_socket_t *sock, int on);
void tcp_set_keepalive(tcp_socket_t *sock, int on);
void tcp_set_nonblock(tcp_socket_t *sock, int on);
void tcp_receive(net_buffer_t *buf);
int  tcp_close(tcp_socket_t *sock);

void tcp_tick(uint32_t now_ms);
void tcp_set_owner(tcp_socket_t *sock, pcb_t *proc);
void tcp_wakeup(pcb_t *proc);

int  tcp_option_parse(const uint8_t *opt, uint32_t len, tcp_socket_t *sock);
int  tcp_option_build(tcp_socket_t *sock, uint8_t *out, uint32_t max_len, int syn);

uint16_t tcp_ephemeral_alloc(void);

/* Statistics and table dumps for /proc/net/tcp and friends. */
typedef struct {
    uint32_t active_opens;
    uint32_t passive_opens;
    uint32_t attempts;
    uint32_t established;
    uint32_t closes;
    uint32_t segs_sent;
    uint32_t segs_rcvd;
    uint32_t bytes_sent;
    uint32_t bytes_rcvd;
    uint32_t retransmits;
    uint32_t bad_segs;
    uint32_t resets_sent;
    uint32_t resets_rcvd;
} tcp_stats_t;

const tcp_stats_t   *tcp_get_stats(void);
const tcp_socket_t  *tcp_get_sockets(uint32_t *count);
const tcp_socket_t  *tcp_get_listeners(uint32_t *count);
int   tcp_set_mss(tcp_socket_t *sock, uint16_t mss);
int   tcp_get_mss(tcp_socket_t *sock);
void  tcp_register_stats_active(void);
void  tcp_register_stats_passive(void);
void  tcp_register_stats_close(void);
void  tcp_register_stats_attempt(void);
void  tcp_register_stats_established(void);
void  tcp_register_stats_sent(uint32_t bytes, int retrans);
void  tcp_register_stats_rcvd(uint32_t bytes);

/* State machine helpers (implemented in tcp_state.c) */
const char *tcp_state_name(uint32_t state);
int  tcp_state_is_connecting(uint32_t state);
int  tcp_state_is_connected(uint32_t state);
int  tcp_state_is_closing(uint32_t state);
int  tcp_state_is_listen(uint32_t state);
int  tcp_state_is_closed(uint32_t state);
void tcp_state_log_transition(tcp_socket_t *sock, uint32_t from, uint32_t to);
int  tcp_describe_segment(tcp_header_t *hdr, char *out, uint32_t out_size);

/* ---- Congestion Control API ---- */
void  tcp_cc_set_algo(tcp_socket_t *sock, uint8_t algo);
uint8_t tcp_cc_get_algo(tcp_socket_t *sock);
const char *tcp_cc_get_name(uint8_t algo);
int tcp_cc_init_cubic(tcp_socket_t *sock);
void tcp_cc_on_ack(tcp_socket_t *sock, uint32_t acked_bytes);
void tcp_cc_on_loss(tcp_socket_t *sock);
void tcp_cc_on_rto(tcp_socket_t *sock);
void tcp_cc_on_dup_ack(tcp_socket_t *sock);
const tcp_cc_stats_t *tcp_cc_get_stats(void);

#endif
