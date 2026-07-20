#ifndef TRACEPOINT_H
#define TRACEPOINT_H

#include "stdint.h"

/* ============================================================
 * Tracepoints 静态追踪点子系统
 *
 * 提供内核静态追踪点基础设施，允许在代码中定义追踪点，
 * 并在运行时动态注册/注销探针函数。
 * ============================================================ */

#define TRACEPOINT_MAX            128
#define TRACEPOINT_NAME_MAX       64
#define TRACEPOINT_MAX_PROBES      8

struct tracepoint;
typedef void (*tracepoint_func_t)(void *data, unsigned long arg1, unsigned long arg2,
                                  unsigned long arg3, unsigned long arg4);

typedef struct tracepoint_probe {
    int                  used;
    tracepoint_func_t    func;
    void                *data;
    uint64_t             hit_count;
} tracepoint_probe_t;

typedef struct tracepoint {
    int                  used;
    int                  id;
    char                 name[TRACEPOINT_NAME_MAX];
    int                  state;        /* 0=disabled, 1=enabled */
    tracepoint_probe_t   probes[TRACEPOINT_MAX_PROBES];
    uint32_t             probe_count;
    uint64_t             reg_count;    /* 注册次数 */
    uint64_t             unreg_count;  /* 注销次数 */
    uint64_t             call_count;   /* 总调用次数 */
} tracepoint_t;

struct tracepoint_entry {
    const char          *name;
    tracepoint_t        *tp;
};

/* ============================================================
 * 初始化
 * ============================================================ */
int tracepoint_init(void);

/* ============================================================
 * 核心 API
 * ============================================================ */

int tracepoint_probe_register(const char *name, tracepoint_func_t probe, void *data);
int tracepoint_probe_unregister(const char *name, tracepoint_func_t probe, void *data);
void tracepoint_synchronize_unregister(void);
tracepoint_t *tracepoint_find(const char *name);
int tracepoint_enable(const char *name);
int tracepoint_disable(const char *name);
void tracepoint_call(tracepoint_t *tp, unsigned long a1, unsigned long a2,
                     unsigned long a3, unsigned long a4);

/* ============================================================
 * 宏定义 - 定义和使用 tracepoint
 * ============================================================ */

#define DECLARE_TRACE(name, proto, args) \
    extern tracepoint_t __tracepoint_##name; \
    static inline void trace_##name proto { \
        if (__tracepoint_##name.state && __tracepoint_##name.probe_count > 0) \
            tracepoint_call(&__tracepoint_##name, (unsigned long)a1, \
                           (unsigned long)a2, (unsigned long)a3, (unsigned long)a4); \
    }

#define DEFINE_TRACE(name) \
    tracepoint_t __tracepoint_##name = { \
        .used = 1, .name = #name, .state = 1, .probe_count = 0, \
    }

#define REGISTER_TRACE(name, probe, data) \
    tracepoint_probe_register(#name, (tracepoint_func_t)(probe), (data))

#define UNREGISTER_TRACE(name, probe, data) \
    tracepoint_probe_unregister(#name, (tracepoint_func_t)(probe), (data))

/* ============================================================
 * 统计与调试
 * ============================================================ */
void tracepoint_print_stats(void);

#endif /* TRACEPOINT_H */
