/*
 * ext_mem.c - 外部内存管理系统
 *
 * 提供物理内存区域查询功能，支持 4GB 物理内存。
 * 从 BIOS E820 或 Multiboot 信息中获取内存映射，
 * 并提供范围检查、总量查询等接口。
 */

#include "ext_mem.h"
#include "pmm.h"
#include "string.h"

/* ---- 全局状态 ---- */
static mem_map_entry_t mem_map[EXT_MEM_MAX_ENTRIES];
static uint32_t mem_map_count = 0;
static uint32_t total_memory = 0;      /* 总物理内存 (KB) */
static uint32_t available_memory = 0;  /* 可用物理内存 (KB) */

/* ---- 从 Multiboot 信息构建内存映射 ---- */
static void build_mem_map_from_multiboot(void) {
    /* 默认内存映射: 假设标准 PC 内存布局
     * 在实际实现中，应从 Multiboot MMAP 或 BIOS E820 获取 */

    mem_map_count = 0;

    /* 常规内存: 0 - 640KB */
    mem_map[mem_map_count].base = 0x00000000;
    mem_map[mem_map_count].length = 0x000A0000;
    mem_map[mem_map_count].type = MEM_TYPE_CONVENTIONAL;
    mem_map_count++;

    /* VGA 缓冲区: 640KB - 768KB */
    mem_map[mem_map_count].base = 0x000A0000;
    mem_map[mem_map_count].length = 0x00020000;
    mem_map[mem_map_count].type = MEM_TYPE_RESERVED;
    mem_map_count++;

    /* BIOS 扩展区域: 768KB - 1MB */
    mem_map[mem_map_count].base = 0x000C0000;
    mem_map[mem_map_count].length = 0x00040000;
    mem_map[mem_map_count].type = MEM_TYPE_RESERVED;
    mem_map_count++;

    /* 扩展内存: 1MB - 4GB */
    mem_map[mem_map_count].base = 0x00100000;
    mem_map[mem_map_count].length = 0xFFE00000ULL;  /* 到 4GB-2MB */
    mem_map[mem_map_count].type = MEM_TYPE_CONVENTIONAL;
    mem_map_count++;

    /* ACPI 区域: 假设位于 4GB 之前 */
    mem_map[mem_map_count].base = 0xFFE00000ULL;
    mem_map[mem_map_count].length = 0x00100000;
    mem_map[mem_map_count].type = MEM_TYPE_ACPI;
    mem_map_count++;

    /* BIOS ROM: 4GB 顶部 */
    mem_map[mem_map_count].base = 0xFFF00000ULL;
    mem_map[mem_map_count].length = 0x00100000;
    mem_map[mem_map_count].type = MEM_TYPE_RESERVED;
    mem_map_count++;
}

/* ---- 计算内存统计 ---- */
static void compute_stats(void) {
    total_memory = 0;
    available_memory = 0;

    uint32_t i;
    for (i = 0; i < mem_map_count; i++) {
        uint64_t len_kb = mem_map[i].length / 1024;
        total_memory += (uint32_t)len_kb;
        if (mem_map[i].type == MEM_TYPE_CONVENTIONAL) {
            available_memory += (uint32_t)len_kb;
        }
    }
}

/* ---- 初始化 ---- */
void ext_mem_init(void) {
    /* 初始化内存映射 */
    memset(mem_map, 0, sizeof(mem_map));
    mem_map_count = 0;

    /* 构建内存映射 (从 Multiboot 或默认) */
    build_mem_map_from_multiboot();

    /* 计算统计信息 */
    compute_stats();
}

/* ---- 获取总物理内存 (KB) ---- */
uint32_t ext_mem_get_total(void) {
    return total_memory;
}

/* ---- 获取可用物理内存 (KB) ---- */
uint32_t ext_mem_get_available(void) {
    return available_memory;
}

/* ---- 获取内存映射 ---- */
void ext_mem_get_map(mem_map_entry_t *entries, uint32_t *count) {
    if (!entries || !count) return;

    uint32_t i;
    uint32_t n = mem_map_count;
    if (n > EXT_MEM_MAX_ENTRIES) n = EXT_MEM_MAX_ENTRIES;

    for (i = 0; i < n; i++) {
        entries[i] = mem_map[i];
    }
    *count = n;
}

/* ---- 检查内存范围是否可用 ---- */
int ext_mem_check_range(uint64_t addr, uint64_t size) {
    uint64_t end = addr + size;
    uint32_t i;

    for (i = 0; i < mem_map_count; i++) {
        if (mem_map[i].type != MEM_TYPE_CONVENTIONAL) continue;

        uint64_t region_start = mem_map[i].base;
        uint64_t region_end = mem_map[i].base + mem_map[i].length;

        /* 检查请求的范围是否完全包含在此常规内存区域内 */
        if (addr >= region_start && end <= region_end) {
            return 1;  /* 可用 */
        }
    }

    return 0;  /* 不可用 */
}
