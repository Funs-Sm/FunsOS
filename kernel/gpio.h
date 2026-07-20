#ifndef GPIO_H
#define GPIO_H

#include "stdint.h"

#define GPIO_NAME_LEN 32
#define GPIO_MAX_CHIPS 8
#define GPIO_MAX_LINES 64

#define GPIO_DIRECTION_IN  0
#define GPIO_DIRECTION_OUT 1

#define GPIO_LEVEL_LOW  0
#define GPIO_LEVEL_HIGH 1

struct gpio_chip;

typedef int (*gpio_get_t)(struct gpio_chip *chip, uint32_t offset);
typedef void (*gpio_set_t)(struct gpio_chip *chip, uint32_t offset, int value);
typedef int (*gpio_direction_input_t)(struct gpio_chip *chip, uint32_t offset);
typedef int (*gpio_direction_output_t)(struct gpio_chip *chip, uint32_t offset, int value);
typedef int (*gpio_request_t)(struct gpio_chip *chip, uint32_t offset, const char *label);
typedef void (*gpio_free_t)(struct gpio_chip *chip, uint32_t offset);
typedef int (*gpio_set_debounce_t)(struct gpio_chip *chip, uint32_t offset, uint32_t debounce);

struct gpio_line {
    char label[GPIO_NAME_LEN];
    uint8_t direction;
    uint8_t value;
    uint8_t requested;
    uint32_t debounce_ms;
    uint64_t get_count;
    uint64_t set_count;
};

struct gpio_chip {
    char name[GPIO_NAME_LEN];
    uint32_t base;
    uint32_t ngpio;
    struct gpio_line lines[GPIO_MAX_LINES];
    gpio_get_t get;
    gpio_set_t set;
    gpio_direction_input_t direction_input;
    gpio_direction_output_t direction_output;
    gpio_request_t request;
    gpio_free_t free;
    gpio_set_debounce_t set_debounce;
    void *data;
    uint64_t request_count;
    uint64_t free_count;
    uint64_t direction_in_count;
    uint64_t direction_out_count;
    uint64_t get_value_count;
    uint64_t set_value_count;
    uint64_t debounce_count;
    struct gpio_chip *next;
};

typedef struct gpio_chip gpio_chip_t;

int gpio_init(void);
int gpiochip_add_data(gpio_chip_t *chip, void *data);
int gpio_request(uint32_t gpio, const char *label);
void gpio_free(uint32_t gpio);
int gpio_direction_input(uint32_t gpio);
int gpio_direction_output(uint32_t gpio, int value);
int gpio_get_value(uint32_t gpio);
void gpio_set_value(uint32_t gpio, int value);
int gpio_set_debounce(uint32_t gpio, uint32_t debounce);
void gpio_print_stats(void);

#endif
