#ifndef KPROBE_H
#define KPROBE_H

#include "stdint.h"
#include "kernel_types.h"

/* ============================================================
 * Kprobe (Kernel Probe) 内核探测子系统
 *
 * 提供动态内核调试能力，可以在任意内核函数入口/返回点
 * 插入探测点，执行预定义的处理函数。
 * ============================================================ */

/* 最大探测点数 */
#define KPROBE_MAX_PROBES     64
#define KPROBE_SYMBOL_MAX    128

/* 探测点类型 */
typedef enum {
    KPROBE_TYPE_ENTRY = 0,   /* 函数入口探测 */
    KPROBE_TYPE_RETURN,      /* 函数返回探测 */
    KPROBE_TYPE_OFFSET,      /* 指定偏移探测 */
} kprobe_type_t;

/* 探测点状态 */
typedef enum {
    KPROBE_STATE_INACTIVE = 0,
    KPROBE_STATE_ACTIVE,
    KPROBE_STATE_DISABLED,
    KPROBE_STATE_HIT,
} kprobe_state_t;

/* 探测点回调函数类型 */
struct kprobe;
typedef void (*kprobe_handler_t)(struct kprobe *kp, regs_t *regs);

/* 探测点结构 */
typedef struct kprobe {
    int              id;           /* 探测点 ID */
    int              used;         /* 是否使用 */
    kprobe_type_t    type;         /* 探测类型 */
    kprobe_state_t   state;        /* 状态 */
    char             symbol[KPROBE_SYMBOL_MAX]; /* 符号名 */
    uint32_t         addr;         /* 探测地址 */
    uint32_t         offset;       /* 偏移量 */
    uint32_t         orig_insn;    /* 原始指令 */
    kprobe_handler_t pre_handler;  /* 执行前回调 */
    kprobe_handler_t post_handler; /* 执行后回调 */
    uint64_t         hit_count;    /* 命中次数 */
    uint64_t         last_hit_tsc; /* 上次命中时间 */
    void            *user_data;    /* 用户数据 */
} kprobe_t;

/* ============================================================
 * 初始化
 * ============================================================ */
void kprobe_init(void);

/* ============================================================
 * 核心 API
 * ============================================================ */

/*
 * kprobe_register - 注册探测点
 * symbol: 函数符号名
 * type: 探测类型
 * pre_handler: 执行前回调
 * post_handler: 执行后回调
 * 返回: 探测点 ID, 负数错误码
 */
int kprobe_register(const char *symbol, kprobe_type_t type,
                    kprobe_handler_t pre_handler,
                    kprobe_handler_t post_handler);

/*
 * kprobe_register_addr - 按地址注册探测点
 * addr: 探测地址
 * type: 探测类型
 * pre_handler: 执行前回调
 * post_handler: 执行后回调
 * 返回: 探测点 ID, 负数错误码
 */
int kprobe_register_addr(uint32_t addr, kprobe_type_t type,
                         kprobe_handler_t pre_handler,
                         kprobe_handler_t post_handler);

/*
 * kprobe_unregister - 注销探测点
 * id: 探测点 ID
 * 返回: 0 成功, 负数错误码
 */
int kprobe_unregister(int id);

/*
 * kprobe_enable - 启用探测点
 * id: 探测点 ID
 * 返回: 0 成功, 负数错误码
 */
int kprobe_enable(int id);

/*
 * kprobe_disable - 禁用探测点
 * id: 探测点 ID
 * 返回: 0 成功, 负数错误码
 */
int kprobe_disable(int id);

/* ============================================================
 * 查询接口
 * ============================================================ */

kprobe_t *kprobe_get(int id);
int kprobe_find_by_addr(uint32_t addr);
uint64_t kprobe_get_hit_count(int id);
void kprobe_dump(int id);
void kprobe_dump_all(void);
int kprobe_get_count(void);

/* ============================================================
 * 统计与调试
 * ============================================================ */

typedef struct {
    uint32_t total_probes;
    uint32_t active_probes;
    uint64_t total_hits;
} kprobe_stats_t;

void kprobe_get_stats(kprobe_stats_t *stats);

#endif /* KPROBE_H */
