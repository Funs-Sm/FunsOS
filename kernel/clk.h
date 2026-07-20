#ifndef CLK_H
#define CLK_H

#include "stdint.h"

#define CLK_NAME_LEN 32
#define CLK_MAX_CLKS 32
#define CLK_MAX_PARENTS 8

#define CLK_RATE_UNCHANGED 0
#define CLK_RATE_CHANGED   1

struct clk_hw;

typedef int (*clk_prepare_t)(struct clk_hw *hw);
typedef void (*clk_unprepare_t)(struct clk_hw *hw);
typedef int (*clk_enable_t)(struct clk_hw *hw);
typedef void (*clk_disable_t)(struct clk_hw *hw);
typedef uint32_t (*clk_recalc_rate_t)(struct clk_hw *hw, uint32_t parent_rate);
typedef int (*clk_set_rate_t)(struct clk_hw *hw, uint32_t rate, uint32_t parent_rate);
typedef uint32_t (*clk_get_rate_t)(struct clk_hw *hw);
typedef int (*clk_set_parent_t)(struct clk_hw *hw, struct clk_hw *parent);

struct clk_ops {
    clk_prepare_t prepare;
    clk_unprepare_t unprepare;
    clk_enable_t enable;
    clk_disable_t disable;
    clk_recalc_rate_t recalc_rate;
    clk_set_rate_t set_rate;
    clk_get_rate_t get_rate;
    clk_set_parent_t set_parent;
};

struct clk_hw {
    char name[CLK_NAME_LEN];
    struct clk_hw *parent;
    const struct clk_ops *ops;
    uint32_t rate;
    uint32_t init_rate;
    uint8_t prepared;
    uint8_t enabled;
    uint32_t enable_count;
    uint64_t prepare_count;
    uint64_t unprepare_count;
    uint64_t enable_call_count;
    uint64_t disable_call_count;
    uint64_t set_rate_count;
    uint64_t get_rate_count;
    struct clk_hw *children[CLK_MAX_PARENTS];
    uint32_t n_children;
    struct clk_hw *next;
};

typedef struct clk_hw clk_hw_t;

int clk_init(void);
int clk_register(clk_hw_t *hw, const char *name, struct clk_hw *parent,
                 const struct clk_ops *ops, uint32_t init_rate);
int clk_prepare(clk_hw_t *hw);
void clk_unprepare(clk_hw_t *hw);
int clk_enable(clk_hw_t *hw);
void clk_disable(clk_hw_t *hw);
uint32_t clk_get_rate(clk_hw_t *hw);
int clk_set_rate(clk_hw_t *hw, uint32_t rate);
clk_hw_t *clk_get_by_name(const char *name);
void clk_print_stats(void);

#endif
