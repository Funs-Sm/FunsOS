/* tcp_congestion.c - TCP 拥塞控制算法实现
 *
 * 实现四种拥塞控制算法:
 *   - Reno: 经典慢启动/拥塞避免/快恢复
 *   - CUBIC: Linux默认, 三次函数窗口增长
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
    /* 在实际系统中应使用高精度时钟 */
    /* 这里返回模拟时间 */
    static uint64_t fake_time = 0;
    return fake_time++;
}

/* CUBIC 常量 */
#define CUBIC_C       0.4
#define CUBIC_BETA    0.7

/* Vegas 参数 */
#define VEGAS_ALPHA   1     /* 队列延迟下限 (包) */
#define VEGAS_GAMMA   3     /* 队列延迟上限 (包) */
#define VEGAS_BASE_RTT_DEFAULT 100000  /* 默认基础RTT 100ms */

/* BBR 参数 */
#define BBR_HIGH_GAIN    2.885   /* Startup阶段增益 */
#define BBR_DRAIN_GAIN   0.35    /* Drain阶段增益 */
#define BBR_PROBE_BW_GAIN 1.25   /* ProbeBW增益 */
#define BBR_PROBE_RTT_LEN 200000 /* ProbeRTT周期长度 (us) */
#define BBR_MIN_RTT_FILTER_LEN 10000  /* MinRTT滤波窗口 (us) */

/* BBR 状态机 */
typedef enum {
    BBR_STARTUP,
    BBR_DRAIN,
    BBR_PROBE_BW,
    BBR_PROBE_RTT
} bbr_state_t;

/* 扩展状态 (用于BBR内部) */
typedef struct {
    bbr_state_t state;          /* 当前BBR状态 */
    uint64_t    round_start;    /* 当前RTT轮起始时间 */
    uint32_t    next_round_delivered; /* 下一轮的已交付字节 */
    int         full_bw_reached;      /* 是否达到满带宽 */
    uint32_t    full_bw;              /* 满带宽估计 */
    uint32_t    full_bw_count;        /* 满带宽计数器 */
    uint32_t    cycle_stamp;          /* 探测周期时间戳 */
    uint32_t    cycle_index;          /* 探测周期索引 (0-7) */
    uint32_t    min_rtt_stamp;        /* 最小RTT更新时间戳 */
    uint32_t    min_rtt_us;           /* 过滤后的最小RTT */
} bbr_extra_t;

static bbr_extra_t bbr_state;  /* 全局BBR扩展状态 */

/* ========== 公共API实现 ========== */

/*
 * tcp_cong_init - 初始化拥塞控制状态
 */
void tcp_cong_init(tcp_cong_t *c, cong_algo_t algo, uint32_t mss)
{
    memset(c, 0, sizeof(tcp_cong_t));

    c->algo = algo;
    c->mss = mss ? mss : 1460;

    /* 初始窗口: 10个MSS (RFC6928) 或 2个MSS (保守) */
    c->cwnd = 10 * c->mss;

    /* 初始慢启动阈值: 无穷大 (表示未设置) */
    c->ssthresh = 0xFFFFFFFFu;

    /* 处于慢启动状态 */
    c->slow_start = 1;

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

    /* 初始化BBR扩展状态 */
    if (algo == CONG_BBR) {
        memset(&bbr_state, 0, sizeof(bbr_state));
        bbr_state.state = BBR_STARTUP;
        bbr_state.min_rtt_us = 0xFFFFFFFFu;
    }
}

/*
 * Reno 算法: 收到ACK时的处理
 * - 慢启动: cwnd += MSS (每ACK)
 * - 拥塞避免: cwnd += MSS*MSS/cwnd (每ACK)
 */
