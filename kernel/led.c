#include "led.h"
#include "klog.h"
#include "string.h"

struct led_global {
    uint8_t initialized;
    struct led_classdev leds[LED_MAX_LEDS];
    uint32_t n_leds;
    struct led_classdev *led_list;
    struct led_trigger triggers[LED_MAX_TRIGGERS];
    uint32_t n_triggers;
    struct led_trigger *trigger_list;
    uint64_t total_sets;
    uint64_t total_blinks;
};

static struct led_global led_data;

static void dummy_brightness_set(struct led_classdev *led_cdev, uint32_t brightness) {
    if (!led_cdev) return;
    if (brightness > led_cdev->max_brightness) brightness = led_cdev->max_brightness;
    led_cdev->brightness = brightness;
}

static uint32_t dummy_brightness_get(struct led_classdev *led_cdev) {
    if (!led_cdev) return 0;
    return led_cdev->brightness;
}

static void heartbeat_trig_activate(struct led_classdev *led_cdev) {
    if (!led_cdev) return;
    led_cdev->blink_delay_on = 100;
    led_cdev->blink_delay_off = 900;
    led_cdev->blink_active = 1;
}

static void heartbeat_trig_deactivate(struct led_classdev *led_cdev) {
    if (!led_cdev) return;
    led_cdev->blink_active = 0;
}

static void timer_trig_activate(struct led_classdev *led_cdev) {
    if (!led_cdev) return;
    led_cdev->blink_delay_on = 500;
    led_cdev->blink_delay_off = 500;
    led_cdev->blink_active = 1;
}

static void timer_trig_deactivate(struct led_classdev *led_cdev) {
    if (!led_cdev) return;
    led_cdev->blink_active = 0;
}

static void default_on_trig_activate(struct led_classdev *led_cdev) {
    if (!led_cdev) return;
    led_set_brightness(led_cdev, led_cdev->max_brightness);
    led_cdev->blink_active = 0;
}

static void default_on_trig_deactivate(struct led_classdev *led_cdev) {
    if (!led_cdev) return;
    led_set_brightness(led_cdev, 0);
}

static struct led_trigger builtin_triggers[] = {
    { "none", NULL, NULL, 1, NULL },
    { "heartbeat", heartbeat_trig_activate, heartbeat_trig_deactivate, 1, NULL },
    { "timer", timer_trig_activate, timer_trig_deactivate, 1, NULL },
    { "default-on", default_on_trig_activate, default_on_trig_deactivate, 1, NULL },
};

static void led_init_dummy_led(struct led_classdev *led, const char *name, uint32_t max_brightness, const char *default_trigger) {
    memset(led, 0, sizeof(*led));
    strncpy(led->name, name, LED_NAME_LEN - 1);
    led->max_brightness = max_brightness;
    led->brightness = 0;
    led->enabled = 1;
    led->brightness_set = dummy_brightness_set;
    led->brightness_get = dummy_brightness_get;
    led->blink_delay_on = 0;
    led->blink_delay_off = 0;
    led->blink_active = 0;

    struct led_trigger *trig = led_trigger_get_by_name(default_trigger);
    if (trig) {
        led_trigger_set(led, trig);
    }
}

int led_init(void) {
    if (led_data.initialized) return 0;
    memset(&led_data, 0, sizeof(led_data));

    for (uint32_t i = 0; i < sizeof(builtin_triggers)/sizeof(builtin_triggers[0]); i++) {
        memcpy(&led_data.triggers[led_data.n_triggers], &builtin_triggers[i], sizeof(struct led_trigger));
        led_data.triggers[led_data.n_triggers].next = led_data.trigger_list;
        led_data.trigger_list = &led_data.triggers[led_data.n_triggers];
        led_data.n_triggers++;
    }

    led_init_dummy_led(&led_data.leds[0], "power", 255, "default-on");
    led_init_dummy_led(&led_data.leds[1], "disk", 255, "heartbeat");
    led_init_dummy_led(&led_data.leds[2], "capslock", 1, "none");
    led_init_dummy_led(&led_data.leds[3], "numlock", 1, "none");
    led_init_dummy_led(&led_data.leds[4], "scrolllock", 1, "none");

    led_data.n_leds = 5;
    led_data.led_list = &led_data.leds[0];
    for (uint32_t i = 0; i < led_data.n_leds - 1; i++) {
        led_data.leds[i].next = &led_data.leds[i + 1];
    }
    led_data.leds[led_data.n_leds - 1].next = NULL;

    led_data.initialized = 1;
    klog_info("LED: LED subsystem initialized (%u LEDs, %u triggers)",
              led_data.n_leds, led_data.n_triggers);
    return 0;
}

int led_classdev_register(struct led_classdev *led_cdev) {
    if (!led_data.initialized || !led_cdev || led_data.n_leds >= LED_MAX_LEDS) return -1;
    memcpy(&led_data.leds[led_data.n_leds], led_cdev, sizeof(*led_cdev));
    struct led_classdev *new_led = &led_data.leds[led_data.n_leds];
    new_led->enabled = 1;
    new_led->next = led_data.led_list;
    led_data.led_list = new_led;
    led_data.n_leds++;
    return 0;
}

