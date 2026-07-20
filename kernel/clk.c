#include "clk.h"
#include "klog.h"
#include "string.h"

struct clk_global {
    uint8_t initialized;
    clk_hw_t clks[CLK_MAX_CLKS];
    uint32_t n_clks;
    clk_hw_t *clk_list;
    uint64_t total_prepares;
    uint64_t total_unprepares;
    uint64_t total_enables;
    uint64_t total_disables;
    uint64_t total_set_rates;
    uint64_t total_get_rates;
};

static struct clk_global clk_data;

static int clk_dummy_prepare(clk_hw_t *hw) { if (!hw) return -1; return 0; }
static void clk_dummy_unprepare(clk_hw_t *hw) { (void)hw; }
static int clk_dummy_enable(clk_hw_t *hw) { if (!hw) return -1; return 0; }
static void clk_dummy_disable(clk_hw_t *hw) { (void)hw; }

static uint32_t clk_dummy_recalc_rate(clk_hw_t *hw, uint32_t parent_rate) {
    if (!hw) return 0;
    return parent_rate > 0 ? parent_rate : hw->init_rate;
}

static int clk_dummy_set_rate(clk_hw_t *hw, uint32_t rate, uint32_t parent_rate) {
    (void)parent_rate;
    if (!hw) return -1;
    hw->rate = rate;
    return 0;
}

static uint32_t clk_dummy_get_rate(clk_hw_t *hw) {
    if (!hw) return 0;
    return hw->rate;
}

static const struct clk_ops clk_dummy_ops = {
    .prepare = clk_dummy_prepare,
    .unprepare = clk_dummy_unprepare,
    .enable = clk_dummy_enable,
    .disable = clk_dummy_disable,
    .recalc_rate = clk_dummy_recalc_rate,
    .set_rate = clk_dummy_set_rate,
    .get_rate = clk_dummy_get_rate,
};

static clk_hw_t *clk_get(const char *name) {
    clk_hw_t *c = clk_data.clk_list;
    while (c) {
        if (strcmp(c->name, name) == 0) return c;
        c = c->next;
    }
    return NULL;
}

int clk_init(void) {
    if (clk_data.initialized) return 0;
    memset(&clk_data, 0, sizeof(clk_data));

    clk_hw_t *pll = &clk_data.clks[0];
    memset(pll, 0, sizeof(*pll));
    strncpy(pll->name, "pll", CLK_NAME_LEN - 1);
    pll->parent = NULL;
    pll->ops = &clk_dummy_ops;
    pll->rate = 1000000000;
    pll->init_rate = 1000000000;
    pll->next = NULL;
    clk_data.clk_list = pll;
    clk_data.n_clks++;

    clk_hw_t *cpu_clk = &clk_data.clks[1];
    memset(cpu_clk, 0, sizeof(*cpu_clk));
    strncpy(cpu_clk->name, "cpu_clk", CLK_NAME_LEN - 1);
    cpu_clk->parent = pll;
    cpu_clk->ops = &clk_dummy_ops;
    cpu_clk->rate = 2000000000;
    cpu_clk->init_rate = 2000000000;
    cpu_clk->next = clk_data.clk_list;
    clk_data.clk_list = cpu_clk;
    pll->children[pll->n_children++] = cpu_clk;
    clk_data.n_clks++;

    clk_hw_t *bus_clk = &clk_data.clks[2];
    memset(bus_clk, 0, sizeof(*bus_clk));
    strncpy(bus_clk->name, "bus_clk", CLK_NAME_LEN - 1);
    bus_clk->parent = pll;
    bus_clk->ops = &clk_dummy_ops;
    bus_clk->rate = 100000000;
    bus_clk->init_rate = 100000000;
    bus_clk->next = clk_data.clk_list;
    clk_data.clk_list = bus_clk;
    pll->children[pll->n_children++] = bus_clk;
    clk_data.n_clks++;

    clk_hw_t *periph_clk = &clk_data.clks[3];
    memset(periph_clk, 0, sizeof(*periph_clk));
    strncpy(periph_clk->name, "periph_clk", CLK_NAME_LEN - 1);
    periph_clk->parent = bus_clk;
    periph_clk->ops = &clk_dummy_ops;
    periph_clk->rate = 50000000;
    periph_clk->init_rate = 50000000;
    periph_clk->next = clk_data.clk_list;
    clk_data.clk_list = periph_clk;
    bus_clk->children[bus_clk->n_children++] = periph_clk;
    clk_data.n_clks++;

    clk_prepare(pll);
    clk_enable(pll);
    clk_prepare(cpu_clk);
    clk_enable(cpu_clk);
    clk_prepare(bus_clk);
    clk_enable(bus_clk);
    clk_prepare(periph_clk);
    clk_enable(periph_clk);

    clk_data.initialized = 1;
    klog_info("CLK: Common clock framework initialized (%u clocks)", clk_data.n_clks);
    return 0;
}

int clk_register(clk_hw_t *hw, const char *name, clk_hw_t *parent,
                 const struct clk_ops *ops, uint32_t init_rate) {
    if (!clk_data.initialized || !hw || !name || clk_data.n_clks >= CLK_MAX_CLKS) return -1;
    memset(hw, 0, sizeof(*hw));
    strncpy(hw->name, name, CLK_NAME_LEN - 1);
    hw->parent = parent;
    hw->ops = ops ? ops : &clk_dummy_ops;
    hw->rate = init_rate;
    hw->init_rate = init_rate;
    hw->next = clk_data.clk_list;
    clk_data.clk_list = hw;
    if (parent && parent->n_children < CLK_MAX_PARENTS) {
        parent->children[parent->n_children++] = hw;
    }
    clk_data.n_clks++;
    klog_info("CLK: registered clock '%s' (parent=%s, rate=%u)",
              name, parent ? parent->name : "none", init_rate);
    return 0;
}

