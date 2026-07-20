#ifndef KNETFILTER_H
#define KNETFILTER_H

#include "stdint.h"

#define KNETFILTER_MAX_HOOKS    8
#define KNETFILTER_MAX_HOOKOPS  64
#define KNF_INET_NUMHOOKS       5

#define KNF_INET_PRE_ROUTING   0
#define KNF_INET_LOCAL_IN      1
#define KNF_INET_FORWARD       2
#define KNF_INET_LOCAL_OUT     3
#define KNF_INET_POST_ROUTING  4

#define KNF_ACCEPT   0
#define KNF_DROP     1
#define KNF_STOLEN   2
#define KNF_QUEUE    3
#define KNF_REPEAT   4

struct sk_buff;
struct knf_hook_ops;

typedef unsigned int (*knf_hookfn_t)(unsigned int hooknum, struct sk_buff *skb,
                                     const void *in, const void *out,
                                     int (*okfn)(struct sk_buff *));

struct knf_hook_ops {
    struct knf_hook_ops *next;
    knf_hookfn_t hook;
    const void *owner;
    uint8_t hooknum;
    int8_t priority;
    uint32_t used;
    uint64_t packets;
    uint64_t bytes;
};

typedef struct knf_hook_ops knf_hook_ops_t;

struct knf_conn {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t proto;
    uint8_t state;
    uint32_t timeout;
    uint64_t packets;
    uint64_t bytes;
    uint32_t used;
};

int knetfilter_init(void);
int knf_register_hook(knf_hook_ops_t *reg);
int knf_unregister_hook(knf_hook_ops_t *reg);
unsigned int knf_hook_slow(uint8_t hooknum, struct sk_buff *skb,
                           const void *in, const void *out,
                           int (*okfn)(struct sk_buff *));
void knetfilter_print_stats(void);

#endif
