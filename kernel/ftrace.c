#include "ftrace.h"
#include "klog.h"
#include "string.h"

struct ftrace_global {
    uint8_t initialized;
    uint8_t enabled;
    uint32_t current_tracer;
    struct ftrace_ops ops[FTRACE_MAX_OPS];
    uint32_t n_ops;
    struct ftrace_ops *ops_list;
    struct ftrace_entry ring_buf[FTRACE_RING_BUF_SIZE];
    uint32_t ring_head;
    uint32_t ring_tail;
    uint64_t total_traces;
    uint64_t overflows;
    char filter[FTRACE_MAX_FILTER][FTRACE_NAME_LEN];
    uint32_t n_filter;
    uint8_t filter_enabled;
};

static struct ftrace_global ftrace_data;

static void dummy_tracer_func(unsigned long ip, unsigned long parent_ip, struct ftrace_ops *op) {
    (void)ip;
    (void)parent_ip;
    (void)op;
}

static void function_tracer(unsigned long ip, unsigned long parent_ip, struct ftrace_ops *op) {
    (void)op;
    if (ftrace_data.ring_head - ftrace_data.ring_tail >= FTRACE_RING_BUF_SIZE) {
        ftrace_data.ring_tail = (ftrace_data.ring_tail + 1) % FTRACE_RING_BUF_SIZE;
        ftrace_data.overflows++;
    }
    struct ftrace_entry *e = &ftrace_data.ring_buf[ftrace_data.ring_head % FTRACE_RING_BUF_SIZE];
    e->ip = ip;
    e->parent_ip = parent_ip;
    e->timestamp = ftrace_data.total_traces;
    e->pid = 0;
    e->valid = 1;
    ftrace_data.ring_head = (ftrace_data.ring_head + 1) % FTRACE_RING_BUF_SIZE;
    ftrace_data.total_traces++;
}

static struct ftrace_ops function_ops = {
    "function", 1, 1, function_tracer, NULL
};

static struct ftrace_ops function_graph_ops = {
    "function_graph", 1, 0, dummy_tracer_func, NULL
};

static struct ftrace_ops nop_ops = {
    "nop", 1, 0, dummy_tracer_func, NULL
};

int ftrace_init(void) {
    if (ftrace_data.initialized) return 0;
    memset(&ftrace_data, 0, sizeof(ftrace_data));

    memset(ftrace_data.ring_buf, 0, sizeof(ftrace_data.ring_buf));
    ftrace_data.n_ops = 0;
    ftrace_data.enabled = 0;
    ftrace_data.current_tracer = FTRACE_TRACER_NOP;

    ftrace_register(&nop_ops);
    ftrace_register(&function_ops);
    ftrace_register(&function_graph_ops);

    ftrace_data.filter_enabled = 0;
    ftrace_data.n_filter = 0;

    ftrace_data.initialized = 1;
    klog_info("Ftrace: function tracer initialized (%u ops, ring buf %u entries)",
              ftrace_data.n_ops, FTRACE_RING_BUF_SIZE);
    return 0;
}

int ftrace_register(struct ftrace_ops *ops) {
    if (!ops || ftrace_data.n_ops >= FTRACE_MAX_OPS) return -1;
    memcpy(&ftrace_data.ops[ftrace_data.n_ops], ops, sizeof(*ops));
    ftrace_data.ops[ftrace_data.n_ops].next = ftrace_data.ops_list;
    ftrace_data.ops_list = &ftrace_data.ops[ftrace_data.n_ops];
    ftrace_data.ops[ftrace_data.n_ops].registered = 1;
    ftrace_data.n_ops++;
    return 0;
}

int ftrace_unregister(struct ftrace_ops *ops) {
    if (!ops) return -1;
    struct ftrace_ops **prev = &ftrace_data.ops_list;
    while (*prev) {
        if (*prev == ops) {
            (*prev)->registered = 0;
            (*prev)->enabled = 0;
            *prev = (*prev)->next;
            ftrace_data.n_ops--;
            return 0;
        }
        prev = &(*prev)->next;
    }
    return -1;
}

void ftrace_enable(struct ftrace_ops *ops) {
    if (ops && ops->registered) {
        ops->enabled = 1;
    }
    ftrace_data.enabled = 1;
}

void ftrace_disable(struct ftrace_ops *ops) {
    if (ops) {
        ops->enabled = 0;
    } else {
        struct ftrace_ops *o = ftrace_data.ops_list;
        while (o) {
            o->enabled = 0;
            o = o->next;
        }
        ftrace_data.enabled = 0;
    }
}

int ftrace_set_filter(const char *name) {
    if (!name || ftrace_data.n_filter >= FTRACE_MAX_FILTER) return -1;
    strncpy(ftrace_data.filter[ftrace_data.n_filter], name, FTRACE_NAME_LEN - 1);
    ftrace_data.filter[ftrace_data.n_filter][FTRACE_NAME_LEN - 1] = '\0';
    ftrace_data.n_filter++;
    ftrace_data.filter_enabled = 1;
    return 0;
}

