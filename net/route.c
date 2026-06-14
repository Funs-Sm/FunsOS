#include "route.h"
#include "string.h"
#include "sync.h"
#include "kheap.h"

/* ------------------------------------------------------------------ */
/*  多路由表                                                           */
/* ------------------------------------------------------------------ */

/* 每个路由表最多 ROUTE_TABLE_MAX 条路由 */
static route_entry_t  route_tables[ROUTE_TABLE_MAX_ID][ROUTE_TABLE_MAX];
static uint32_t       route_table_count[ROUTE_TABLE_MAX_ID];

/* 策略路由规则链表 (按优先级排序) */
static route_policy_t *policy_list = NULL;
static uint32_t        policy_count = 0;

/* 路由缓存 */
static route_cache_entry_t route_cache[ROUTE_CACHE_SIZE];
static uint32_t            route_cache_time = 0;

/* 统计信息 */
static route_stats_t  rstats;
static mutex_t        rtable_lock;

/* 兼容旧接口: 主表指针 */
#define rtable       (route_tables[ROUTE_TABLE_MAIN])
#define rtable_count (route_table_count[ROUTE_TABLE_MAIN])

void route_init(void) {
    memset(route_tables, 0, sizeof(route_tables));
    memset(route_table_count, 0, sizeof(route_table_count));
    memset(&rstats, 0, sizeof(rstats));
    memset(route_cache, 0, sizeof(route_cache));
    policy_list = NULL;
    policy_count = 0;
    route_cache_time = 0;
    mutex_init(&rtable_lock);
}

/* ------------------------------------------------------------------ */
/*  路由缓存                                                           */
/* ------------------------------------------------------------------ */

static uint32_t route_cache_hash(uint32_t dst, uint32_t src) {
    /* 简单哈希函数 */
    return (dst ^ src ^ (dst >> 16) ^ (src >> 16)) % ROUTE_CACHE_SIZE;
}

route_entry_t *route_cache_lookup(uint32_t dst, uint32_t src) {
    uint32_t idx = route_cache_hash(dst, src);
    route_cache_entry_t *e = &route_cache[idx];

    if (e->valid && e->dst == dst && e->src == src && e->route) {
        e->last_used = ++route_cache_time;
        rstats.cache_hits++;
        return e->route;
    }

    rstats.cache_misses++;
    return NULL;
}

void route_cache_insert(uint32_t dst, uint32_t src, route_entry_t *route) {
    if (!route) return;

    uint32_t idx = route_cache_hash(dst, src);
    route_cache_entry_t *e = &route_cache[idx];

    /* 简单替换策略: 直接替换 */
    e->dst = dst;
    e->src = src;
    e->route = route;
    e->last_used = ++route_cache_time;
    e->valid = 1;
}

void route_cache_flush(void) {
    memset(route_cache, 0, sizeof(route_cache));
    route_cache_time = 0;
}

/* ------------------------------------------------------------------ */
/*  策略路由                                                           */
/* ------------------------------------------------------------------ */

int route_add_policy(uint32_t priority, uint32_t match_type,
                     uint32_t match_value, uint32_t match_mask, uint32_t table_id) {
    mutex_lock(&rtable_lock);

    if (policy_count >= ROUTE_POLICY_MAX || table_id >= ROUTE_TABLE_MAX_ID) {
        mutex_unlock(&rtable_lock);
        return -1;
    }

    /* 检查是否已存在相同优先级的规则 */
    route_policy_t *p = policy_list;
    while (p) {
        if (p->priority == priority) {
            /* 更新现有规则 */
            p->match_type = match_type;
            p->match_value = match_value;
            p->match_mask = match_mask;
            p->table_id = table_id;
            mutex_unlock(&rtable_lock);
            return 0;
        }
        p = p->next;
    }

    /* 创建新规则 */
    route_policy_t *np = (route_policy_t *)kmalloc(sizeof(route_policy_t));
    if (!np) {
        mutex_unlock(&rtable_lock);
        return -1;
    }
    np->priority = priority;
    np->match_type = match_type;
    np->match_value = match_value;
    np->match_mask = match_mask;
    np->table_id = table_id;

    /* 按优先级插入链表 (数值越小优先级越高) */
    route_policy_t **pp = &policy_list;
    while (*pp && (*pp)->priority < priority) {
        pp = &(*pp)->next;
    }
    np->next = *pp;
    *pp = np;
    policy_count++;

    mutex_unlock(&rtable_lock);
    return 0;
}

