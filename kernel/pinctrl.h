#ifndef PINCTRL_H
#define PINCTRL_H

#include "stdint.h"

#define PINCTRL_NAME_LEN 32
#define PINCTRL_MAX_PINS 64
#define PINCTRL_MAX_DEVS 4
#define PINCTRL_MAX_STATES 8

#define PINCONF_PULL_NONE 0
#define PINCONF_PULL_UP   1
#define PINCONF_PULL_DOWN 2

struct pin_config {
    uint8_t pull;
    uint32_t drive_strength;
    uint32_t bias;
    uint8_t configured;
};

struct pin_desc {
    uint32_t pin_id;
    char name[PINCTRL_NAME_LEN];
    uint32_t mux_function;
    struct pin_config config;
    uint8_t requested;
    uint64_t mux_count;
    uint64_t config_count;
};

struct pinctrl_state {
    char name[PINCTRL_NAME_LEN];
    uint32_t n_pins;
    uint32_t pins[PINCTRL_MAX_PINS];
    uint8_t active;
};

struct pinctrl_dev {
    char name[PINCTRL_NAME_LEN];
    struct pin_desc pins[PINCTRL_MAX_PINS];
    uint32_t n_pins;
    struct pinctrl_state states[PINCTRL_MAX_STATES];
    uint32_t n_states;
    uint64_t register_count;
    uint64_t select_count;
    uint64_t mux_enable_count;
    uint64_t pinconf_count;
    struct pinctrl_dev *next;
};

typedef struct pinctrl_dev pinctrl_dev_t;
typedef struct pin_desc pin_desc_t;
typedef struct pin_config pin_config_t;
typedef struct pinctrl_state pinctrl_state_t;

int pinctrl_init(void);
int pinctrl_register(pinctrl_dev_t *dev);
int pinctrl_select_state(pinctrl_dev_t *dev, const char *state_name);
int pinmux_enable(pinctrl_dev_t *dev, uint32_t pin, uint32_t func);
int pinconf_set(pinctrl_dev_t *dev, uint32_t pin, const pin_config_t *cfg);
void pinctrl_print_stats(void);

#endif