void ftrace_clear_filter(void) {
    ftrace_data.n_filter = 0;
    ftrace_data.filter_enabled = 0;
}

void ftrace_trace_function(unsigned long ip, unsigned long parent_ip) {
    if (!ftrace_data.enabled || !ftrace_data.initialized) return;
    struct ftrace_ops *o = ftrace_data.ops_list;
    while (o) {
        if (o->enabled && o->func) {
            o->func(ip, parent_ip, o);
        }
        o = o->next;
    }
}

void ftrace_set_tracer(uint32_t tracer) {
    struct ftrace_ops *o = ftrace_data.ops_list;
    while (o) {
        o->enabled = 0;
        o = o->next;
    }
    switch (tracer) {
    case FTRACE_TRACER_FUNCTION:
        function_ops.enabled = 1;
        ftrace_data.enabled = 1;
        ftrace_data.current_tracer = tracer;
        break;
    case FTRACE_TRACER_FUNCTION_GRAPH:
        function_graph_ops.enabled = 1;
        ftrace_data.enabled = 1;
        ftrace_data.current_tracer = tracer;
        break;
    default:
        nop_ops.enabled = 1;
        ftrace_data.enabled = 0;
        ftrace_data.current_tracer = FTRACE_TRACER_NOP;
        break;
    }
}

static void ftrace_simulate_trace(void) {
    static unsigned long fake_ips[] = {
        0xC0100000, 0xC0101000, 0xC0102000, 0xC0103000, 0xC0104000,
        0xC0105000, 0xC0106000, 0xC0107000, 0xC0108000, 0xC0109000
    };
    static unsigned long fake_parents[] = {
        0xC0100000, 0xC0100000, 0xC0101000, 0xC0101000, 0xC0102000,
        0xC0102000, 0xC0103000, 0xC0103000, 0xC0104000, 0xC0104000
    };
    static uint32_t idx = 0;
    ftrace_trace_function(fake_ips[idx % 10], fake_parents[idx % 10]);
    idx++;
}

void ftrace_print_stats(void) {
    klog_info("=== Ftrace Function Tracer Statistics ===");
    klog_info("Initialized: %s", ftrace_data.initialized ? "yes" : "no");
    klog_info("Enabled: %s", ftrace_data.enabled ? "yes" : "no");
    const char *tracer_name = "nop";
    switch (ftrace_data.current_tracer) {
    case FTRACE_TRACER_FUNCTION: tracer_name = "function"; break;
    case FTRACE_TRACER_FUNCTION_GRAPH: tracer_name = "function_graph"; break;
    }
    klog_info("Current tracer: %s", tracer_name);
    klog_info("Registered ops: %u", ftrace_data.n_ops);
    klog_info("Total traces recorded: %llu", (unsigned long long)ftrace_data.total_traces);
    klog_info("Ring buffer overflows: %llu", (unsigned long long)ftrace_data.overflows);
    klog_info("Filter rules: %u%s", ftrace_data.n_filter, ftrace_data.filter_enabled ? " (active)" : "");
    klog_info("");

    klog_info("Available tracers:");
    struct ftrace_ops *o = ftrace_data.ops_list;
    uint32_t op_idx = 0;
    while (o && op_idx < FTRACE_MAX_OPS) {
        klog_info("  %s %s", o->name, o->enabled ? "(active)" : "");
        o = o->next;
        op_idx++;
    }
    klog_info("");

    if (ftrace_data.filter_enabled && ftrace_data.n_filter > 0) {
        klog_info("Filtered functions:");
        for (uint32_t i = 0; i < ftrace_data.n_filter; i++) {
            klog_info("  %s", ftrace_data.filter[i]);
        }
        klog_info("");
    }

    uint32_t count = ftrace_data.ring_head - ftrace_data.ring_tail;
    if (count > FTRACE_RING_BUF_SIZE) count = FTRACE_RING_BUF_SIZE;
    if (count > 0) {
        klog_info("Recent trace entries (%u):", count > 16 ? 16 : count);
        uint32_t start = (uint32_t)(ftrace_data.ring_tail % FTRACE_RING_BUF_SIZE);
        for (uint32_t i = 0; i < 16 && i < count; i++) {
            struct ftrace_entry *e = &ftrace_data.ring_buf[(start + i) % FTRACE_RING_BUF_SIZE];
            if (!e->valid) continue;
            klog_info("  [%llu] 0x%lx <- 0x%lx pid=%u",
                      (unsigned long long)e->timestamp,
                      (unsigned long)e->ip,
                      (unsigned long)e->parent_ip,
                      e->pid);
        }
    }

    for (int i = 0; i < 5; i++) {
        ftrace_simulate_trace();
    }
}
