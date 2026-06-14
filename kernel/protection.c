#include "protection.h"
#include "kheap.h"
#include "string.h"
#include "panic.h"
#include "klog.h"

/* 保护统计信息 */
static protection_stats_t prot_stats;

/* 已释放指针跟踪表（用于双重释放和使用后释放检测） */
#define FREED_PTR_TABLE_SIZE 256
static struct {
    void *ptr;
    uint32_t magic;    /* 标记: HEAP_MAGIC_FREE 表示已释放 */
    uint32_t size;     /* 原始分配大小 */
} freed_ptrs[FREED_PTR_TABLE_SIZE];
static uint32_t freed_ptr_count = 0;

/* 栈 canary 位置记录 */
#define STACK_CANARY_TABLE_SIZE 64
static struct {
    uint32_t *canary_addr;   /* canary 值存放的地址 */
    uint32_t expected;       /* 期望的 canary 值 */
} stack_canaries[STACK_CANARY_TABLE_SIZE];
static uint32_t stack_canary_count = 0;

void protection_init(void) {
    memset(&prot_stats, 0, sizeof(prot_stats));
    memset(freed_ptrs, 0, sizeof(freed_ptrs));
    freed_ptr_count = 0;
    memset(stack_canaries, 0, sizeof(stack_canaries));
    stack_canary_count = 0;
}

int heap_check_integrity(void) {
    /*
     * 堆溢出检测: 遍历已释放指针表，检查是否有异常魔数
     * 在实际内核中，这会遍历堆块链表检查每个块的头部/尾部魔数
     * 这里提供框架实现
     */
    int integrity_ok = 1;

    /* 检查已释放块表中的魔数是否被篡改 */
    for (uint32_t i = 0; i < freed_ptr_count; i++) {
        if (freed_ptrs[i].magic != HEAP_MAGIC_FREE) {
            /* 魔数被修改，说明发生了堆溢出 */
            prot_stats.heap_corruptions++;
            integrity_ok = 0;
            klog_err("PROTECTION: Heap corruption detected at freed block %x\n",
                     (uint32_t)freed_ptrs[i].ptr);
        }
    }

    return integrity_ok;
}

int stack_check_guard(void) {
    /*
     * 栈溢出检测: 检查所有注册的栈 canary 值是否被修改
     */
    int integrity_ok = 1;

    for (uint32_t i = 0; i < stack_canary_count; i++) {
        if (stack_canaries[i].canary_addr &&
            *(stack_canaries[i].canary_addr) != stack_canaries[i].expected) {
            prot_stats.stack_overflows++;
            integrity_ok = 0;
            klog_err("PROTECTION: Stack overflow detected! Canary at %x expected %x found %x\n",
                     (uint32_t)stack_canaries[i].canary_addr,
                     stack_canaries[i].expected,
                     *(stack_canaries[i].canary_addr));
        }
    }

    return integrity_ok;
}

int null_ptr_check(void *ptr) {
    /*
     * 空指针解引用保护: 检查指针是否为 NULL 或接近 NULL
     * 返回 1 表示安全，0 表示危险
     */
    if (ptr == 0) {
        prot_stats.null_ptr_derefs++;
        klog_err("PROTECTION: NULL pointer dereference blocked\n");
        return 0;
    }
    /* 检查接近零地址的指针（前 4096 字节通常不可访问） */
    if ((uint32_t)ptr < 0x1000) {
        prot_stats.null_ptr_derefs++;
        klog_err("PROTECTION: Near-NULL pointer dereference blocked at %x\n",
                 (uint32_t)ptr);
        return 0;
    }
    return 1;
}

int double_free_check(void *ptr) {
    /*
     * 双重释放检测: 检查指针是否在已释放表中
     * 返回 1 表示安全（可以释放），0 表示双重释放
     */
    if (ptr == 0) return 0;  /* 释放 NULL 是未定义行为 */

    for (uint32_t i = 0; i < freed_ptr_count; i++) {
        if (freed_ptrs[i].ptr == ptr && freed_ptrs[i].magic == HEAP_MAGIC_FREE) {
            prot_stats.double_frees++;
            klog_err("PROTECTION: Double free detected at %x\n", (uint32_t)ptr);
            return 0;
        }
    }
    return 1;
}

