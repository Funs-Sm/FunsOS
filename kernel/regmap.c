#include "regmap.h"
#include "klog.h"
#include "string.h"

#define REGMAP_STATIC_CACHE_SIZE 256

struct regmap_global {
    uint8_t initialized;
    struct regmap maps[REGMAP_MAX_MAPS];
    uint32_t n_maps;
    struct regmap *map_list;
    uint64_t total_maps_created;
    uint64_t total_maps_destroyed;
};

static struct regmap_global regmap_data;
static uint32_t regmap_static_cache[REGMAP_MAX_MAPS][REGMAP_STATIC_CACHE_SIZE];

static int dummy_mmio_read(struct regmap *map, uint32_t reg, uint32_t *val) {
    if (!map || !val) return -1;
    volatile uint8_t *base = (volatile uint8_t *)map->mmio_base;
    if (!base) {
        *val = 0xDEADBEEF;
        return 0;
    }
    if (map->config.reg_bits == 8) {
        *val = base[reg];
    } else if (map->config.reg_bits == 16) {
        *val = *(volatile uint16_t *)(base + reg * 2);
    } else {
        *val = *(volatile uint32_t *)(base + reg * 4);
    }
    return 0;
}

static int dummy_mmio_write(struct regmap *map, uint32_t reg, uint32_t val) {
    if (!map) return -1;
    volatile uint8_t *base = (volatile uint8_t *)map->mmio_base;
    if (!base) return 0;
    if (map->config.reg_bits == 8) {
        base[reg] = (uint8_t)val;
    } else if (map->config.reg_bits == 16) {
        *(volatile uint16_t *)(base + reg * 2) = (uint16_t)val;
    } else {
        *(volatile uint32_t *)(base + reg * 4) = val;
    }
    return 0;
}

int regmap_init(void) {
    if (regmap_data.initialized) return 0;
    memset(&regmap_data, 0, sizeof(regmap_data));
    memset(regmap_static_cache, 0, sizeof(regmap_static_cache));

    regmap_data.initialized = 1;
    klog_info("Regmap: framework initialized (max %u maps)", REGMAP_MAX_MAPS);
    return 0;
}

struct regmap *regmap_init_mmio(void *base, const struct regmap_config *config) {
    if (!regmap_data.initialized || !config || regmap_data.n_maps >= REGMAP_MAX_MAPS) return NULL;
    struct regmap *map = &regmap_data.maps[regmap_data.n_maps];
    memset(map, 0, sizeof(*map));

    memcpy(&map->config, config, sizeof(*config));
    if (map->config.name[0] == '\0') {
        strncpy(map->config.name, "mmio_regmap", REGMAP_NAME_LEN);
    }
    map->mmio_base = base;
    map->reg_read = dummy_mmio_read;
    map->reg_write = dummy_mmio_write;
    map->initialized = 1;

    if (map->config.cache_type == REGMAP_CACHE_FLAT) {
        map->cache_flat = regmap_static_cache[regmap_data.n_maps];
        memset(map->cache_flat, 0, REGMAP_STATIC_CACHE_SIZE * sizeof(uint32_t));
    }

    map->next = regmap_data.map_list;
    regmap_data.map_list = map;
    regmap_data.n_maps++;
    regmap_data.total_maps_created++;
    return map;
}

struct regmap *regmap_init_i2c(void *i2c_dev, const struct regmap_config *config) {
    (void)i2c_dev;
    if (!regmap_data.initialized || !config || regmap_data.n_maps >= REGMAP_MAX_MAPS) return NULL;
    struct regmap *map = &regmap_data.maps[regmap_data.n_maps];
    memset(map, 0, sizeof(*map));

    memcpy(&map->config, config, sizeof(*config));
    if (map->config.name[0] == '\0') {
        strncpy(map->config.name, "i2c_regmap", REGMAP_NAME_LEN);
    }
    map->reg_read = dummy_mmio_read;
    map->reg_write = dummy_mmio_write;
    map->initialized = 1;

    if (map->config.cache_type == REGMAP_CACHE_FLAT) {
        map->cache_flat = regmap_static_cache[regmap_data.n_maps];
        memset(map->cache_flat, 0, REGMAP_STATIC_CACHE_SIZE * sizeof(uint32_t));
    }

    map->next = regmap_data.map_list;
    regmap_data.map_list = map;
    regmap_data.n_maps++;
    regmap_data.total_maps_created++;
    return map;
}

void regmap_exit(struct regmap *map) {
    if (!map || !regmap_data.initialized) return;
    struct regmap **prev = &regmap_data.map_list;
    while (*prev) {
        if (*prev == map) {
            *prev = map->next;
            regmap_data.n_maps--;
            regmap_data.total_maps_destroyed++;
            memset(map, 0, sizeof(*map));
            return;
        }
        prev = &(*prev)->next;
    }
}

