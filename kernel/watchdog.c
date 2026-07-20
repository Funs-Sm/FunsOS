#include "watchdog.h"
#include "kheap.h"
#include "string.h"
#include "timer.h"
#include "klog.h"
#include "sync.h"
#include "stdio.h"

#define MAX_WATCHDOGS 8

static struct watchdog_device wdt_pool[MAX_WATCHDOGS];
static uint8_t wdt_initialized = 0;
static uint32_t wdt_count;
static uint8_t panic_on_timeout = 0;
static spinlock_t wdt_lock;

struct watchdog_device *kernel_wdt;
struct watchdog_device *hpet_wdt;

int watchdog_init(void) {
    if (wdt_initialized) return 0;
    spinlock_init(&wdt_lock);
    memset(wdt_pool, 0, sizeof(wdt_pool));
    wdt_count = 0;

    kernel_wdt = watchdog_register("kernel_wdt", WDT_DEFAULT_TIMEOUT);
    if (kernel_wdt) {
        watchdog_set_pretimeout(kernel_wdt, WDT_DEFAULT_TIMEOUT - (WDT_PRETIMEOUT_SECS * 1000));
    }

    hpet_wdt = watchdog_register("hpet_wdt", WDT_DEFAULT_TIMEOUT * 3);

    wdt_initialized = 1;
    klog_info("Watchdog subsystem initialized (%u watchdogs)", wdt_count);
    return 0;
}

struct watchdog_device *watchdog_register(const char *name, uint64_t timeout_ms) {
    if (!name || wdt_count >= MAX_WATCHDOGS) return NULL;

    struct watchdog_device *wdt = &wdt_pool[wdt_count++];
    memset(wdt, 0, sizeof(*wdt));
    strncpy(wdt->name, name, sizeof(wdt->name) - 1);
    wdt->state = WDT_STOPPED;
    wdt->timeout_ticks = timeout_ms;
    wdt->pretimeout_ticks = 0;
    wdt->last_pet = timer_get_ticks();
    wdt->pet_count = 0;
    wdt->timeout_count = 0;
    wdt->pretimeout_fired = 0;
    wdt->pretimeout_cb = NULL;
    wdt->expire_cb = NULL;
    wdt->private_data = NULL;
    return wdt;
}

int watchdog_unregister(struct watchdog_device *wdt) {
    if (!wdt) return -1;
    watchdog_stop(wdt);
    wdt->state = WDT_STOPPED;
    return 0;
}

int watchdog_start(struct watchdog_device *wdt) {
    if (!wdt) return -1;
    spinlock_lock(&wdt_lock);
    wdt->state = WDT_RUNNING;
    wdt->last_pet = timer_get_ticks();
    wdt->pretimeout_fired = 0;
    spinlock_unlock(&wdt_lock);
    klog_debug("watchdog: %s started (timeout=%llu)", wdt->name, (unsigned long long)wdt->timeout_ticks);
    return 0;
}

int watchdog_stop(struct watchdog_device *wdt) {
    if (!wdt) return -1;
    spinlock_lock(&wdt_lock);
    wdt->state = WDT_STOPPED;
    spinlock_unlock(&wdt_lock);
    return 0;
}

int watchdog_pet(struct watchdog_device *wdt) {
    if (!wdt) return -1;
    spinlock_lock(&wdt_lock);
    wdt->last_pet = timer_get_ticks();
    wdt->pet_count++;
    wdt->pretimeout_fired = 0;
    if (wdt->state == WDT_EXPIRED || wdt->state == WDT_PRETIMEOUT) {
        wdt->state = WDT_RUNNING;
    }
    spinlock_unlock(&wdt_lock);
    return 0;
}

int watchdog_set_timeout(struct watchdog_device *wdt, uint64_t timeout_ms) {
    if (!wdt) return -1;
    if (timeout_ms < WDT_MIN_TIMEOUT) timeout_ms = WDT_MIN_TIMEOUT;
    if (timeout_ms > WDT_MAX_TIMEOUT) timeout_ms = WDT_MAX_TIMEOUT;
    wdt->timeout_ticks = timeout_ms;
    return 0;
}

