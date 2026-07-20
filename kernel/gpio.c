#include "gpio.h"
#include "klog.h"
#include "string.h"

struct gpio_global {
    uint8_t initialized;
    gpio_chip_t chips[GPIO_MAX_CHIPS];
    uint32_t n_chips;
    gpio_chip_t *chip_list;
    uint64_t total_requests;
    uint64_t total_frees;
    uint64_t total_in_dir;
    uint64_t total_out_dir;
    uint64_t total_gets;
    uint64_t total_sets;
    uint64_t total_debounces;
};

static struct gpio_global gpio_data;

static int virt_gpio_get(gpio_chip_t *chip, uint32_t offset) {
    if (!chip || offset >= chip->ngpio) return 0;
    return chip->lines[offset].value;
}

static void virt_gpio_set(gpio_chip_t *chip, uint32_t offset, int value) {
    if (!chip || offset >= chip->ngpio) return;
    chip->lines[offset].value = value ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW;
}

static int virt_gpio_direction_input(gpio_chip_t *chip, uint32_t offset) {
    if (!chip || offset >= chip->ngpio) return -1;
    chip->lines[offset].direction = GPIO_DIRECTION_IN;
    chip->direction_in_count++;
    return 0;
}

static int virt_gpio_direction_output(gpio_chip_t *chip, uint32_t offset, int value) {
    if (!chip || offset >= chip->ngpio) return -1;
    chip->lines[offset].direction = GPIO_DIRECTION_OUT;
    chip->lines[offset].value = value ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW;
    chip->direction_out_count++;
    return 0;
}

static int virt_gpio_request(gpio_chip_t *chip, uint32_t offset, const char *label) {
    if (!chip || offset >= chip->ngpio) return -1;
    if (chip->lines[offset].requested) return -1;
    chip->lines[offset].requested = 1;
    if (label) strncpy(chip->lines[offset].label, label, GPIO_NAME_LEN - 1);
    return 0;
}

static void virt_gpio_free(gpio_chip_t *chip, uint32_t offset) {
    if (!chip || offset >= chip->ngpio) return;
    chip->lines[offset].requested = 0;
    chip->lines[offset].label[0] = '\0';
}

static int virt_gpio_set_debounce(gpio_chip_t *chip, uint32_t offset, uint32_t debounce) {
    if (!chip || offset >= chip->ngpio) return -1;
    chip->lines[offset].debounce_ms = debounce;
    chip->debounce_count++;
    return 0;
}

static gpio_chip_t *gpio_find_chip(uint32_t gpio, uint32_t *offset_out) {
    gpio_chip_t *chip = gpio_data.chip_list;
    while (chip) {
        if (gpio >= chip->base && gpio < chip->base + chip->ngpio) {
            if (offset_out) *offset_out = gpio - chip->base;
            return chip;
        }
        chip = chip->next;
    }
    return NULL;
}

static void gpio_init_virtual_chip(gpio_chip_t *chip, const char *name,
                                   uint32_t base, uint32_t ngpio) {
    memset(chip, 0, sizeof(*chip));
    strncpy(chip->name, name, GPIO_NAME_LEN - 1);
    chip->base = base;
    chip->ngpio = ngpio;
    chip->get = virt_gpio_get;
    chip->set = virt_gpio_set;
    chip->direction_input = virt_gpio_direction_input;
    chip->direction_output = virt_gpio_direction_output;
    chip->request = virt_gpio_request;
    chip->free = virt_gpio_free;
    chip->set_debounce = virt_gpio_set_debounce;
    for (uint32_t i = 0; i < ngpio; i++) {
        chip->lines[i].direction = GPIO_DIRECTION_IN;
        chip->lines[i].value = GPIO_LEVEL_LOW;
        chip->lines[i].requested = 0;
        strncpy(chip->lines[i].label, "unused", GPIO_NAME_LEN - 1);
    }
}

int gpio_init(void) {
    if (gpio_data.initialized) return 0;
    memset(&gpio_data, 0, sizeof(gpio_data));

    gpio_init_virtual_chip(&gpio_data.chips[0], "virt_gpio", 0, 32);
    gpio_data.chips[0].next = NULL;
    gpio_data.chip_list = &gpio_data.chips[0];
    gpio_data.n_chips = 1;

    gpio_request(0, "led_power");
    gpio_direction_output(0, 1);
    gpio_request(1, "button_reset");
    gpio_direction_input(1);
    gpio_request(2, "cs_sdcard");
    gpio_direction_output(2, 1);
    gpio_set_debounce(1, 50);

    gpio_data.initialized = 1;
    klog_info("GPIO: General-purpose I/O subsystem initialized (%u chips, %u GPIOs)",
              gpio_data.n_chips, 32);
    return 0;
}

int gpiochip_add_data(gpio_chip_t *chip, void *data) {
    if (!gpio_data.initialized || !chip || gpio_data.n_chips >= GPIO_MAX_CHIPS) return -1;
    memcpy(&gpio_data.chips[gpio_data.n_chips], chip, sizeof(*chip));
    gpio_chip_t *new_chip = &gpio_data.chips[gpio_data.n_chips];
    new_chip->data = data;
    new_chip->next = gpio_data.chip_list;
    gpio_data.chip_list = new_chip;
    gpio_data.n_chips++;
    klog_info("GPIO: registered chip '%s' (base=%u, gpios=%u)",
              new_chip->name, new_chip->base, new_chip->ngpio);
    return 0;
}

