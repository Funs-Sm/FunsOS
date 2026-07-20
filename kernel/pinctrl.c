#include "pinctrl.h"
#include "klog.h"
#include "string.h"

struct pinctrl_global {
    uint8_t initialized;
    pinctrl_dev_t devices[PINCTRL_MAX_DEVS];
    uint32_t n_devices;
    pinctrl_dev_t *dev_list;
    uint64_t total_registers;
    uint64_t total_selects;
    uint64_t total_mux_enables;
    uint64_t total_pinconfs;
};

static struct pinctrl_global pinctrl_data;

static void pinctrl_init_virtual_pins(pinctrl_dev_t *dev, const char *name, uint32_t n_pins) {
    memset(dev, 0, sizeof(*dev));
    strncpy(dev->name, name, PINCTRL_NAME_LEN - 1);
    dev->n_pins = n_pins;
    for (uint32_t i = 0; i < n_pins; i++) {
        dev->pins[i].pin_id = i;
        strncpy(dev->pins[i].name, "PIN", PINCTRL_NAME_LEN - 1);
        dev->pins[i].mux_function = 0;
        dev->pins[i].config.pull = PINCONF_PULL_NONE;
        dev->pins[i].config.drive_strength = 8;
        dev->pins[i].config.bias = 0;
        dev->pins[i].config.configured = 1;
        dev->pins[i].requested = 0;
    }

    strncpy(dev->states[0].name, "default", PINCTRL_NAME_LEN - 1);
    dev->states[0].n_pins = n_pins;
    for (uint32_t i = 0; i < n_pins; i++) dev->states[0].pins[i] = i;
    dev->states[0].active = 1;
    dev->n_states = 1;

    strncpy(dev->states[1].name, "sleep", PINCTRL_NAME_LEN - 1);
    dev->states[1].n_pins = 0;
    dev->states[1].active = 0;
    dev->n_states++;

    strncpy(dev->states[2].name, "idle", PINCTRL_NAME_LEN - 1);
    dev->states[2].n_pins = 0;
    dev->states[2].active = 0;
    dev->n_states++;
}

int pinctrl_init(void) {
    if (pinctrl_data.initialized) return 0;
    memset(&pinctrl_data, 0, sizeof(pinctrl_data));

    pinctrl_init_virtual_pins(&pinctrl_data.devices[0], "virt_pinctrl", 32);
    pinctrl_data.devices[0].next = NULL;
    pinctrl_data.dev_list = &pinctrl_data.devices[0];
    pinctrl_data.n_devices = 1;

    pinctrl_data.initialized = 1;
    klog_info("PINCTRL: Pin control subsystem initialized (%u devices, %u pins)",
              pinctrl_data.n_devices, 32);
    return 0;
}

int pinctrl_register(pinctrl_dev_t *dev) {
    if (!pinctrl_data.initialized || !dev || pinctrl_data.n_devices >= PINCTRL_MAX_DEVS) return -1;
    memcpy(&pinctrl_data.devices[pinctrl_data.n_devices], dev, sizeof(*dev));
    pinctrl_dev_t *new_dev = &pinctrl_data.devices[pinctrl_data.n_devices];
    new_dev->next = pinctrl_data.dev_list;
    pinctrl_data.dev_list = new_dev;
    new_dev->register_count++;
    pinctrl_data.total_registers++;
    pinctrl_data.n_devices++;
    klog_info("PINCTRL: registered device '%s' (%u pins)",
              new_dev->name, new_dev->n_pins);
    return 0;
}

int pinctrl_select_state(pinctrl_dev_t *dev, const char *state_name) {
    if (!dev || !state_name) return -1;
    for (uint32_t i = 0; i < dev->n_states; i++) {
        if (strcmp(dev->states[i].name, state_name) == 0) {
            for (uint32_t j = 0; j < dev->n_states; j++) {
                dev->states[j].active = 0;
            }
            dev->states[i].active = 1;
            dev->select_count++;
            pinctrl_data.total_selects++;
            klog_info("PINCTRL: '%s' selected state '%s'", dev->name, state_name);
            return 0;
        }
    }
    return -1;
}

int pinmux_enable(pinctrl_dev_t *dev, uint32_t pin, uint32_t func) {
    if (!dev || pin >= dev->n_pins) return -1;
    dev->pins[pin].mux_function = func;
    dev->pins[pin].mux_count++;
    dev->mux_enable_count++;
    pinctrl_data.total_mux_enables++;
    return 0;
}

int pinconf_set(pinctrl_dev_t *dev, uint32_t pin, const pin_config_t *cfg) {
    if (!dev || pin >= dev->n_pins || !cfg) return -1;
    memcpy(&dev->pins[pin].config, cfg, sizeof(*cfg));
    dev->pins[pin].config_count++;
    dev->pinconf_count++;
    pinctrl_data.total_pinconfs++;
    return 0;
}

void pinctrl_print_stats(void) {
    klog_info("=== Pin Control Subsystem Statistics ===");
    klog_info("Initialized: %s", pinctrl_data.initialized ? "yes" : "no");
    klog_info("Devices: %u", pinctrl_data.n_devices);
    klog_info("Total registers: %llu", (unsigned long long)pinctrl_data.total_registers);
    klog_info("Total state selects: %llu", (unsigned long long)pinctrl_data.total_selects);
    klog_info("Total mux enables: %llu", (unsigned long long)pinctrl_data.total_mux_enables);
    klog_info("Total pinconf sets: %llu", (unsigned long long)pinctrl_data.total_pinconfs);
    klog_info("");

    klog_info("Pin controller devices:");
    pinctrl_dev_t *dev = pinctrl_data.dev_list;
    uint32_t didx = 0;
    while (dev && didx < PINCTRL_MAX_DEVS) {
        klog_info("  [%u] %s (%u pins, %u states)",
                  didx, dev->name, dev->n_pins, dev->n_states);
        klog_info("    active states:");
        for (uint32_t s = 0; s < dev->n_states; s++) {
            klog_info("      '%s' (%u pins): %s",
                      dev->states[s].name, dev->states[s].n_pins,
                      dev->states[s].active ? "active" : "inactive");
        }
        klog_info("    pin stats (first 8 pins):");
        for (uint32_t p = 0; p < 8 && p < dev->n_pins; p++) {
            const char *pull_str = "none";
            if (dev->pins[p].config.pull == PINCONF_PULL_UP) pull_str = "up";
            else if (dev->pins[p].config.pull == PINCONF_PULL_DOWN) pull_str = "down";
            klog_info("      pin%u: mux=%u pull=%s drv=%umA mux_sets=%llu cfg_sets=%llu",
                      p, dev->pins[p].mux_function, pull_str,
                      dev->pins[p].config.drive_strength,
                      (unsigned long long)dev->pins[p].mux_count,
                      (unsigned long long)dev->pins[p].config_count);
        }
        klog_info("    device ops: reg=%llu sel=%llu mux=%llu cfg=%llu",
                  (unsigned long long)dev->register_count,
                  (unsigned long long)dev->select_count,
                  (unsigned long long)dev->mux_enable_count,
                  (unsigned long long)dev->pinconf_count);
        klog_info("");
        dev = dev->next;
        didx++;
    }
}
