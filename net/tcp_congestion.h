/* tcp_congestion.h - TCP 拥塞控制算法 */
#ifndef TCP_CONGESTION_H
#define TCP_CONGESTION_H

#include "stdint.h"
#include "stddef.h"

/* 拥塞控制算法类型 */
typedef enum {
    CONG_RENO,        /* Reno (经典: 慢启动/拥塞避免/快恢复) */
    CONG_CUBIC,       /* CUBIC (Linux默认: 三次函数窗口增长) */
    CONG_VEGAS,       /* Vegas (基于延迟: 最小排队延迟) */
    CONG_BBR,         /* BBR (Google: 带宽估计 + Bottleneck RTT) */
    CONG_MAX
} cong_algo_t;

/* 拥塞控制状态 */
typedef struct {
    /* 窗口参数 */
    uint32_t cwnd;           /* 拥塞窗口 (bytes) */
    uint32_t ssthresh;       /* 慢启动阈值 */
    uint32_t rtt_min;        /* 最小RTT (us) */
    uint32_t rtt_smoothed;   /* 平滑RTT (us) */
    uint32_t rtt_var;        /* RTT偏差 (us) */

    /* Reno/Cubic 状态 */
    int     slow_start;      /* 是否在慢启动阶段 */
    uint32_t dup_ack_count;  /* 重复ACK计数 */
    uint32_t recovery_end;   /* 快恢复结束序列号 */

    /* Cubic 参数 */
    double  w_max;           /* 上次丢包时的最大窗口 */
    double  c;               /* CUBIC常数 (0.4) */
    double  k;               /* 当前时间距上次丢包的时间偏移 */
    uint64_t epoch_start;    /* 当前epoch开始时间 */

    /* Vegas 参数 */
    uint32_t base_rtt;       /* 基础RTT (最小观测值) */
    uint32_t vegas_cwnd;     /* Vegas计算的目标窗口 */

    /* BBR 参数 */
    uint32_t bw_estimate;    /* 带宽估计 (bytes/s) */
    uint32_t btlbw;          /* 瓶颈带宽 */
    uint32_t btlrtt;         /* 瓶颈RTT */
    uint32_t probe_rtt_cnt;  /* ProbeRTT周期计数 */
    int     probing_bw;      /* 是否在探测带宽阶段 */
    int     probing_rtt;     /* 是否在探测RTT阶段 */

    /* 统计 */
    uint64_t bytes_acked;    /* 已确认字节数 */
    uint64_t bytes_sent;     /* 已发送字节数 */
    uint32_t loss_events;    /* 丢包事件计数 */
    uint32_t retransmits;    /* 重传次数 */

    /* 配置 */
    cong_algo_t algo;        /* 当前算法 */
    uint32_t mss;            /* 最大段大小 (通常1460) */
} tcp_cong_t;

/* 初始化拥塞控制 */
void tcp_cong_init(tcp_cong_t *c, cong_algo_t algo, uint32_t mss);

/* 收到 ACK 时更新 (调用一次每ACK) */
void tcp_cong_on_ack(tcp_cong_t *c, uint32_t acked_bytes);

/* 发生超时时处理 */
void tcp_cong_on_timeout(tcp_cong_t *c);

/* 收到重复ACK时处理, 返回是否应该快重传 */
int  tcp_cong_on_dupack(tcp_cong_t *c);

/* 获取当前允许发送的字节数 */
uint32_t tcp_cong_get_window(const tcp_cong_t *c);

/* 更新RTT测量 */
void tcp_cong_update_rtt(tcp_cong_t *c, uint32_t rtt_us);

/* 切换拥塞控制算法 */
void tcp_cong_set_algo(tcp_cong_t *c, cong_algo_t algo);

/* 获取算法名称字符串 */
const char *tcp_cong_algo_name(cong_algo_t algo);

/* 获取拥塞控制统计信息 */
typedef struct {
    const char *algo_name;
    uint32_t    current_cwnd;
    uint32_t    ssthresh;
    uint32_t    rtt_min_us;
    uint32_t    rtt_avg_us;
    uint32_t    loss_count;
    uint32_t    retransmit_count;
    uint64_t    total_acked;
    uint64_t    total_sent;
} cong_stats_t;

void tcp_cong_get_stats(const tcp_cong_t *c, cong_stats_t *out);

#endif /* TCP_CONGESTION_H */