int route_del_policy(uint32_t priority) {
    mutex_lock(&rtable_lock);

    route_policy_t **pp = &policy_list;
    while (*pp) {
        if ((*pp)->priority == priority) {
            route_policy_t *del = *pp;
            *pp = del->next;
            kfree(del);
            policy_count--;
            mutex_unlock(&rtable_lock);
            return 0;
        }
        pp = &(*pp)->next;
    }

    mutex_unlock(&rtable_lock);
    return -1;
}

route_entry_t *route_lookup_policy(uint32_t src, uint32_t dst,
                                    uint32_t tos, uint32_t fwmark) {
    mutex_lock(&rtable_lock);

    route_policy_t *p = policy_list;
    while (p) {
        int match = 0;

        switch (p->match_type) {
        case ROUTE_POLICY_SRC_ADDR:
            match = ((src & p->match_mask) == (p->match_value & p->match_mask));
            break;
        case ROUTE_POLICY_DST_ADDR:
            match = ((dst & p->match_mask) == (p->match_value & p->match_mask));
            break;
        case ROUTE_POLICY_TOS:
            match = ((tos & p->match_mask) == (p->match_value & p->match_mask));
            break;
        case ROUTE_POLICY_FW_MARK:
            match = ((fwmark & p->match_mask) == (p->match_value & p->match_mask));
            break;
        case ROUTE_POLICY_INTERFACE:
            /* 简化: 按接口索引匹配 */
            match = (src == p->match_value);
            break;
        default:
            break;
        }

        if (match && p->table_id < ROUTE_TABLE_MAX_ID) {
            /* 在匹配的策略路由表中查找 */
            static route_entry_t result;
            result = route_lookup_table(p->table_id, (ipv4_addr_t){dst});
            if (result.iface) {
                rstats.policy_matches++;
                mutex_unlock(&rtable_lock);
                return &result;  /* 注意: 返回静态变量，调用者需立即使用 */
            }
        }
        p = p->next;
    }

    mutex_unlock(&rtable_lock);
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  多路由表操作                                                        */
/* ------------------------------------------------------------------ */

int route_add_to_table(uint32_t table_id, ipv4_addr_t dest, ipv4_addr_t mask,
                        ipv4_addr_t gw, net_interface_t *iface,
                        uint32_t metric, uint32_t flags) {
    if (table_id >= ROUTE_TABLE_MAX_ID) return -1;

    mutex_lock(&rtable_lock);

    route_entry_t *tbl = route_tables[table_id];
    uint32_t *cnt = &route_table_count[table_id];

    /* 替换现有条目 */
    for (uint32_t i = 0; i < *cnt; i++) {
        if (tbl[i].dest.addr == dest.addr && tbl[i].mask.addr == mask.addr) {
            tbl[i].gateway = gw;
            tbl[i].iface   = iface;
            tbl[i].metric  = metric;
            tbl[i].flags   = flags;
            rstats.inserts++;
            mutex_unlock(&rtable_lock);
            return 0;
        }
    }

    if (*cnt >= ROUTE_TABLE_MAX) {
        mutex_unlock(&rtable_lock);
        return -1;
    }

    tbl[*cnt].dest    = dest;
    tbl[*cnt].mask    = mask;
    tbl[*cnt].gateway = gw;
    tbl[*cnt].iface   = iface;
    tbl[*cnt].metric  = metric;
    tbl[*cnt].flags   = flags;
    (*cnt)++;
    rstats.inserts++;

    mutex_unlock(&rtable_lock);
    return 0;
}

int route_delete_from_table(uint32_t table_id, ipv4_addr_t dest, ipv4_addr_t mask) {
    if (table_id >= ROUTE_TABLE_MAX_ID) return -1;

    mutex_lock(&rtable_lock);

    route_entry_t *tbl = route_tables[table_id];
    uint32_t *cnt = &route_table_count[table_id];

    for (uint32_t i = 0; i < *cnt; i++) {
        if (tbl[i].dest.addr == dest.addr && tbl[i].mask.addr == mask.addr) {
            for (uint32_t k = i; k + 1 < *cnt; k++) tbl[k] = tbl[k + 1];
            (*cnt)--;
            rstats.deletes++;
            mutex_unlock(&rtable_lock);
            return 0;
        }
    }

    mutex_unlock(&rtable_lock);
    return -1;
}

route_entry_t route_lookup_table(uint32_t table_id, ipv4_addr_t dst) {
    route_entry_t best;
    memset(&best, 0, sizeof(best));

    if (table_id >= ROUTE_TABLE_MAX_ID) return best;

    route_entry_t *tbl = route_tables[table_id];
    uint32_t cnt = route_table_count[table_id];
    uint32_t best_pref = 0;
    int      found     = 0;

    for (uint32_t i = 0; i < cnt; i++) {
        if ((tbl[i].flags & ROUTE_FLAG_UP) == 0) continue;
        if ((dst.addr & tbl[i].mask.addr) != (tbl[i].dest.addr & tbl[i].mask.addr))
            continue;

        uint32_t m = tbl[i].mask.addr;
        uint32_t pref = 0;
        if (m) {
            while (m & 0x80000000U) { pref++; m <<= 1; }
        }

        if (!found || pref > best_pref ||
            (pref == best_pref && tbl[i].metric < best.metric)) {
            best = tbl[i];
            best_pref = pref;
            found = 1;
        }
    }

    return best;
}

/* ------------------------------------------------------------------ */
/*  原有接口 (操作主路由表)                                            */
/* ------------------------------------------------------------------ */

int route_add(ipv4_addr_t dest, ipv4_addr_t mask, ipv4_addr_t gw,
              net_interface_t *iface, uint32_t metric, uint32_t flags) {
    return route_add_to_table(ROUTE_TABLE_MAIN, dest, mask, gw, iface, metric, flags);
}

int route_delete(ipv4_addr_t dest, ipv4_addr_t mask) {
    return route_delete_from_table(ROUTE_TABLE_MAIN, dest, mask);
}

void route_purge_dynamic(void) {
    mutex_lock(&rtable_lock);
    uint32_t k = 0;
    for (uint32_t i = 0; i < rtable_count; i++) {
        if (rtable[i].flags & ROUTE_FLAG_STATIC) {
            rtable[k++] = rtable[i];
        } else {
            rstats.deletes++;
        }
    }
    rtable_count = k;
    mutex_unlock(&rtable_lock);
}

/* ------------------------------------------------------------------ */
/*  Lookup (带缓存和策略路由)                                          */
/* ------------------------------------------------------------------ */

route_entry_t route_lookup(ipv4_addr_t dst) {
    rstats.lookups++;

    /* 1. 先查路由缓存 */
    route_entry_t *cached = route_cache_lookup(dst.addr, 0);
    if (cached) {
        return *cached;
    }

    /* 2. 尝试策略路由 */
    route_entry_t *policy_result = route_lookup_policy(0, dst.addr, 0, 0);
    if (policy_result && policy_result->iface) {
        route_cache_insert(dst.addr, 0, policy_result);
        rstats.hits++;
        return *policy_result;
    }

    /* 3. 在主路由表中查找 */
    route_entry_t best = route_lookup_table(ROUTE_TABLE_MAIN, dst);

    if (best.iface) {
        uint32_t m = best.mask.addr;
        uint32_t pref = 0;
        if (m) {
            while (m & 0x80000000U) { pref++; m <<= 1; }
        }
        rstats.hits++;
        if (pref > rstats.longest_prefix) rstats.longest_prefix = pref;

        /* 插入缓存 */
        static route_entry_t cache_storage;
        cache_storage = best;
        route_cache_insert(dst.addr, 0, &cache_storage);
    } else {
        rstats.misses++;
    }

    return best;
}

int route_resolve(ipv4_addr_t dst, net_interface_t **out_iface,
                  ipv4_addr_t *out_gw) {
    route_entry_t r = route_lookup(dst);
    if (!r.iface) return -1;
    if (!(r.iface->up) || !(r.iface->flags & IFF_UP)) return -1;
    if (r.flags & ROUTE_FLAG_BLACKHOLE) return -1;
    if (out_iface) *out_iface = r.iface;
    if (out_gw) {
        *out_gw = r.gateway.addr ? r.gateway : dst;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Accessors                                                          */
/* ------------------------------------------------------------------ */

const route_entry_t *route_get_all(uint32_t *count) {
    if (count) *count = rtable_count;
    return rtable;
}

const route_stats_t *route_get_stats(void) { return &rstats; }

/* ------------------------------------------------------------------ */
/*  Convenience: install the direct / gateway route for an interface  */
/* ------------------------------------------------------------------ */

int route_install_iface_defaults(net_interface_t *iface) {
    if (!iface) return -1;
    /* Direct subnet: network address + mask as a /N entry. */
    ipv4_addr_t net;
    net.addr = iface->ip.addr & iface->mask.addr;
    if (route_add(net, iface->mask, (ipv4_addr_t){0}, iface, 0,
                  ROUTE_FLAG_UP) != 0) return -1;
    /* Default route via the gateway (if any). */
    if (iface->gateway.addr != 0) {
        ipv4_addr_t zero = (ipv4_addr_t){0};
        if (route_add(zero, zero, iface->gateway, iface, 1,
                      ROUTE_FLAG_UP | ROUTE_FLAG_GATEWAY) != 0) return -1;
    }
    return 0;
}
