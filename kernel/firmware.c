#include "firmware.h"
#include "stdint.h"
#include "string.h"
#include "klog.h"

struct firmware_global {
    uint8_t            initialized;
    firmware_t         firmwares[FIRMWARE_MAX];
    uint32_t           fw_count;
    uint32_t           next_id;
    firmware_cache_t   cache[FIRMWARE_CACHE_MAX];
    uint32_t           cache_count;
    uint64_t           total_requests;
    uint64_t           total_nowait;
    uint64_t           total_releases;
    uint64_t           total_cache_hits;
    uint64_t           total_cache_misses;
    uint64_t           failed_requests;
};

static struct firmware_global fw_data;

static void fw_fill_data(firmware_t *f) {
    f->data = f->data_buf;
    f->size = 512 + (f->id * 64) % 512;
    for (uint32_t i = 0; i < f->size && i < sizeof(f->data_buf); i++) {
        f->data_buf[i] = (uint8_t)(i ^ f->id ^ 0xA5);
    }
}

static firmware_t *fw_alloc(const char *name) {
    for (uint32_t i = 0; i < FIRMWARE_MAX; i++) {
        if (!fw_data.firmwares[i].used) {
            firmware_t *f = &fw_data.firmwares[i];
            memset(f, 0, sizeof(*f));
            f->id = fw_data.next_id++;
            f->used = 1;
            f->refcnt = 1;
            strncpy(f->name, name, FIRMWARE_NAME_MAX - 1);
            fw_fill_data(f);
            fw_data.fw_count++;
            return f;
        }
    }
    return NULL;
}

static firmware_cache_t *fw_cache_lookup(const char *name) {
    for (uint32_t i = 0; i < FIRMWARE_CACHE_MAX; i++) {
        if (fw_data.cache[i].fw &&
            strcmp(fw_data.cache[i].name, name) == 0) {
            return &fw_data.cache[i];
        }
    }
    return NULL;
}

int firmware_init(void) {
    if (fw_data.initialized) return 0;
    memset(&fw_data, 0, sizeof(fw_data));

    /* 预加载一些常用固件到缓存 */
    static const char *builtin_fw[] = {
        "intel/ucode/06-8e-0c.bin",
        "rtl_nic/rtl8168h-2.fw",
        "e1000e/e1000e_ich8lan.bin",
        "i915/skl_dmc_ver1.bin",
        "amdgpu/polaris10_k_smc.bin",
        "usbduxfast_fw.bin",
        NULL
    };

    for (int i = 0; builtin_fw[i]; i++) {
        firmware_t *f = fw_alloc(builtin_fw[i]);
        if (f) {
            f->cached = 1;
            firmware_cache_add(builtin_fw[i], f);
        }
    }

    fw_data.initialized = 1;
    klog_info("firmware: firmware loader initialized (%u firmwares, %u cached)",
              fw_data.fw_count, fw_data.cache_count);
    return 0;
}

firmware_t *firmware_find(const char *name) {
    if (!name || !fw_data.initialized) return NULL;
    for (uint32_t i = 0; i < FIRMWARE_MAX; i++) {
        if (fw_data.firmwares[i].used &&
            strcmp(fw_data.firmwares[i].name, name) == 0) {
            return &fw_data.firmwares[i];
        }
    }
    return NULL;
}

firmware_t *firmware_get_by_name(const char *name) {
    return firmware_find(name);
}

int firmware_cache_add(const char *name, firmware_t *fw) {
    if (!name || !fw) return -22;

    /* 检查是否已在缓存 */
    firmware_cache_t *exist = fw_cache_lookup(name);
    if (exist) {
        exist->fw = fw;
        return 0;
    }

    /* 找空闲缓存槽 */
    for (uint32_t i = 0; i < FIRMWARE_CACHE_MAX; i++) {
        if (!fw_data.cache[i].fw) {
            strncpy(fw_data.cache[i].name, name, FIRMWARE_NAME_MAX - 1);
            fw_data.cache[i].fw = fw;
            fw_data.cache[i].hit_count = 0;
            fw_data.cache_count++;
            return 0;
        }
    }
    return -28;
}

void firmware_cache_purge(void) {
    for (uint32_t i = 0; i < FIRMWARE_CACHE_MAX; i++) {
        if (fw_data.cache[i].fw) {
            fw_data.cache[i].fw->cached = 0;
            fw_data.cache[i].fw = NULL;
        }
    }
    fw_data.cache_count = 0;
    klog_info("firmware: cache purged");
}

uint32_t firmware_cache_get_count(void) {
    return fw_data.cache_count;
}

uint64_t firmware_cache_get_hits(void) {
    return fw_data.total_cache_hits;
}