int regmap_read(struct regmap *map, uint32_t reg, uint32_t *val) {
    if (!map || !map->initialized || !val) return -1;
    if (map->config.max_register != 0 && reg > map->config.max_register) return -1;
    map->total_reads++;

    if (map->config.cache_type != REGMAP_CACHE_NONE && !map->config.cache_bypass &&
        map->cache_flat && reg < REGMAP_STATIC_CACHE_SIZE) {
        static uint8_t cache_valid[REGMAP_STATIC_CACHE_SIZE] = {0};
        if (cache_valid[reg]) {
            *val = map->cache_flat[reg];
            map->cache_hits++;
            return 0;
        } else {
            map->cache_misses++;
            if (map->reg_read) {
                int ret = map->reg_read(map, reg, val);
                if (ret == 0) {
                    map->cache_flat[reg] = *val;
                    cache_valid[reg] = 1;
                }
                return ret;
            }
        }
    }

    if (map->reg_read) {
        return map->reg_read(map, reg, val);
    }
    *val = 0;
    return 0;
}

int regmap_write(struct regmap *map, uint32_t reg, uint32_t val) {
    if (!map || !map->initialized) return -1;
    if (map->config.max_register != 0 && reg > map->config.max_register) return -1;
    map->total_writes++;

    if (map->config.cache_type != REGMAP_CACHE_NONE && !map->config.cache_bypass &&
        map->cache_flat && reg < REGMAP_STATIC_CACHE_SIZE) {
        static uint8_t cache_valid[REGMAP_STATIC_CACHE_SIZE] = {0};
        map->cache_flat[reg] = val;
        cache_valid[reg] = 1;
    }

    if (map->reg_write) {
        return map->reg_write(map, reg, val);
    }
    return 0;
}

int regmap_update_bits(struct regmap *map, uint32_t reg, uint32_t mask, uint32_t val) {
    uint32_t tmp;
    int ret = regmap_read(map, reg, &tmp);
    if (ret != 0) return ret;
    tmp &= ~mask;
    tmp |= val & mask;
    return regmap_write(map, reg, tmp);
}

int regmap_set_bits(struct regmap *map, uint32_t reg, uint32_t bits) {
    return regmap_update_bits(map, reg, bits, bits);
}

int regmap_clear_bits(struct regmap *map, uint32_t reg, uint32_t bits) {
    return regmap_update_bits(map, reg, bits, 0);
}

void regmap_cache_flush(struct regmap *map) {
    if (!map || !map->cache_flat) return;
    if (map->config.cache_type == REGMAP_CACHE_FLAT) {
        static uint8_t cache_valid[REGMAP_STATIC_CACHE_SIZE] = {0};
        memset(map->cache_flat, 0, REGMAP_STATIC_CACHE_SIZE * sizeof(uint32_t));
        memset(cache_valid, 0, REGMAP_STATIC_CACHE_SIZE);
    }
    klog_info("Regmap: cache flushed for %s", map->config.name);
}

void regmap_print_stats(void) {
    klog_info("=== Regmap Statistics ===");
    klog_info("Initialized: %s", regmap_data.initialized ? "yes" : "no");
    klog_info("Active maps: %u", regmap_data.n_maps);
    klog_info("Total created: %llu, destroyed: %llu",
              (unsigned long long)regmap_data.total_maps_created,
              (unsigned long long)regmap_data.total_maps_destroyed);
    klog_info("");

    struct regmap *m = regmap_data.map_list;
    uint32_t idx = 0;
    while (m && idx < REGMAP_MAX_MAPS) {
        klog_info("Map %u (%s):", idx, m->config.name);
        klog_info("  reg_bits: %u, val_bits: %u, stride: %u",
                  m->config.reg_bits, m->config.val_bits, m->config.reg_stride);
        klog_info("  max_register: 0x%X", m->config.max_register);
        const char *cache_type = "none";
        if (m->config.cache_type == REGMAP_CACHE_FLAT) cache_type = "flat";
        else if (m->config.cache_type == REGMAP_CACHE_RBTREE) cache_type = "rbtree";
        klog_info("  cache type: %s, bypass: %s", cache_type,
                  m->config.cache_bypass ? "yes" : "no");
        klog_info("  reads: %llu, writes: %llu",
                  (unsigned long long)m->total_reads,
                  (unsigned long long)m->total_writes);
        uint64_t total_cache = m->cache_hits + m->cache_misses;
        if (total_cache > 0) {
            uint32_t hit_pct = (uint32_t)(m->cache_hits * 100 / total_cache);
            klog_info("  cache hits: %llu (%u%%), misses: %llu",
                      (unsigned long long)m->cache_hits,
                      hit_pct,
                      (unsigned long long)m->cache_misses);
        }
        klog_info("  mmio_base: %p", m->mmio_base);
        klog_info("");
        m = m->next;
        idx++;
    }
}