int clk_prepare(clk_hw_t *hw) {
    if (!hw) return -1;
    if (hw->parent) clk_prepare(hw->parent);
    if (hw->ops && hw->ops->prepare) {
        int ret = hw->ops->prepare(hw);
        hw->prepare_count++;
        clk_data.total_prepares++;
        return ret;
    }
    hw->prepared = 1;
    hw->prepare_count++;
    clk_data.total_prepares++;
    return 0;
}

void clk_unprepare(clk_hw_t *hw) {
    if (!hw) return;
    if (hw->ops && hw->ops->unprepare) {
        hw->ops->unprepare(hw);
    }
    hw->prepared = 0;
    hw->unprepare_count++;
    clk_data.total_unprepares++;
}

int clk_enable(clk_hw_t *hw) {
    if (!hw) return -1;
    if (hw->enable_count > 0) {
        hw->enable_count++;
        hw->enable_call_count++;
        clk_data.total_enables++;
        return 0;
    }
    if (hw->parent) clk_enable(hw->parent);
    if (hw->ops && hw->ops->enable) {
        int ret = hw->ops->enable(hw);
        if (ret == 0) {
            hw->enabled = 1;
            hw->enable_count = 1;
            hw->enable_call_count++;
            clk_data.total_enables++;
        }
        return ret;
    }
    hw->enabled = 1;
    hw->enable_count = 1;
    hw->enable_call_count++;
    clk_data.total_enables++;
    return 0;
}

void clk_disable(clk_hw_t *hw) {
    if (!hw) return;
    if (hw->enable_count > 1) {
        hw->enable_count--;
        hw->disable_call_count++;
        clk_data.total_disables++;
        return;
    }
    if (hw->ops && hw->ops->disable) {
        hw->ops->disable(hw);
    }
    hw->enabled = 0;
    hw->enable_count = 0;
    hw->disable_call_count++;
    clk_data.total_disables++;
}

uint32_t clk_get_rate(clk_hw_t *hw) {
    if (!hw) return 0;
    uint32_t parent_rate = 0;
    if (hw->parent) parent_rate = clk_get_rate(hw->parent);
    if (hw->ops && hw->ops->get_rate) {
        uint32_t r = hw->ops->get_rate(hw);
        hw->get_rate_count++;
        clk_data.total_get_rates++;
        return r;
    }
    if (hw->ops && hw->ops->recalc_rate) {
        hw->rate = hw->ops->recalc_rate(hw, parent_rate);
    }
    hw->get_rate_count++;
    clk_data.total_get_rates++;
    return hw->rate;
}

int clk_set_rate(clk_hw_t *hw, uint32_t rate) {
    if (!hw) return -1;
    uint32_t parent_rate = 0;
    if (hw->parent) parent_rate = clk_get_rate(hw->parent);
    if (hw->ops && hw->ops->set_rate) {
        int ret = hw->ops->set_rate(hw, rate, parent_rate);
        hw->set_rate_count++;
        clk_data.total_set_rates++;
        return ret;
    }
    hw->rate = rate;
    hw->set_rate_count++;
    clk_data.total_set_rates++;
    return 0;
}

clk_hw_t *clk_get_by_name(const char *name) {
    return clk_get(name);
}

void clk_print_stats(void) {
    klog_info("=== Common Clock Framework Statistics ===");
    klog_info("Initialized: %s", clk_data.initialized ? "yes" : "no");
    klog_info("Clocks registered: %u", clk_data.n_clks);
    klog_info("Total prepares: %llu", (unsigned long long)clk_data.total_prepares);
    klog_info("Total unprepares: %llu", (unsigned long long)clk_data.total_unprepares);
    klog_info("Total enables: %llu", (unsigned long long)clk_data.total_enables);
    klog_info("Total disables: %llu", (unsigned long long)clk_data.total_disables);
    klog_info("Total set_rate: %llu", (unsigned long long)clk_data.total_set_rates);
    klog_info("Total get_rate: %llu", (unsigned long long)clk_data.total_get_rates);
    klog_info("");

    klog_info("Clock tree:");
    clk_hw_t *c = clk_data.clk_list;
    uint32_t cidx = 0;
    while (c && cidx < CLK_MAX_CLKS) {
        const char *state = "";
        if (!c->prepared) state = "unprepared";
        else if (!c->enabled) state = "prepared";
        else state = "enabled";

        klog_info("  [%u] '%s' (%s)", cidx, c->name, state);
        klog_info("    parent: %s", c->parent ? c->parent->name : "none");
        klog_info("    rate: %u Hz (%u MHz)", c->rate, c->rate / 1000000);
        klog_info("    users: %u, children: %u", c->enable_count, c->n_children);
        klog_info("    ops: prep=%llu unprep=%llu en=%llu dis=%llu set=%llu get=%llu",
                  (unsigned long long)c->prepare_count,
                  (unsigned long long)c->unprepare_count,
                  (unsigned long long)c->enable_call_count,
                  (unsigned long long)c->disable_call_count,
                  (unsigned long long)c->set_rate_count,
                  (unsigned long long)c->get_rate_count);
        klog_info("");
        c = c->next;
        cidx++;
    }
}
