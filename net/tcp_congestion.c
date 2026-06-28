/* tcp_congestion.c - TCP 拥塞控制算法实现
 *
 * 实现四种拥塞控制算法:
 *   - Reno/NewReno: 经典慢启动/拥塞避免/快恢复 (RFC5681, RFC6582)
 *   - CUBIC: Linux默认, 三次函数窗口增长 (RFC8312)
 *   - Vegas: 基于延迟, 最小排队延迟
 *   - BBR: Google, 带宽估计 + Bottleneck RTT
 */

#include "tcp_congestion.h"
#include "string.h"
#include "stdlib.h"

/* ========== 内部辅助函数 ========== */

/* 立方根近似计算 (Newton-Raphson迭代) */
static double cbrt_approx(double x)
{
    if (x == 0.0) return 0.0;
    if (x < 0.0) return -cbrt_approx(-x);

    double r = 1.0;
    int i;
    for (i = 0; i < 20; i++) {
        double r2 = r * r;
        r = (2.0 * r + x / (r2)) / 3.0;
    }
    return r;
}

/* 获取当前时间 (简化: 使用单调计数器) */
static uint64_t cong_get_time_ms(void)
{
    static uint64_t fake_time = 0;
    return fake_time++;
}

/* CUBIC 常量 */
#define CUBIC_C       0.4
#define CUBIC_BETA    0.7

/* Vegas 参数 */
#define VEGAS_ALPHA   1
#define VEGAS_GAMMA   3
#define VEGAS_BASE_RTT_DEFAULT 100000

/* BBR 参数 */
#define BBR_HIGH_GAIN    2.885
#define BBR_DRAIN_GAIN   0.35
#define BBR_PROBE_BW_GAIN 1.25
#define BBR_PROBE_RTT_LEN 200000
#define BBR_MIN_RTT_FILTER_LEN 10000

/* BBR 状态机 */
typedef enum {
    BBR_STARTUP,
    BBR_DRAIN,
    BBR_PROBE_BW,
    BBR_PROBE_RTT
} bbr_state_t;

typedef struct {
    bbr_state_t state;
    uint64_t    round_start;
    uint32_t    next_round_delivered;
    int         full_bw_reached;
    uint32_t    full_bw;
    uint32_t    full_bw_count;
    uint32_t    cycle_stamp;
    uint32_t    cycle_index;
    uint32_t    min_rtt_stamp;
    uint32_t    min_rtt_us;
} bbr_extra_t;

static bbr_extra_t bbr_state;

/* 返回min(cwnd, flight_size)用于ssthresh计算 */
static uint32_t get_loss_window(const tcp_cong_t *c)
{
    uint32_t w = c->cwnd;
    if (c->flight_size > 0 && c->flight_size < w)
        w = c->flight_size;
    return w;
}

/* ========== 公共API实现 ========== */

void tcp_cong_init(tcp_cong_t *c, cong_algo_t algo, uint32_t mss)
{
    memset(c, 0, sizeof(tcp_cong_t));

    c->algo = algo;
    c->mss = mss ? mss : 1460;

    /* RFC6928: 初始窗口IW = min(4*MSS, max(2*MSS, 4380bytes))
     * 简化使用10*MSS或更保守的2*MSS，这里使用更标准的RFC5681 IW=2*MSS */
    c->cwnd = 2 * c->mss;

    /* 初始慢启动阈值: 无穷大 */
    c->ssthresh = 0xFFFFFFFFu;

    /* 慢启动状态 */
    c->slow_start = 1;
    c->in_fast_recovery = 0;
    c->high_seq = 0;
    c->flight_size = 0;

    /* RTT初始化 */
    c->rtt_min = 0xFFFFFFFFu;
    c->rtt_smoothed = 0;
    c->rtt_var = 0;

    /* Cubic参数初始化 */
    c->w_max = 0.0;
    c->c = CUBIC_C;
    c->k = 0.0;
    c->epoch_start = 0;

    /* Vegas参数初始化 */
    c->base_rtt = VEGAS_BASE_RTT_DEFAULT;
    c->vegas_cwnd = 0;

    /* BBR参数初始化 */
    c->bw_estimate = 0;
    c->btlbw = 0;
    c->btlrtt = 0;
    c->probe_rtt_cnt = 0;
    c->probing_bw = 0;
    c->probing_rtt = 0;

    /* 统计清零 */
    c->bytes_acked = 0;
    c->bytes_sent = 0;
    c->loss_events = 0;
    c->retransmits = 0;

    if (algo == CONG_BBR) {
        memset(&bbr_state, 0, sizeof(bbr_state));
        bbr_state.state = BBR_STARTUP;
        bbr_state.min_rtt_us = 0xFFFFFFFFu;
    }
}