static void reno_on_ack(tcp_cong_t *c, uint32_t acked_bytes)
{
    if (c->slow_start) {
        /* 慢启动阶段: 每收到ACK, cwnd增加MSS */
        c->cwnd += c->mss;

        /* 检查是否超过慢启动阈值 */
        if (c->cwnd >= c->ssthresh) {
            c->slow_start = 0;
        }

        /* 安全上限: 不超过接收窗口 (假设65535字节) */
        if (c->cwnd > 65535 * c->mss) {
            c->cwnd = 65535 * c->mss;
        }
    } else {
        /* 拥塞避免阶段: 加法增长
         * 每RTT增加一个MSS, 分散到每个ACK上:
         * 每ACK增加: MSS * MSS / cwnd
         */
        uint32_t increment = (c->mss * c->mss) / c->cwnd;
        if (increment < 1)
            increment = 1;
        c->cwnd += increment;
    }

    /* 快恢复结束后重置dup_ack计数 */
    if (c->recovery_end && c->bytes_acked >= c->recovery_end) {
        c->dup_ack_count = 0;
        c->recovery_end = 0;
    }
}

/*
 * CUBIC 算法: 收到ACK时的处理
 * W(t) = C*(t-K)^3 + W_max
 * K = cubic_root(W_max * (1-beta_cubic) / C)
 */
static void cubic_on_ack(tcp_cong_t *c, uint32_t acked_bytes)
{
    /* 获取当前时间距epoch开始的偏移 (秒) */
    uint64_t now = cong_get_time_ms();
    double t = ((double)(now - c->epoch_start)) / 1000.0;  /* 转换为秒 */

    /*
     * 计算CUBIC目标窗口:
     * W(t) = C*(t-K)^3 + W_max
     * 其中 K = cubic_root(W_max * (1-beta) / C)
     */
    double t_minus_k = t - c->k;
    double w_cubic = c->c * (t_minus_k * t_minus_k * t_minus_k) + c->w_max;

    /* 目标窗口不能小于初始值 (MSS) */
    if (w_cubic < (double)c->mss)
        w_cubic = (double)c->mss;

    /* 更新cwnd向目标收敛 */
    /* 使用TCP友好的增长速率作为下界 */
    double w_tcp = c->cwnd + (double)(c->mss * c->mss) / (double)c->cwnd;

    /* 取CUBIC目标和TCP友好值的较大者 */
    double w_target = (w_cubic > w_tcp) ? w_cubic : w_tcp;

    /* 平滑过渡到目标窗口 */
    double current_wnd = (double)c->cwnd;
    if (w_target > current_wnd) {
        /* 向上增长: 使用CUBIC增量 */
        c->cwnd = (uint32_t)w_target;
    } else {
        /* 向下调整: 使用更保守的步进 */
        c->cwnd -= (c->mss * c->mss) / c->cwnd;
        if (c->cwnd < 2 * c->mss)
            c->cwnd = 2 * c->mss;
    }

    /* 更新最大窗口记录 */
    if ((uint32_t)w_target > (uint32_t)c->w_max && c->w_max == 0.0) {
        c->w_max = w_target;
    }

    /* 安全上限 */
    if (c->cwnd > 65535 * c->mss) {
        c->cwnd = 65535 * c->mss;
    }
}

/*
 * Vegas 算法: 收到ACK时的处理
 * 基于排队延迟的拥塞控制
 */