int watchdog_set_pretimeout(struct watchdog_device *wdt, uint64_t pretimeout_ms) {
    if (!wdt) return -1;
    wdt->pretimeout_ticks = pretimeout_ms;
    return 0;
}

int watchdog_set_callbacks(struct watchdog_device *wdt,
                           wdt_callback_t pretimeout_cb,
                           wdt_callback_t expire_cb,
                           void *private_data) {
    if (!wdt) return -1;
    wdt->pretimeout_cb = pretimeout_cb;
    wdt->expire_cb = expire_cb;
    wdt->private_data = private_data;
    return 0;
}

wdt_state_t watchdog_get_state(struct watchdog_device *wdt) {
    return wdt ? wdt->state : WDT_STOPPED;
}

uint64_t watchdog_time_left(struct watchdog_device *wdt) {
    if (!wdt || wdt->state != WDT_RUNNING) return 0;
    uint64_t now = timer_get_ticks();
    uint64_t elapsed = now - wdt->last_pet;
    if (elapsed >= wdt->timeout_ticks) return 0;
    return wdt->timeout_ticks - elapsed;
}

void watchdog_tick(void) {
    if (!wdt_initialized) return;

    uint64_t now = timer_get_ticks();

    for (uint32_t i = 0; i < wdt_count; i++) {
        struct watchdog_device *wdt = &wdt_pool[i];
        if (wdt->state != WDT_RUNNING && wdt->state != WDT_PRETIMEOUT) continue;

        uint64_t elapsed = now - wdt->last_pet;

        if (wdt->pretimeout_ticks > 0 && !wdt->pretimeout_fired &&
            elapsed >= wdt->pretimeout_ticks && elapsed < wdt->timeout_ticks) {
            wdt->state = WDT_PRETIMEOUT;
            wdt->pretimeout_fired = 1;
            klog_warn("watchdog: %s pretimeout! (%llu ms elapsed)",
                      wdt->name, (unsigned long long)elapsed);
            if (wdt->pretimeout_cb) wdt->pretimeout_cb(wdt->private_data);
        }

        if (elapsed >= wdt->timeout_ticks) {
            wdt->state = WDT_EXPIRED;
            wdt->timeout_count++;
            klog_emerg("watchdog: %s TIMEOUT! (%llu ms since last pet)",
                       wdt->name, (unsigned long long)elapsed);
            if (wdt->expire_cb) {
                wdt->expire_cb(wdt->private_data);
            }
            if (panic_on_timeout) {
                klog_emerg("watchdog: panic_on_timeout enabled, halting");
                for (volatile int j = 0; j < 10000000; j++) { asm volatile("pause"); }
            }
        }
    }
}

void watchdog_set_panic_on_timeout(uint8_t enable) {
    panic_on_timeout = enable;
}

uint8_t watchdog_get_panic_on_timeout(void) {
    return panic_on_timeout;
}

void watchdog_print_stats(void) {
    klog_info("=== Watchdog Devices ===");
    klog_info("  panic_on_timeout: %s", panic_on_timeout ? "yes" : "no");
    for (uint32_t i = 0; i < wdt_count; i++) {
        struct watchdog_device *wdt = &wdt_pool[i];
        const char *state_str = "stopped";
        switch (wdt->state) {
            case WDT_RUNNING: state_str = "running"; break;
            case WDT_PRETIMEOUT: state_str = "pretimeout"; break;
            case WDT_EXPIRED: state_str = "EXPIRED"; break;
            default: break;
        }
        klog_info("  [%s] state=%s timeout=%llu pets=%u timeouts=%u left=%llu",
                 wdt->name, state_str,
                 (unsigned long long)wdt->timeout_ticks,
                 wdt->pet_count, wdt->timeout_count,
                 (unsigned long long)watchdog_time_left(wdt));
    }
}