/*
 * Reno 算法慢启动/拥塞避免:
 * - 慢启动: 每收到一个新ACK, cwnd += MSS (指数增长: 每RTT翻倍)
 * - 拥塞避免: cwnd += MSS*MSS/cwnd (线性增长: 每RTT增加MSS)
 */
static void reno_cong_avoid(tcp_cong_t *c, uint32_t acked_bytes)
{
    if (c->in_fast_recovery)
        return;

    if (c->cwnd < c->ssthresh) {
        /* 慢启动: 指数增长, 每收到一个ACK增加1个MSS */
        c->cwnd += c->mss;
        c->slow_start = 1;
    } else {
        /* 拥塞避免: 线性增长 */
        uint32_t increment = (c->mss * c->mss) / c->cwnd;
        if (increment < 1)
            increment = 1;
        c->cwnd += increment;
        c->slow_start = 0;
    }

    /* 安全上限 */
    if (c->cwnd > 65535 * c->mss)
        c->cwnd = 65535 * c->mss;
}

/*
 * CUBIC 算法:
 * - 慢启动阶段: 同Reno指数增长 (cwnd < ssthresh)
 * - 拥塞避免阶段: 使用三次函数 W(t) = C*(t-K)^3 + W_max
 */
static void cubic_on_ack(tcp_cong_t *c, uint32_t acked_bytes)
{
    /* 慢启动阶段: 同Reno指数增长 */
    if (c->cwnd < c->ssthresh && c->epoch_start == 0) {
        c->cwnd += c->mss;
        c->slow_start = 1;
        if (c->cwnd > 65535 * c->mss)
            c->cwnd = 65535 * c->mss;
        return;
    }
    c->slow_start = 0;

    /* 如果是epoch开始，记录开始时间 */
    if (c->epoch_start == 0) {
        c->epoch_start = cong_get_time_ms();
        if (c->cwnd < c->w_max) {
            double arg = c->w_max * (1.0 - CUBIC_BETA) / CUBIC_C;
            c->k = cbrt_approx(arg);
        } else {
            c->k = 0.0;
        }
    }

    /* 获取当前时间距epoch开始的偏移 (秒) */
    uint64_t now = cong_get_time_ms();
    double t = ((double)(now - c->epoch_start)) / 1000.0;

    /* 计算CUBIC目标窗口 */
    double t_minus_k = t - c->k;
    double w_cubic = c->c * (t_minus_k * t_minus_k * t_minus_k) + c->w_max;

    if (w_cubic < (double)c->mss)
        w_cubic = (double)c->mss;

    /* TCP友好窗口 (Reno增长速率) */
    double w_tcp = c->cwnd + (double)(c->mss * c->mss) / (double)c->cwnd;

    double w_target = (w_cubic > w_tcp) ? w_cubic : w_tcp;

    /* 平滑增长 */
    double current_wnd = (double)c->cwnd;
    if (w_target > current_wnd) {
        uint32_t inc = (uint32_t)(w_target - current_wnd);
        if (inc > c->mss) inc = c->mss;
        c->cwnd += inc;
    } else if (w_target < current_wnd) {
        if (c->cwnd > 2 * c->mss)
            c->cwnd -= c->mss;
    }

    if (c->cwnd > 65535 * c->mss)
        c->cwnd = 65535 * c->mss;
}