int gpio_request(uint32_t gpio, const char *label) {
    uint32_t offset;
    gpio_chip_t *chip = gpio_find_chip(gpio, &offset);
    if (!chip || !chip->request) return -1;
    int ret = chip->request(chip, offset, label);
    if (ret == 0) {
        chip->request_count++;
        gpio_data.total_requests++;
    }
    return ret;
}

void gpio_free(uint32_t gpio) {
    uint32_t offset;
    gpio_chip_t *chip = gpio_find_chip(gpio, &offset);
    if (!chip || !chip->free) return;
    chip->free(chip, offset);
    chip->free_count++;
    gpio_data.total_frees++;
}

int gpio_direction_input(uint32_t gpio) {
    uint32_t offset;
    gpio_chip_t *chip = gpio_find_chip(gpio, &offset);
    if (!chip || !chip->direction_input) return -1;
    int ret = chip->direction_input(chip, offset);
    if (ret == 0) gpio_data.total_in_dir++;
    return ret;
}

int gpio_direction_output(uint32_t gpio, int value) {
    uint32_t offset;
    gpio_chip_t *chip = gpio_find_chip(gpio, &offset);
    if (!chip || !chip->direction_output) return -1;
    int ret = chip->direction_output(chip, offset, value);
    if (ret == 0) gpio_data.total_out_dir++;
    return ret;
}

int gpio_get_value(uint32_t gpio) {
    uint32_t offset;
    gpio_chip_t *chip = gpio_find_chip(gpio, &offset);
    if (!chip || !chip->get) return 0;
    chip->lines[offset].get_count++;
    chip->get_value_count++;
    gpio_data.total_gets++;
    return chip->get(chip, offset);
}

void gpio_set_value(uint32_t gpio, int value) {
    uint32_t offset;
    gpio_chip_t *chip = gpio_find_chip(gpio, &offset);
    if (!chip || !chip->set) return;
    chip->set(chip, offset, value);
    chip->lines[offset].set_count++;
    chip->set_value_count++;
    gpio_data.total_sets++;
}

int gpio_set_debounce(uint32_t gpio, uint32_t debounce) {
    uint32_t offset;
    gpio_chip_t *chip = gpio_find_chip(gpio, &offset);
    if (!chip || !chip->set_debounce) return -1;
    int ret = chip->set_debounce(chip, offset, debounce);
    if (ret == 0) gpio_data.total_debounces++;
    return ret;
}

void gpio_print_stats(void) {
    klog_info("=== GPIO Subsystem Statistics ===");
    klog_info("Initialized: %s", gpio_data.initialized ? "yes" : "no");
    klog_info("GPIO chips: %u", gpio_data.n_chips);
    klog_info("Total requests: %llu", (unsigned long long)gpio_data.total_requests);
    klog_info("Total frees: %llu", (unsigned long long)gpio_data.total_frees);
    klog_info("Total direction_in: %llu", (unsigned long long)gpio_data.total_in_dir);
    klog_info("Total direction_out: %llu", (unsigned long long)gpio_data.total_out_dir);
    klog_info("Total get_value: %llu", (unsigned long long)gpio_data.total_gets);
    klog_info("Total set_value: %llu", (unsigned long long)gpio_data.total_sets);
    klog_info("Total debounce sets: %llu", (unsigned long long)gpio_data.total_debounces);
    klog_info("");

    klog_info("GPIO chips:");
    gpio_chip_t *chip = gpio_data.chip_list;
    uint32_t cidx = 0;
    while (chip && cidx < GPIO_MAX_CHIPS) {
        klog_info("  [%u] %s (base=%u, gpios=%u)", cidx, chip->name, chip->base, chip->ngpio);
        klog_info("    ops: req=%llu free=%llu in=%llu out=%llu get=%llu set=%llu deb=%llu",
                  (unsigned long long)chip->request_count,
                  (unsigned long long)chip->free_count,
                  (unsigned long long)chip->direction_in_count,
                  (unsigned long long)chip->direction_out_count,
                  (unsigned long long)chip->get_value_count,
                  (unsigned long long)chip->set_value_count,
                  (unsigned long long)chip->debounce_count);
        klog_info("    GPIO lines (first 8):");
        for (uint32_t p = 0; p < 8 && p < chip->ngpio; p++) {
            klog_info("      GPIO%u (%s): %s %s=%u gets=%llu sets=%llu debounce=%ums",
                      chip->base + p, chip->lines[p].label,
                      chip->lines[p].requested ? "requested" : "free",
                      chip->lines[p].direction == GPIO_DIRECTION_IN ? "in" : "out",
                      chip->lines[p].value,
                      (unsigned long long)chip->lines[p].get_count,
                      (unsigned long long)chip->lines[p].set_count,
                      chip->lines[p].debounce_ms);
        }
        klog_info("");
        chip = chip->next;
        cidx++;
    }
}
