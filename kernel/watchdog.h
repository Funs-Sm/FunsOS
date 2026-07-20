#ifndef WATCHDOG_H
#define WATCHDOG_H

#include "stdint.h"

#define WDT_DEFAULT_TIMEOUT 10000
#define WDT_MIN_TIMEOUT     1000
#define WDT_MAX_TIMEOUT     60000
#define WDT_PRETIMEOUT_SECS 5

typedef enum {
    WDT_STOPPED = 0,
    WDT_RUNNING = 1,
    WDT_PRETIMEOUT = 2,
    WDT_EXPIRED = 3
} wdt_state_t;

typedef void (*wdt_callback_t)(void *private);

struct watchdog_device {
    char name[32];
    wdt_state_t state;
    uint64_t timeout_ticks;
    uint64_t last_pet;
    uint64_t pretimeout_ticks;
    uint8_t pretimeout_fired;
    wdt_callback_t pretimeout_cb;
    wdt_callback_t expire_cb;
    void *private_data;
    uint32_t pet_count;
    uint32_t timeout_count;
};

int watchdog_init(void);

struct watchdog_device *watchdog_register(const char *name, uint64_t timeout_ms);
int watchdog_unregister(struct watchdog_device *wdt);

int watchdog_start(struct watchdog_device *wdt);
int watchdog_stop(struct watchdog_device *wdt);
int watchdog_pet(struct watchdog_device *wdt);
int watchdog_set_timeout(struct watchdog_device *wdt, uint64_t timeout_ms);
int watchdog_set_pretimeout(struct watchdog_device *wdt, uint64_t pretimeout_ms);
int watchdog_set_callbacks(struct watchdog_device *wdt,
                           wdt_callback_t pretimeout_cb,
                           wdt_callback_t expire_cb,
                           void *private_data);

wdt_state_t watchdog_get_state(struct watchdog_device *wdt);
uint64_t watchdog_time_left(struct watchdog_device *wdt);
void watchdog_tick(void);

void watchdog_set_panic_on_timeout(uint8_t enable);
uint8_t watchdog_get_panic_on_timeout(void);
void watchdog_print_stats(void);

extern struct watchdog_device *kernel_wdt;
extern struct watchdog_device *hpet_wdt;

#endif