static void vegas_on_ack(tcp_cong_t *c, uint32_t acked_bytes)
{
    if (c->rtt_smoothed == 0 || c->base_rtt == 0)
        return;  /* RTT数据不足 */

    /*
     * Vegas核心思想:
     * actual = cwnd / rtt (实际吞吐率)
     * expected = cwnd / base_rtt (期望吞吐率, 无排队时)
     * diff = expected - actual (队列中的包数)
     *
     * if diff < alpha -> 增加 cwnd (网络空闲)
     * if diff > gamma -> 减少 cwnd (网络拥塞)
     * else -> 保持不变
     */

    /* 当前RTT与最小RTT的差值 (反映排队延迟) */
    uint32_t rtt_diff = 0;
    if (c->rtt_smoothed > c->base_rtt) {
        rtt_diff = c->rtt_smoothed - c->base_rtt;
    }

    /* 计算当前队列中的包数 (diff) */
    /* diff = (cwnd / base_rtt) * rtt_diff 近似 */
    uint64_t expected_rate = ((uint64_t)c->cwnd * 1000000) / c->base_rtt;
    uint64_t actual_rate = (c->rtt_smoothed > 0) ?
                          ((uint64_t)c->cwnd * 1000000) / c->rtt_smoothed : 0;

    int32_t diff = (int32_t)(expected_rate - actual_rate);
    diff = diff / 1000;  /* 缩放到合理范围 */

    /* 根据diff调整窗口 */
    if (diff < VEGAS_ALPHA) {
        /* 队列太短, 网络空闲, 可以增加发送速率 */
        c->cwnd += c->mss;
    } else if (diff > VEGAS_GAMMA) {
        /* 队列太长, 发生拥塞, 减少发送速率 */
        if (c->cwnd > 2 * c->mss) {
            c->cwnd -= c->mss;
        }
    }
    /* else: diff在[alpha, gamma]范围内, 保持cwnd不变 */

    /* 更新Vegas目标窗口 */
    c->vegas_cwnd = c->cwnd;
}

/*
 * BBR 算法: 收到ACK时的处理
 * 基于带宽估计和最小RTT的拥塞控制
 */
