/* ext_mem.h - 外部内存管理系统 */
#ifndef EXT_MEM_H
#define EXT_MEM_H

#include "stdint.h"

/* 内存区域类型 */
#define MEM_TYPE_CONVENTIONAL  1  /* 常规内存 */
#define MEM_TYPE_RESERVED      2  /* 保留 */
#define MEM_TYPE_ACPI          3  /* ACPI */
#define MEM_TYPE_NVS           4  /* NVS */
#define MEM_TYPE_UNUSABLE      5  /* 不可用 */

/* 最大内存映射条目数 */
#define EXT_MEM_MAX_ENTRIES    64

typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;
} mem_map_entry_t;

void ext_mem_init(void);
uint32_t ext_mem_get_total(void);
uint32_t ext_mem_get_available(void);
void ext_mem_get_map(mem_map_entry_t *entries, uint32_t *count);
int ext_mem_check_range(uint64_t addr, uint64_t size);

#endif /* EXT_MEM_H */
