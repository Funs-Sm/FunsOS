#ifndef FTRACE_H
#define FTRACE_H

#include "stdint.h"

#define FTRACE_NAME_LEN 32
#define FTRACE_MAX_OPS 16
#define FTRACE_RING_BUF_SIZE 256
#define FTRACE_MAX_FILTER 32

#define FTRACE_TRACER_FUNCTION 0
#define FTRACE_TRACER_FUNCTION_GRAPH 1
#define FTRACE_TRACER_NOP 2

struct ftrace_ops;

typedef void (*ftrace_func_t)(unsigned long ip, unsigned long parent_ip, struct ftrace_ops *op);
typedef void (*ftrace_graph_entry_t)(unsigned long ip);
typedef void (*ftrace_graph_return_t)(unsigned long ip);

struct ftrace_ops {
    char name[FTRACE_NAME_LEN];
    uint8_t registered;
    uint8_t enabled;
    ftrace_func_t func;
    struct ftrace_ops *next;
};

struct ftrace_graph_ops {
    ftrace_graph_entry_t entry;
    ftrace_graph_return_t ret;
};

struct ftrace_entry {
    unsigned long ip;
    unsigned long parent_ip;
    uint64_t timestamp;
    uint32_t pid;
    uint8_t valid;
};

int ftrace_init(void);
int ftrace_register(struct ftrace_ops *ops);
int ftrace_unregister(struct ftrace_ops *ops);
void ftrace_enable(struct ftrace_ops *ops);
void ftrace_disable(struct ftrace_ops *ops);
int ftrace_set_filter(const char *name);
void ftrace_clear_filter(void);
void ftrace_trace_function(unsigned long ip, unsigned long parent_ip);
void ftrace_print_stats(void);

#endif