int use_after_free_check(void *ptr) {
    /*
     * 使用后释放检测: 检查指针是否指向已释放的内存
     * 返回 1 表示安全，0 表示使用后释放
     */
    if (ptr == 0) return 0;

    for (uint32_t i = 0; i < freed_ptr_count; i++) {
        if (freed_ptrs[i].magic == HEAP_MAGIC_FREE &&
            freed_ptrs[i].ptr != 0) {
            uint32_t freed_start = (uint32_t)freed_ptrs[i].ptr;
            uint32_t freed_end = freed_start + freed_ptrs[i].size;
            uint32_t check_ptr = (uint32_t)ptr;
            if (check_ptr >= freed_start && check_ptr < freed_end) {
                prot_stats.use_after_frees++;
                klog_err("PROTECTION: Use-after-free detected at %x (freed block %x size %x)\n",
                         (uint32_t)ptr, freed_start, freed_ptrs[i].size);
                return 0;
            }
        }
    }
    return 1;
}

int int_overflow_check(uint64_t a, uint64_t b, int is_add) {
    /*
     * 整数溢出检测: 检查 a op b 是否溢出
     * is_add: 1=加法, 0=乘法
     * 返回 1 表示安全，0 表示溢出
     */
    if (is_add) {
        /* 加法溢出: a + b < a (无符号) */
        uint64_t result = a + b;
        if (result < a) {
            prot_stats.int_overflows++;
            klog_err("PROTECTION: Integer overflow in addition\n");
            return 0;
        }
    } else {
        /* 乘法溢出: a * b / a != b (当 a != 0) */
        if (a != 0) {
            uint64_t result = a * b;
            if (result / a != b) {
                prot_stats.int_overflows++;
                klog_err("PROTECTION: Integer overflow in multiplication\n");
                return 0;
            }
        }
    }
    return 1;
}

void protection_report(void) {
    klog_info("\n=== FUNSOS Protection Report ===\n");
    klog_info("  Heap corruptions:    %u\n", prot_stats.heap_corruptions);
    klog_info("  Stack overflows:      %u\n", prot_stats.stack_overflows);
    klog_info("  NULL dereferences:    %u\n", prot_stats.null_ptr_derefs);
    klog_info("  Double frees:         %u\n", prot_stats.double_frees);
    klog_info("  Use-after-free:       %u\n", prot_stats.use_after_frees);
    klog_info("  Integer overflows:    %u\n", prot_stats.int_overflows);
    klog_info("================================\n");
}

const protection_stats_t *protection_get_stats(void) {
    return &prot_stats;
}

/* 注册已释放的指针（供 kfree 调用） */
void protection_register_free(void *ptr, uint32_t size) {
    if (freed_ptr_count < FREED_PTR_TABLE_SIZE) {
        freed_ptrs[freed_ptr_count].ptr = ptr;
        freed_ptrs[freed_ptr_count].magic = HEAP_MAGIC_FREE;
        freed_ptrs[freed_ptr_count].size = size;
        freed_ptr_count++;
    } else {
        /* 表满，移除最旧的条目 */
        for (uint32_t i = 0; i < FREED_PTR_TABLE_SIZE - 1; i++) {
            freed_ptrs[i] = freed_ptrs[i + 1];
        }
        freed_ptrs[FREED_PTR_TABLE_SIZE - 1].ptr = ptr;
        freed_ptrs[FREED_PTR_TABLE_SIZE - 1].magic = HEAP_MAGIC_FREE;
        freed_ptrs[FREED_PTR_TABLE_SIZE - 1].size = size;
    }
}

/* 注销已释放的指针（供 kmalloc 调用，当重新分配同一地址时） */
void protection_unregister_free(void *ptr) {
    for (uint32_t i = 0; i < freed_ptr_count; i++) {
        if (freed_ptrs[i].ptr == ptr) {
            freed_ptrs[i].magic = 0;
            freed_ptrs[i].ptr = 0;
            freed_ptrs[i].size = 0;
            return;
        }
    }
}

/* 注册栈 canary */
void protection_register_stack_canary(uint32_t *canary_addr) {
    if (stack_canary_count < STACK_CANARY_TABLE_SIZE) {
        stack_canaries[stack_canary_count].canary_addr = canary_addr;
        stack_canaries[stack_canary_count].expected = STACK_CANARY_VALUE;
        *canary_addr = STACK_CANARY_VALUE;
        stack_canary_count++;
    }
}

/* 注销栈 canary */
void protection_unregister_stack_canary(uint32_t *canary_addr) {
    for (uint32_t i = 0; i < stack_canary_count; i++) {
        if (stack_canaries[i].canary_addr == canary_addr) {
            stack_canaries[i].canary_addr = 0;
            return;
        }
    }
}
