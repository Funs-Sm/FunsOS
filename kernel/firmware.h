#ifndef FIRMWARE_H
#define FIRMWARE_H

#include "stdint.h"

/* ============================================================
 * Firmware Loader 固件加载子系统
 *
 * 负责加载设备固件到内存，支持同步和异步加载，
 * 内置固件缓存机制避免重复加载。
 * ============================================================ */

#define FIRMWARE_MAX            32
#define FIRMWARE_NAME_MAX      128
#define FIRMWARE_MAX_SIZE    65536
#define FIRMWARE_CACHE_MAX      16

struct firmware;
typedef void (*firmware_cb_t)(const struct firmware *fw, void *context);

typedef struct firmware {
    int                  id;
    int                  used;
    char                 name[FIRMWARE_NAME_MAX];
    const uint8_t       *data;
    uint32_t             size;
    uint32_t             refcnt;
    uint64_t             loaded_time;
    uint64_t             last_access;
    uint64_t             access_count;
    uint8_t              cached;
    uint8_t              data_buf[1024];    /* 模拟固件数据 */
} firmware_t;

typedef struct firmware_cache_entry {
    char                 name[FIRMWARE_NAME_MAX];
    firmware_t          *fw;
    uint64_t             hit_count;
} firmware_cache_t;

/* ============================================================
 * 初始化
 * ============================================================ */
int firmware_init(void);

/* ============================================================
 * 核心 API
 * ============================================================ */

int request_firmware(const struct firmware **fw, const char *name, uint32_t device);
int request_firmware_nowait(const char *name, uint32_t device,
                            uint32_t gfp, void *context, firmware_cb_t cont);
void release_firmware(const struct firmware *fw);
firmware_t *firmware_find(const char *name);
firmware_t *firmware_get_by_name(const char *name);

/* ============================================================
 * 缓存管理
 * ============================================================ */
int firmware_cache_add(const char *name, firmware_t *fw);
void firmware_cache_purge(void);
uint32_t firmware_cache_get_count(void);
uint64_t firmware_cache_get_hits(void);

/* ============================================================
 * 统计与调试
 * ============================================================ */
void firmware_print_stats(void);

#endif /* FIRMWARE_H */
