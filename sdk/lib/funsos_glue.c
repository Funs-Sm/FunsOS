/* funsos_glue.c - 内核接口胶水层
 * 桥接 SDK API 和内核系统调用
 *
 * 现在深度集成到内核：直接使用内核的 kmalloc/kfree、
 * 系统服务注册表等功能，不再维护独立的静态堆。
 */

#include "funsos.h"
#include "funsos_api.h"
#include "kheap.h"
#include "string.h"

/* ---- SDK 初始化/清理 ---- */

static int g_sdk_initialized = 0;

int funsos_sdk_init(void)
{
    if (g_sdk_initialized)
        return 0;

    g_sdk_initialized = 1;
    return 0;
}

int funsos_sdk_cleanup(void)
{
    g_sdk_initialized = 0;
    return 0;
}

/* ---- 内存分配封装 - 直接使用内核内存管理器 ---- */

void *funsos_alloc(uint32_t size)
{
    return kmalloc(size);
}

void funsos_free(void *ptr)
{
    kfree(ptr);
}