static void bbr_on_ack(tcp_cong_t *c, uint32_t acked_bytes)
{
    uint64_t now = cong_get_time_ms();

    /* 更新带宽估计 (delivery rate sampling) */
    if (acked_bytes > 0) {
        /* 简化的带宽估计: 使用平滑RTT计算 */
        uint32_t rtt_sec = (c->rtt_smoothed + 500000) / 1000000;  /* 四舍五入到秒 */
        if (rtt_sec > 0) {
            uint32_t sample_bw = acked_bytes / rtt_sec;
            /* 取最大带宽估计 */
            if (sample_bw > c->bw_estimate) {
                c->bw_estimate = sample_bw;
            }
        }
    }

    /* BBR状态机处理 */
    switch (bbr_state.state) {
    case BBR_STARTUP:
        /*
         * Startup阶段: 类似慢启动, 但使用高增益
         * cwnd按指数增长直到检测到带宽饱和
         */
        c->cwnd = (uint32_t)((double)c->cwnd * BBR_HIGH_GAIN);

        /* 检测是否达到满带宽 */
        if (!bbr_state.full_bw_reached) {
            if (c->bw_estimate >= bbr_state.full_bw) {
                bbr_state.full_bw_count++;
                if (bbr_state.full_bw_count >= 3) {
                    /* 连续3次达到相同带宽, 认为已饱和 */
                    bbr_state.full_bw_reached = 1;
                    bbr_state.full_bw = c->bw_estimate;
                    bbr_state.state = BBR_DRAIN;
                }
            } else {
                bbr_state.full_bw = c->bw_estimate;
                bbr_state.full_bw_count = 0;
            }
        } else {
            bbr_state.state = BBR_DRAIN;
        }
        break;

    case BBR_DRAIN:
        /*
         * Drain阶段: 排空Startup期间积累的队列
         * 使用低增益使cwnd下降
         */
        c->cwnd = (uint32_t)((double)c->cwnd * BBR_DRAIN_GAIN);

        /* 当cwnd降到合适水平后进入ProbeBW */
        if ((double)c->cwnd <= (double)bbr_state.full_bw * (double)c->btlrtt / 1000000.0) {
            bbr_state.state = BBR_PROBE_BW;
            bbr_state.cycle_index = 0;
        }
        break;

    case BBR_PROBE_BW:
        /*
         * ProbeBW阶段: 周期性探测更多带宽
         * 8个周期的增益模式: [1.25, 0.75, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0]
         */
        {
            static const double gains[] = {1.25, 0.75, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
            double gain = gains[bbr_state.cycle_index % 8];

            /* BDP = bandwidth * min_rtt */
            uint32_t bdp = (c->btlbw * c->btlrtt) / 1000000;
            if (bdp < 2 * c->mss)
                bdp = 2 * c->mss;

            c->cwnd = (uint32_t)((double)bdp * gain);
            c->probing_bw = (gain > 1.0) ? 1 : 0;

            /* 周期推进 (简化的周期切换逻辑) */
            if (++bbr_state.cycle_index % 100 == 0) {  /* 每100个ACK切换一次 */
                bbr_state.cycle_index = (bbr_state.cycle_index + 1) % 8;
            }
        }

        /* 定期进入ProbeRTT */
        if (now - bbr_state.min_rtt_stamp > BBR_MIN_RTT_FILTER_LEN) {
            bbr_state.state = BBR_PROBE_RTT;
            c->probing_rtt = 1;
        }
        break;

    case BBR_PROBE_RTT:
        /*
         * ProbeRTT阶段: 测量真实最小RTT
         * 将cwnd降低到最小值以排空所有队列
         */
        {
            uint32_t min_cwnd = 4 * c->mss;
            if (c->cwnd > min_cwnd) {
                c->cwnd = min_cwnd;
            }
            c->probe_rtt_cnt++;

            /* ProbeRTT持续约200ms后退出 */
            if (c->probe_rtt_cnt * c->rtt_smoothed > BBR_PROBE_RTT_LEN) {
                bbr_state.state = BBR_PROBE_BW;
                c->probing_rtt = 0;
                c->probe_rtt_cnt = 0;
            }
        }
        break;
    }

    /* 更新瓶颈RTT估计 */
    if (c->rtt_min != 0xFFFFFFFFu &&
        (c->btlrtt == 0 || c->rtt_min < c->btlrtt)) {
        c->btlrtt = c->rtt_min;
        bbr_state.min_rtt_us = c->rtt_min;
        bbr_state.min_rtt_stamp = now;
    }

    /* 更新瓶颈带宽 */
    if (c->bw_estimate > c->btlbw) {
        c->btlbw = c->bw_estimate;
    }

    /* 安全限制 */
    if (c->cwnd < 2 * c->mss)
        c->cwnd = 2 * c->mss;
    if (c->cwnd > 65535 * c->mss)
        c->cwnd = 65535 * c->mss;
}

/*
 * tcp_cong_on_ack - 收到ACK时更新拥塞窗口
 */
void tcp_cong_on_ack(tcp_cong_t *c, uint32_t acked_bytes)
{
    if (!c || acked_bytes == 0)
        return;

    /* 更新统计 */
    c->bytes_acked += acked_bytes;

    /* 根据选择的算法调用对应的处理函数 */
    switch (c->algo) {
    case CONG_RENO:
        reno_on_ack(c, acked_bytes);
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
        /* 默认使用Reno */
        reno_on_ack(c, acked_bytes);
        break;
    }
}

/*
 * tcp_cong_on_timeout - 超时处理 (RTO事件)
 * 所有算法的共同行为: 大幅降低cwnd并进入慢启动
 */
void tcp_cong_on_timeout(tcp_cong_t *c)
{
    if (!c)
        return;

    /* 记录丢包事件 */
    c->loss_events++;
    c->retransmits++;

    /* 保存当前窗口用于Cubic计算 */
    uint32_t prev_cwnd = c->cwnd;

    /* 超时时: ssthresh = max(cwnd/2, 2*MSS) */
    c->ssthresh = c->cwnd / 2;
    if (c->ssthresh < 2 * c->mss)
        c->ssthresh = 2 * c->mss;

    /* cwnd 重置为 1 个MSS (RFC5681) */
    c->cwnd = c->mss;

    /* 进入慢启动 */
    c->slow_start = 1;

    /* 重置重复ACK计数 */
    c->dup_ack_count = 0;
    c->recovery_end = 0;

    /* Cubic特定处理: 记录w_max, 开始新的epoch */
    if (c->algo == CONG_CUBIC) {
        c->w_max = (double)prev_cwnd;
        /* 计算 K = cubic_root(W_max * (1-beta) / C) */
        double arg = c->w_max * (1.0 - CUBIC_BETA) / CUBIC_C;
        c->k = cbrt_approx(arg);
        c->epoch_start = cong_get_time_ms();
    }

    /* Vegas特定处理: 重置base_rtt测量 */
    if (c->algo == CONG_VEGAS) {
        c->base_rtt = VEGAS_BASE_RTT_DEFAULT;
    }

    /* BBR特定处理: 重置到Startup状态 */
    if (c->algo == CONG_BBR) {
        bbr_state.state = BBR_STARTUP;
        bbr_state.full_bw_reached = 0;
        bbr_state.full_bw = 0;
        bbr_state.full_bw_count = 0;
        c->bw_estimate = 0;
    }
}

/*
 * tcp_cong_on_dupack - 收到重复ACK时的处理
 * 返回: 1 表示应该执行快重传, 0 表示不需要
 */
int tcp_cong_on_dupack(tcp_cong_t *c)
{
    if (!c)
        return 0;

    c->dup_ack_count++;
    c->retransmits++;

    /* 三次重复ACK (即第3个dupack) 触发快重传 */
    if (c->dup_ack_count == 3) {
        switch (c->algo) {
        case CONG_RENO:
            /*
             * Reno快恢复:
             * ssthresh = cwnd/2
             * cwnd = ssthresh + 3*MSS (保留已在途的数据)
             */
            c->ssthresh = c->cwnd / 2;
            if (c->ssthresh < 2 * c->mss)
                c->ssthresh = 2 * c->mss;
            c->cwnd = c->ssthresh + 3 * c->mss;
            c->slow_start = 0;
            c->loss_events++;
            break;

        case CONG_CUBIC:
            /*
             * Cubic对快速重传的处理类似Reno,
             * 但窗口减少因子为beta=0.7
             */
            {
                uint32_t prev_cwnd = c->cwnd;
                c->ssthresh = (uint32_t)((double)c->cwnd * CUBIC_BETA);
                if (c->ssthresh < 2 * c->mss)
                    c->ssthresh = 2 * c->mss;
                c->cwnd = c->ssthresh + 3 * c->mss;
                c->slow_start = 0;
                c->loss_events++;

                /* 更新Cubic参数 */
                c->w_max = (double)prev_cwnd;
                double arg = c->w_max * (1.0 - CUBIC_BETA) / CUBIC_C;
                c->k = cbrt_approx(arg);
                c->epoch_start = cong_get_time_ms();
            }
            break;

        case CONG_VEGAS:
            /*
             * Vegas对丢包的反应比Reno/Cubic更温和
             * 只将cwnd降低到3/4或ssthresh
             */
            c->ssthresh = (c->cwnd * 3) / 4;
            if (c->ssthresh < 2 * c->mss)
                c->ssthresh = 2 * c->mss;
            c->cwnd = c->ssthresh + 3 * c->mss;
            c->slow_start = 0;
            c->loss_events++;
            break;

        case CONG_BBR:
            /*
             * BBR不因丢包而减小cwnd
             * 只标记需要重传, 由发送端决定是否重传
             * 但为了兼容性, 仍做适度调整
             */
            c->loss_events++;
            /* BBR保持cwnd不变或轻微调整 */
            break;

        default:
            break;
        }

        /* 设置恢复结束标志 */
        c->recovery_end = c->bytes_acked + c->cwnd;

        return 1;  /* 通知调用者执行快重传 */
    }

    /* 快恢复期间的额外dupack: 允许发送一个新包 */
    if (c->dup_ack_count > 3 && c->algo == CONG_RENO) {
        /* TCP Reno: 每个额外的dupack允许cwnd增加1个MSS */
        c->cwnd += c->mss;
    }

    return 0;
}

/*
 * tcp_cong_get_window - 获取当前拥塞窗口大小
 */
uint32_t tcp_cong_get_window(const tcp_cong_t *c)
{
    if (!c)
        return 0;

    /* 确保至少能发送1个MSS */
    if (c->cwnd < c->mss)
        return c->mss;

    return c->cwnd;
}

/*
 * tcp_cong_update_rtt - 更新RTT测量 (使用Jacobson/Karels算法)
 */
void tcp_cong_update_rtt(tcp_cong_t *c, uint32_t rtt_us)
{
    if (!c || rtt_us == 0)
        return;

    /* 更新最小RTT */
    if (rtt_us < c->rtt_min) {
        c->rtt_min = rtt_us;
    }

    /* Vegas: 更新base_rtt (连接以来观测到的最小RTT) */
    if (c->algo == CONG_VEGAS) {
        if (rtt_us < c->base_rtt) {
            c->base_rtt = rtt_us;
        }
    }

    /* Jacobson/Karels RTT估计算法:
     * RTO 更新规则 (RFC6298):
     * SRTT' = (1 - alpha) * SRTT + alpha * RTT   (alpha=1/8)
     * RTTVAR' = (1 - beta) * RTTVAR + beta * |SRTT - RTT|  (beta=1/4)
     */
    if (c->rtt_smoothed == 0) {
        /* 第一次测量 */
        c->rtt_smoothed = rtt_us;
        c->rtt_var = rtt_us / 2;
    } else {
        /* 后续测量: 使用定点数近似避免浮点运算 */
        /* SRTT = 7/8 * SRTT_old + 1/8 * RTT */
        uint32_t srtt_delta = rtt_us - c->rtt_smoothed;
        c->rtt_smoothed += (srtt_delta >> 3);  /* 除以8 */

        /* RTTVAR = 3/4 * RTTVAR_old + 1/4 * |delta| */
        if (srtt_delta >= c->rtt_smoothed)  /* 注意: 此时srtt_delta已被修改 */
            /* 重新计算绝对差值 */
            srtt_delta = (rtt_us > c->rtt_smoothed) ?
                         (rtt_us - c->rtt_smoothed) :
                         (c->rtt_smoothed - rtt_us);

        c->rtt_var += ((srtt_delta - c->rtt_var) >> 2);  /* 加权平均 */
    }

    /* BBR: 更新最小RTT滤波器 */
    if (c->algo == CONG_BBR) {
        if (rtt_us < bbr_state.min_rtt_us) {
            bbr_state.min_rtt_us = rtt_us;
            bbr_state.min_rtt_stamp = cong_get_time_ms();
        }
    }
}

/*
 * tcp_cong_set_algo - 动态切换拥塞控制算法
 */
void tcp_cong_set_algo(tcp_cong_t *c, cong_algo_t algo)
{
    if (!c || algo >= CONG_MAX)
        return;

    /* 保存当前窗口信息 */
    uint32_t saved_cwnd = c->cwnd;
    uint32_t saved_ssthresh = c->ssthresh;

    /* 切换算法 */
    c->algo = algo;

    /* 根据新算法重新初始化特定参数 */
    switch (algo) {
    case CONG_RENO:
        c->slow_start = (saved_cwnd < saved_ssthresh) ? 1 : 0;
        break;

    case CONG_CUBIC:
        c->w_max = (double)saved_cwnd;
        c->c = CUBIC_C;
        c->k = 0.0;
        c->epoch_start = cong_get_time_ms();
        break;

    case CONG_VEGAS:
        c->base_rtt = c->rtt_min != 0xFFFFFFFFu ? c->rtt_min : VEGAS_BASE_RTT_DEFAULT;
        c->vegas_cwnd = saved_cwnd;
        break;

    case CONG_BBR:
        memset(&bbr_state, 0, sizeof(bbr_state));
        bbr_state.state = BBR_STARTUP;
        bbr_state.full_bw = saved_cwnd;  /* 从当前窗口开始估计 */
        bbr_state.min_rtt_us = c->rtt_min;
        break;

    default:
        break;
    }
}

/*
 * tcp_cong_algo_name - 获取算法名称字符串
 */
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

/*
 * tcp_cong_get_stats - 获取拥塞控制统计信息
 */
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