void led_classdev_unregister(struct led_classdev *led_cdev) {
    if (!led_cdev || !led_data.initialized) return;
    struct led_classdev **prev = &led_data.led_list;
    while (*prev) {
        if (*prev == led_cdev) {
            if (led_cdev->trigger && led_cdev->trigger->deactivate) {
                led_cdev->trigger->deactivate(led_cdev);
            }
            led_set_brightness(led_cdev, 0);
            *prev = led_cdev->next;
            led_data.n_leds--;
            return;
        }
        prev = &(*prev)->next;
    }
}

void led_set_brightness(struct led_classdev *led_cdev, uint32_t brightness) {
    if (!led_cdev) return;
    if (brightness > led_cdev->max_brightness) brightness = led_cdev->max_brightness;
    if (led_cdev->brightness_set) {
        led_cdev->brightness_set(led_cdev, brightness);
    } else {
        led_cdev->brightness = brightness;
    }
    led_cdev->set_count++;
    led_data.total_sets++;
}

uint32_t led_get_brightness(struct led_classdev *led_cdev) {
    if (!led_cdev) return 0;
    if (led_cdev->brightness_get) {
        return led_cdev->brightness_get(led_cdev);
    }
    return led_cdev->brightness;
}

int led_blink_set(struct led_classdev *led_cdev, uint32_t delay_on, uint32_t delay_off) {
    if (!led_cdev) return -1;
    led_cdev->blink_delay_on = delay_on;
    led_cdev->blink_delay_off = delay_off;
    led_cdev->blink_active = (delay_on > 0 || delay_off > 0) ? 1 : 0;
    led_cdev->blink_count++;
    led_data.total_blinks++;
    return 0;
}

int led_trigger_register(struct led_trigger *trig) {
    if (!trig || led_data.n_triggers >= LED_MAX_TRIGGERS) return -1;
    memcpy(&led_data.triggers[led_data.n_triggers], trig, sizeof(*trig));
    led_data.triggers[led_data.n_triggers].registered = 1;
    led_data.triggers[led_data.n_triggers].next = led_data.trigger_list;
    led_data.trigger_list = &led_data.triggers[led_data.n_triggers];
    led_data.n_triggers++;
    return 0;
}

void led_trigger_unregister(struct led_trigger *trig) {
    if (!trig) return;
    struct led_trigger **prev = &led_data.trigger_list;
    while (*prev) {
        if (*prev == trig) {
            *prev = trig->next;
            trig->registered = 0;
            led_data.n_triggers--;
            return;
        }
        prev = &(*prev)->next;
    }
}

struct led_trigger *led_trigger_get_by_name(const char *name) {
    if (!name) return NULL;
    struct led_trigger *t = led_data.trigger_list;
    while (t) {
        if (strcmp(t->name, name) == 0) return t;
        t = t->next;
    }
    return NULL;
}

int led_trigger_set(struct led_classdev *led_cdev, struct led_trigger *trig) {
    if (!led_cdev) return -1;
    if (led_cdev->trigger && led_cdev->trigger->deactivate) {
        led_cdev->trigger->deactivate(led_cdev);
    }
    led_cdev->trigger = trig;
    if (trig && trig->activate) {
        trig->activate(led_cdev);
    }
    return 0;
}

void led_tick(void) {
    static uint32_t tick = 0;
    tick++;
    struct led_classdev *led = led_data.led_list;
    while (led) {
        if (led->blink_active && led->blink_delay_on > 0) {
            uint32_t cycle = led->blink_delay_on + led->blink_delay_off;
            if (cycle == 0) cycle = 1000;
            uint32_t pos = tick % cycle;
            if (pos < led->blink_delay_on) {
                led_set_brightness(led, led->max_brightness);
            } else {
                led_set_brightness(led, 0);
            }
        }
        led = led->next;
    }
}

void led_print_stats(void) {
    for (int i = 0; i < 10; i++) led_tick();

    klog_info("=== LED Subsystem Statistics ===");
    klog_info("Initialized: %s", led_data.initialized ? "yes" : "no");
    klog_info("LED devices: %u", led_data.n_leds);
    klog_info("Triggers: %u", led_data.n_triggers);
    klog_info("Total brightness sets: %llu", (unsigned long long)led_data.total_sets);
    klog_info("Total blink configs: %llu", (unsigned long long)led_data.total_blinks);
    klog_info("");

    klog_info("Available triggers:");
    struct led_trigger *t = led_data.trigger_list;
    uint32_t t_idx = 0;
    while (t && t_idx < LED_MAX_TRIGGERS) {
        klog_info("  %s", t->name);
        t = t->next;
        t_idx++;
    }
    klog_info("");

    klog_info("LED devices:");
    struct led_classdev *l = led_data.led_list;
    uint32_t l_idx = 0;
    while (l && l_idx < LED_MAX_LEDS) {
        klog_info("  LED%u: '%s'", l_idx, l->name);
        klog_info("    brightness: %u/%u", l->brightness, l->max_brightness);
        klog_info("    enabled: %s, blinking: %s",
                  l->enabled ? "yes" : "no",
                  l->blink_active ? "yes" : "no");
        if (l->blink_active) {
            klog_info("    blink: on=%u ms, off=%u ms",
                      l->blink_delay_on, l->blink_delay_off);
        }
        klog_info("    trigger: %s", l->trigger ? l->trigger->name : "none");
        klog_info("    sets: %llu, blinks: %llu",
                  (unsigned long long)l->set_count,
                  (unsigned long long)l->blink_count);
        klog_info("");
        l = l->next;
        l_idx++;
    }
}