static void vegas_on_ack(tcp_cong_t *c, uint32_t acked_bytes)
{
    if (c->rtt_smoothed == 0 || c->base_rtt == 0)
        return;

    uint32_t rtt_diff = 0;
    if (c->rtt_smoothed > c->base_rtt) {
        rtt_diff = c->rtt_smoothed - c->base_rtt;
    }

    uint64_t expected_rate = ((uint64_t)c->cwnd * 1000000) / c->base_rtt;
    uint64_t actual_rate = (c->rtt_smoothed > 0) ?
                          ((uint64_t)c->cwnd * 1000000) / c->rtt_smoothed : 0;

    int32_t diff = (int32_t)(expected_rate - actual_rate);
    diff = diff / 1000;

    if (c->cwnd < c->ssthresh) {
        c->cwnd += c->mss;
    } else {
        if (diff < VEGAS_ALPHA) {
            c->cwnd += c->mss;
        } else if (diff > VEGAS_GAMMA) {
            if (c->cwnd > 2 * c->mss) {
                c->cwnd -= c->mss;
            }
        }
    }

    c->vegas_cwnd = c->cwnd;
}

static void bbr_on_ack(tcp_cong_t *c, uint32_t acked_bytes)
{
    uint64_t now = cong_get_time_ms();

    if (acked_bytes > 0 && c->rtt_smoothed > 0) {
        uint32_t rtt_sec = (c->rtt_smoothed + 500000) / 1000000;
        if (rtt_sec > 0) {
            uint32_t sample_bw = acked_bytes / rtt_sec;
            if (sample_bw > c->bw_estimate) {
                c->bw_estimate = sample_bw;
            }
        }
    }

    switch (bbr_state.state) {
    case BBR_STARTUP:
        c->cwnd = (uint32_t)((double)c->cwnd * BBR_HIGH_GAIN);
        if (!bbr_state.full_bw_reached) {
            if (c->bw_estimate >= bbr_state.full_bw) {
                bbr_state.full_bw_count++;
                if (bbr_state.full_bw_count >= 3) {
                    bbr_state.full_bw_reached = 1;
                    bbr_state.full_bw = c->bw_estimate;
                    bbr_state.state = BBR_DRAIN;
                }
            } else {
                bbr_state.full_bw = c->bw_estimate;
                bbr_state.full_bw_count = 0;
            }
        }
        break;

    case BBR_DRAIN:
        c->cwnd = (uint32_t)((double)c->cwnd * BBR_DRAIN_GAIN);
        if (c->btlrtt > 0 && (double)c->cwnd <= (double)bbr_state.full_bw * (double)c->btlrtt / 1000000.0) {
            bbr_state.state = BBR_PROBE_BW;
            bbr_state.cycle_index = 0;
        }
        break;

    case BBR_PROBE_BW: {
        static const double gains[] = {1.25, 0.75, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
        double gain = gains[bbr_state.cycle_index % 8];
        uint32_t bdp = (c->btlbw > 0 && c->btlrtt > 0) ?
                       (c->btlbw * c->btlrtt) / 1000000 : 4 * c->mss;
        if (bdp < 2 * c->mss)
            bdp = 2 * c->mss;
        c->cwnd = (uint32_t)((double)bdp * gain);
        c->probing_bw = (gain > 1.0) ? 1 : 0;
        if (++bbr_state.cycle_index % 100 == 0) {
            bbr_state.cycle_index = (bbr_state.cycle_index + 1) % 8;
        }
        break;
    }
    case BBR_PROBE_RTT: {
        uint32_t min_cwnd = 4 * c->mss;
        if (c->cwnd > min_cwnd) {
            c->cwnd = min_cwnd;
        }
        c->probe_rtt_cnt++;
        if (c->probe_rtt_cnt * c->rtt_smoothed > BBR_PROBE_RTT_LEN) {
            bbr_state.state = BBR_PROBE_BW;
            c->probing_rtt = 0;
            c->probe_rtt_cnt = 0;
        }
        break;
    }
    }

    if (c->rtt_min != 0xFFFFFFFFu &&
        (c->btlrtt == 0 || c->rtt_min < c->btlrtt)) {
        c->btlrtt = c->rtt_min;
        bbr_state.min_rtt_us = c->rtt_min;
        bbr_state.min_rtt_stamp = now;
    }

    if (c->bw_estimate > c->btlbw) {
        c->btlbw = c->bw_estimate;
    }

    if (c->cwnd < 2 * c->mss)
        c->cwnd = 2 * c->mss;
    if (c->cwnd > 65535 * c->mss)
        c->cwnd = 65535 * c->mss;
}

void tcp_cong_on_ack(tcp_cong_t *c, uint32_t acked_bytes)
{
    if (!c || acked_bytes == 0)
        return;

    c->bytes_acked += acked_bytes;

    switch (c->algo) {
    case CONG_RENO:
        reno_cong_avoid(c, acked_bytes);
        break;
    case CONG_CUBIC:
        cubic_on_ack(c, acked_bytes);
        break;
    case CONG_VEGAS:
        vegas_on_ack(c, acked_bytes);
        break;
    case CONG_BBR:
        bbr_on_ack(c, acked_bytes);
        break;
    default:
        reno_cong_avoid(c, acked_bytes);
        break;
    }
}

/*
 * tcp_cong_on_timeout - RTO超时处理 (RFC5681)
 * - ssthresh = max(FlightSize/2, 2*MSS)
 * - cwnd = 1*MSS (进入慢启动)
 */
void tcp_cong_on_timeout(tcp_cong_t *c)
{
    if (!c)
        return;

    c->loss_events++;
    c->retransmits++;

    uint32_t prev_cwnd = c->cwnd;
    uint32_t loss_win = get_loss_window(c);

    /* ssthresh = max(min(cwnd, FlightSize)/2, 2*MSS) */
    c->ssthresh = loss_win / 2;
    if (c->ssthresh < 2 * c->mss)
        c->ssthresh = 2 * c->mss;

    /* cwnd 降为 1 个MSS (RFC5681) */
    c->cwnd = c->mss;

    c->slow_start = 1;
    c->in_fast_recovery = 0;
    c->dup_ack_count = 0;
    c->recovery_end = 0;
    c->high_seq = 0;
    c->epoch_start = 0;

    if (c->algo == CONG_CUBIC) {
        c->w_max = (double)prev_cwnd;
        double arg = c->w_max * (1.0 - CUBIC_BETA) / CUBIC_C;
        c->k = cbrt_approx(arg);
        c->epoch_start = 0;
    }

    if (c->algo == CONG_VEGAS) {
        c->base_rtt = VEGAS_BASE_RTT_DEFAULT;
    }

    if (c->algo == CONG_BBR) {
        bbr_state.state = BBR_STARTUP;
        bbr_state.full_bw_reached = 0;
        bbr_state.full_bw = 0;
        bbr_state.full_bw_count = 0;
        c->bw_estimate = 0;
    }
}

/*
 * tcp_reno_fast_retransmit - Reno快重传/快恢复 (RFC5681)
 * 收到3个重复ACK时调用:
 * - ssthresh = max(FlightSize/2, 2*MSS)
 * - cwnd = ssthresh + 3*MSS (考虑已在途的3个包)
 * - 进入快恢复
 */
int tcp_reno_fast_retransmit(tcp_cong_t *c)
{
    if (!c)
        return 0;

    uint32_t loss_win = get_loss_window(c);
    uint32_t prev_cwnd = c->cwnd;

    /* ssthresh = max(min(cwnd, FlightSize)/2, 2*MSS) */
    c->ssthresh = loss_win / 2;
    if (c->ssthresh < 2 * c->mss)
        c->ssthresh = 2 * c->mss;

    /* cwnd = ssthresh + 3*MSS */
    c->cwnd = c->ssthresh + 3 * c->mss;

    c->slow_start = 0;
    c->in_fast_recovery = 1;
    c->loss_events++;

    if (c->algo == CONG_CUBIC) {
        c->w_max = (double)prev_cwnd;
        double arg = c->w_max * (1.0 - CUBIC_BETA) / CUBIC_C;
        c->k = cbrt_approx(arg);
        c->epoch_start = cong_get_time_ms();
    }

    return 1;
}

/*
 * tcp_newreno_process_ack - NewReno部分ACK处理 (RFC6582)
 * 在快恢复阶段收到ACK时调用:
 * - 完整ACK (ack >= high_seq): 退出快恢复, cwnd=ssthresh
 * - 部分ACK (ack < high_seq): 重传下一个段, cwnd临时膨胀
 * 返回值: 1=需要重传下一个段, 0=不需要
 */
int tcp_newreno_process_ack(tcp_cong_t *c, uint32_t acked_bytes, uint32_t ack_seq)
{
    if (!c)
        return 0;

    if (!c->in_fast_recovery)
        return 0;

    /* 完整ACK: ACK覆盖了进入恢复时的所有数据 */
    if (ack_seq >= c->high_seq) {
        c->cwnd = c->ssthresh;
        c->in_fast_recovery = 0;
        c->dup_ack_count = 0;
        c->recovery_end = 0;
        return 0;
    }

    /* 部分ACK: 只确认了部分数据, 说明有多个包丢失, 需要继续重传 */
    /* 部分ACK确认了新数据，cwnd可以减1 MSS */
    if (c->cwnd > c->mss)
        c->cwnd -= c->mss;

    /* 每个部分ACK允许发送一个新包 */
    c->cwnd += c->mss;

    return 1;
}

int tcp_cong_on_dupack(tcp_cong_t *c)
{
    if (!c)
        return 0;

    c->dup_ack_count++;

    /* 第3个重复ACK触发快重传 */
    if (c->dup_ack_count == 3) {
        int ret = 0;
        switch (c->algo) {
        case CONG_RENO:
        case CONG_CUBIC:
        case CONG_VEGAS:
            if (c->algo == CONG_VEGAS) {
                uint32_t loss_win = get_loss_window(c);
                c->ssthresh = (loss_win * 3) / 4;
                if (c->ssthresh < 2 * c->mss)
                    c->ssthresh = 2 * c->mss;
                c->cwnd = c->ssthresh + 3 * c->mss;
                c->slow_start = 0;
                c->in_fast_recovery = 1;
                c->loss_events++;
            } else {
                tcp_reno_fast_retransmit(c);
            }
            ret = 1;
            break;

        case CONG_BBR:
            c->loss_events++;
            ret = 1;
            break;

        default:
            tcp_reno_fast_retransmit(c);
            ret = 1;
            break;
        }

        c->recovery_end = c->bytes_acked;
        return ret;
    }

    /* 快恢复期间额外的重复ACK: 每个dupack允许cwnd增加1 MSS */
    if (c->dup_ack_count > 3 && c->in_fast_recovery) {
        c->cwnd += c->mss;
    }

    return 0;
}

uint32_t tcp_cong_get_window(const tcp_cong_t *c)
{
    if (!c)
        return 0;

    if (c->cwnd < c->mss)
        return c->mss;

    return c->cwnd;
}

void tcp_cong_update_flight_size(tcp_cong_t *c, uint32_t flight_size)
{
    if (!c)
        return;
    c->flight_size = flight_size;
}

void tcp_cong_set_high_seq(tcp_cong_t *c, uint32_t seq)
{
    if (!c)
        return;
    c->high_seq = seq;
}

void tcp_cong_update_rtt(tcp_cong_t *c, uint32_t rtt_us)
{
    if (!c || rtt_us == 0)
        return;

    if (rtt_us < c->rtt_min) {
        c->rtt_min = rtt_us;
    }

    if (c->algo == CONG_VEGAS) {
        if (rtt_us < c->base_rtt) {
            c->base_rtt = rtt_us;
        }
    }

    if (c->rtt_smoothed == 0) {
        c->rtt_smoothed = rtt_us;
        c->rtt_var = rtt_us / 2;
    } else {
        /* Jacobson/Karels算法 - 修复bug: 先计算绝对差值 */
        int32_t delta = (int32_t)rtt_us - (int32_t)c->rtt_smoothed;
        if (delta < 0) delta = -delta;

        /* SRTT = 7/8 * SRTT + 1/8 * RTT */
        int32_t srtt_delta = (int32_t)rtt_us - (int32_t)c->rtt_smoothed;
        c->rtt_smoothed += (srtt_delta >> 3);
        if (c->rtt_smoothed < 1)
            c->rtt_smoothed = 1;

        /* RTTVAR = 3/4 * RTTVAR + 1/4 * |SRTT - RTT| */
        int32_t abs_delta = srtt_delta < 0 ? -srtt_delta : srtt_delta;
        c->rtt_var += ((uint32_t)abs_delta - c->rtt_var) >> 2;
    }

    if (c->algo == CONG_BBR) {
        if (rtt_us < bbr_state.min_rtt_us) {
            bbr_state.min_rtt_us = rtt_us;
            bbr_state.min_rtt_stamp = cong_get_time_ms();
        }
    }
}

void tcp_cong_set_algo(tcp_cong_t *c, cong_algo_t algo)
{
    if (!c || algo >= CONG_MAX)
        return;

    uint32_t saved_cwnd = c->cwnd;
    uint32_t saved_ssthresh = c->ssthresh;

    c->algo = algo;

    switch (algo) {
    case CONG_RENO:
        c->slow_start = (saved_cwnd < saved_ssthresh) ? 1 : 0;
        c->in_fast_recovery = 0;
        c->epoch_start = 0;
        break;

    case CONG_CUBIC:
        c->w_max = (double)saved_cwnd;
        c->c = CUBIC_C;
        c->k = 0.0;
        c->epoch_start = 0;
        c->slow_start = (saved_cwnd < saved_ssthresh) ? 1 : 0;
        c->in_fast_recovery = 0;
        break;

    case CONG_VEGAS:
        c->base_rtt = c->rtt_min != 0xFFFFFFFFu ? c->rtt_min : VEGAS_BASE_RTT_DEFAULT;
        c->vegas_cwnd = saved_cwnd;
        c->slow_start = (saved_cwnd < saved_ssthresh) ? 1 : 0;
        c->in_fast_recovery = 0;
        break;

    case CONG_BBR:
        memset(&bbr_state, 0, sizeof(bbr_state));
        bbr_state.state = BBR_STARTUP;
        bbr_state.full_bw = saved_cwnd;
        bbr_state.min_rtt_us = c->rtt_min;
        c->in_fast_recovery = 0;
        break;

    default:
        break;
    }
}

const char *tcp_cong_algo_name(cong_algo_t algo)
{
    static const char *names[] = {
        "Reno",
        "CUBIC",
        "Vegas",
        "BBR",
        "Unknown"
    };

    if (algo < CONG_MAX)
        return names[algo];
    return names[CONG_MAX];
}

void tcp_cong_get_stats(const tcp_cong_t *c, cong_stats_t *out)
{
    if (!c || !out)
        return;

    out->algo_name = tcp_cong_algo_name(c->algo);
    out->current_cwnd = c->cwnd;
    out->ssthresh = c->ssthresh;
    out->rtt_min_us = c->rtt_min;
    out->rtt_avg_us = c->rtt_smoothed;
    out->loss_count = c->loss_events;
    out->retransmit_count = c->retransmits;
    out->total_acked = c->bytes_acked;
    out->total_sent = c->bytes_sent;
}
