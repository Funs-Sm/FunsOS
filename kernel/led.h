#ifndef LED_H
#define LED_H

#include "stdint.h"

#define LED_NAME_LEN 24
#define LED_MAX_LEDS 16
#define LED_MAX_TRIGGERS 8
#define LED_BLINK_ON 0
#define LED_BLINK_DELAY_ON 1
#define LED_BLINK_DELAY_OFF 2

struct led_classdev;
struct led_trigger;

typedef void (*led_brightness_set_t)(struct led_classdev *led_cdev, uint32_t brightness);
typedef uint32_t (*led_brightness_get_t)(struct led_classdev *led_cdev);
typedef void (*led_trigger_activate_t)(struct led_classdev *led_cdev);
typedef void (*led_trigger_deactivate_t)(struct led_classdev *led_cdev);

struct led_trigger {
    char name[LED_NAME_LEN];
    led_trigger_activate_t activate;
    led_trigger_deactivate_t deactivate;
    uint8_t registered;
    struct led_trigger *next;
};

struct led_classdev {
    char name[LED_NAME_LEN];
    uint32_t brightness;
    uint32_t max_brightness;
    uint8_t enabled;
    led_brightness_set_t brightness_set;
    led_brightness_get_t brightness_get;
    struct led_trigger *trigger;
    uint32_t blink_delay_on;
    uint32_t blink_delay_off;
    uint8_t blink_active;
    uint64_t set_count;
    uint64_t blink_count;
    struct led_classdev *next;
};

int led_init(void);
int led_classdev_register(struct led_classdev *led_cdev);
void led_classdev_unregister(struct led_classdev *led_cdev);
void led_set_brightness(struct led_classdev *led_cdev, uint32_t brightness);
uint32_t led_get_brightness(struct led_classdev *led_cdev);
int led_blink_set(struct led_classdev *led_cdev, uint32_t delay_on, uint32_t delay_off);
int led_trigger_register(struct led_trigger *trig);
void led_trigger_unregister(struct led_trigger *trig);
int led_trigger_set(struct led_classdev *led_cdev, struct led_trigger *trig);
struct led_trigger *led_trigger_get_by_name(const char *name);
void led_tick(void);
void led_print_stats(void);

#endif