int request_firmware(const struct firmware **fw_out, const char *name, uint32_t device) {
    (void)device;
    if (!fw_out || !name || !fw_data.initialized) return -22;

    fw_data.total_requests++;
    *fw_out = NULL;

    /* 先查缓存 */
    firmware_cache_t *ce = fw_cache_lookup(name);
    if (ce && ce->fw) {
        ce->hit_count++;
        fw_data.total_cache_hits++;
        ce->fw->refcnt++;
        ce->fw->last_access = fw_data.total_requests;
        ce->fw->access_count++;
        *fw_out = ce->fw;
        klog_info("firmware: cache hit for '%s' (hits=%llu)",
                  name, (unsigned long long)ce->hit_count);
        return 0;
    }

    fw_data.total_cache_misses++;

    /* 找已加载的固件 */
    firmware_t *f = firmware_find(name);
    if (f) {
        f->refcnt++;
        f->last_access = fw_data.total_requests;
        f->access_count++;
        *fw_out = f;
        return 0;
    }

    /* 加载新固件 */
    f = fw_alloc(name);
    if (!f) {
        fw_data.failed_requests++;
        klog_info("firmware: failed to allocate firmware structure for '%s'", name);
        return -28;
    }

    f->last_access = fw_data.total_requests;
    f->access_count = 1;
    *fw_out = f;

    klog_info("firmware: loaded '%s' (%u bytes, device=0x%x)",
              name, f->size, device);
    return 0;
}

int request_firmware_nowait(const char *name, uint32_t device,
                            uint32_t gfp, void *context, firmware_cb_t cont) {
    (void)gfp;
    if (!name || !fw_data.initialized) return -22;

    fw_data.total_nowait++;

    const struct firmware *f = NULL;
    int ret = request_firmware(&f, name, device);

    if (cont) {
        cont(f, context);
    }

    return ret;
}

void release_firmware(const struct firmware *fw) {
    if (!fw) return;
    firmware_t *f = (firmware_t *)fw;
    if (f->refcnt > 0) f->refcnt--;
    fw_data.total_releases++;

    /* 如果未缓存且无引用，则释放 */
    if (f->refcnt == 0 && !f->cached) {
        f->used = 0;
        if (fw_data.fw_count > 0) fw_data.fw_count--;
    }
}

void firmware_print_stats(void) {
    if (!fw_data.initialized) {
        klog_info("firmware: not initialized");
        return;
    }

    klog_info("=== Firmware Loader Statistics ===");
    klog_info("Initialized: yes");
    klog_info("Loaded firmwares: %u/%d", fw_data.fw_count, FIRMWARE_MAX);
    klog_info("Cached firmwares: %u/%d", fw_data.cache_count, FIRMWARE_CACHE_MAX);
    klog_info("Total request_firmware() calls: %llu",
              (unsigned long long)fw_data.total_requests);
    klog_info("Total request_firmware_nowait() calls: %llu",
              (unsigned long long)fw_data.total_nowait);
    klog_info("Total release_firmware() calls: %llu",
              (unsigned long long)fw_data.total_releases);
    klog_info("Cache hits: %llu", (unsigned long long)fw_data.total_cache_hits);
    klog_info("Cache misses: %llu", (unsigned long long)fw_data.total_cache_misses);
    klog_info("Failed requests: %llu", (unsigned long long)fw_data.failed_requests);

    uint64_t total_lookups = fw_data.total_cache_hits + fw_data.total_cache_misses;
    if (total_lookups > 0) {
        uint32_t rate = (uint32_t)(fw_data.total_cache_hits * 100 / total_lookups);
        klog_info("Cache hit rate: %u%%", rate);
    }
    klog_info("");

    klog_info("Cached firmware entries:");
    for (uint32_t i = 0; i < FIRMWARE_CACHE_MAX; i++) {
        if (fw_data.cache[i].fw) {
            const char *basename = strrchr(fw_data.cache[i].name, '/');
            basename = basename ? basename + 1 : fw_data.cache[i].name;
            klog_info("  [%u] %-32s size=%5u hits=%llu refs=%u",
                      i, basename, fw_data.cache[i].fw->size,
                      (unsigned long long)fw_data.cache[i].hit_count,
                      fw_data.cache[i].fw->refcnt);
        }
    }

    klog_info("");
    klog_info("Recently loaded firmware:");
    int shown = 0;
    for (uint32_t i = 0; i < FIRMWARE_MAX && shown < 8; i++) {
        firmware_t *f = &fw_data.firmwares[i];
        if (f->used && !f->cached) {
            const char *basename = strrchr(f->name, '/');
            basename = basename ? basename + 1 : f->name;
            klog_info("  [%2d] %-28s size=%5u refs=%u accesses=%llu",
                      f->id, basename, f->size, f->refcnt,
                      (unsigned long long)f->access_count);
            shown++;
        }
    }
}
