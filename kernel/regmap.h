#ifndef REGMAP_H
#define REGMAP_H

#include "stdint.h"

#define REGMAP_NAME_LEN 16
#define REGMAP_MAX_MAPS 16
#define REGMAP_CACHE_NONE 0
#define REGMAP_CACHE_FLAT 1
#define REGMAP_CACHE_RBTREE 2

struct regmap_config {
    char name[REGMAP_NAME_LEN];
    uint32_t reg_bits;
    uint32_t val_bits;
    uint32_t reg_stride;
    uint32_t max_register;
    uint32_t cache_type;
    uint8_t cache_bypass;
};

struct regmap;

typedef int (*regmap_reg_read_t)(struct regmap *map, uint32_t reg, uint32_t *val);
typedef int (*regmap_reg_write_t)(struct regmap *map, uint32_t reg, uint32_t val);

struct regmap {
    struct regmap_config config;
    uint8_t initialized;
    regmap_reg_read_t reg_read;
    regmap_reg_write_t reg_write;
    void *mmio_base;
    uint32_t *cache_flat;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t total_reads;
    uint64_t total_writes;
    struct regmap *next;
};

int regmap_init(void);
struct regmap *regmap_init_mmio(void *base, const struct regmap_config *config);
struct regmap *regmap_init_i2c(void *i2c_dev, const struct regmap_config *config);
void regmap_exit(struct regmap *map);
int regmap_read(struct regmap *map, uint32_t reg, uint32_t *val);
int regmap_write(struct regmap *map, uint32_t reg, uint32_t val);
int regmap_update_bits(struct regmap *map, uint32_t reg, uint32_t mask, uint32_t val);
int regmap_set_bits(struct regmap *map, uint32_t reg, uint32_t bits);
int regmap_clear_bits(struct regmap *map, uint32_t reg, uint32_t bits);
void regmap_cache_flush(struct regmap *map);
void regmap_print_stats(void);

#endif
