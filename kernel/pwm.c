#include "pwm.h"
#include "klog.h"
#include "string.h"

struct pwm_global {
    uint8_t initialized;
    struct pwm_chip chips[PWM_MAX_CHIPS];
    uint32_t n_chips;
    struct pwm_chip *chip_list;
    struct pwm_device *pwm_list;
    uint64_t total_requests;
    uint64_t total_frees;
    uint64_t total_configs;
    uint64_t total_enables;
};

static struct pwm_global pwm_data;

static int dummy_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm, uint64_t duty_ns, uint64_t period_ns) {
    (void)chip;
    if (!pwm) return -1;
    if (period_ns == 0) return -1;
    if (duty_ns > period_ns) duty_ns = period_ns;
    pwm->duty_ns = duty_ns;
    pwm->period_ns = period_ns;
    pwm->duty_cycle = duty_ns;
    pwm->period = period_ns;
    return 0;
}

static int dummy_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm) {
    (void)chip;
    if (!pwm) return -1;
    pwm->enabled = 1;
    pwm->enable_count++;
    return 0;
}

static void dummy_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm) {
    (void)chip;
    if (!pwm) return;
    pwm->enabled = 0;
    pwm->disable_count++;
}

static int dummy_pwm_set_polarity(struct pwm_chip *chip, struct pwm_device *pwm, uint32_t polarity) {
    (void)chip;
    if (!pwm) return -1;
    pwm->polarity = polarity;
    return 0;
}

static void pwm_init_dummy_chip(struct pwm_chip *chip) {
    memset(chip, 0, sizeof(*chip));
    strncpy(chip->name, "dummy_pwm", PWM_NAME_LEN);
    chip->npwm = 4;
    chip->config = dummy_pwm_config;
    chip->enable = dummy_pwm_enable;
    chip->disable = dummy_pwm_disable;
    chip->set_polarity = dummy_pwm_set_polarity;
    chip->registered = 1;

    for (uint32_t i = 0; i < chip->npwm; i++) {
        memset(&chip->pwms[i], 0, sizeof(chip->pwms[i]));
        chip->pwms[i].hwpwm = i;
        chip->pwms[i].chip = chip;
        chip->pwms[i].period_ns = 1000000;
        chip->pwms[i].duty_ns = 500000;
        strncpy(chip->pwms[i].label, "unused", PWM_NAME_LEN);
    }
}

int pwm_init(void) {
    if (pwm_data.initialized) return 0;
    memset(&pwm_data, 0, sizeof(pwm_data));

    pwm_init_dummy_chip(&pwm_data.chips[0]);
    pwm_data.n_chips = 1;
    pwm_data.chip_list = &pwm_data.chips[0];
    pwm_data.chips[0].next = NULL;

    pwm_data.pwm_list = &pwm_data.chips[0].pwms[0];
    for (uint32_t i = 0; i < pwm_data.chips[0].npwm - 1; i++) {
        pwm_data.chips[0].pwms[i].next = &pwm_data.chips[0].pwms[i + 1];
    }
    pwm_data.chips[0].pwms[pwm_data.chips[0].npwm - 1].next = NULL;

    pwm_data.initialized = 1;
    klog_info("PWM: pulse width modulation subsystem initialized (%u chips)", pwm_data.n_chips);
    return 0;
}

int pwmchip_add(struct pwm_chip *chip) {
    if (!pwm_data.initialized || !chip || pwm_data.n_chips >= PWM_MAX_CHIPS) return -1;
    memcpy(&pwm_data.chips[pwm_data.n_chips], chip, sizeof(*chip));
    struct pwm_chip *new_chip = &pwm_data.chips[pwm_data.n_chips];
    new_chip->registered = 1;
    new_chip->next = pwm_data.chip_list;
    pwm_data.chip_list = new_chip;

    for (uint32_t i = 0; i < new_chip->npwm; i++) {
        new_chip->pwms[i].chip = new_chip;
        new_chip->pwms[i].hwpwm = i;
        new_chip->pwms[i].next = pwm_data.pwm_list;
        pwm_data.pwm_list = &new_chip->pwms[i];
    }
    pwm_data.n_chips++;
    return 0;
}

int pwmchip_remove(struct pwm_chip *chip) {
    if (!chip || !pwm_data.initialized) return -1;
    struct pwm_chip **prev = &pwm_data.chip_list;
    while (*prev) {
        if (*prev == chip) {
            *prev = chip->next;
            chip->registered = 0;
            pwm_data.n_chips--;
            return 0;
        }
        prev = &(*prev)->next;
    }
    return -1;
}

struct pwm_device *pwm_request(uint32_t pwm_id, const char *label) {
    if (!pwm_data.initialized) return NULL;
    struct pwm_chip *c = pwm_data.chip_list;
    uint32_t total = 0;
    while (c) {
        if (pwm_id < total + c->npwm) {
            struct pwm_device *pwm = &c->pwms[pwm_id - total];
            if (pwm->label[0] != '\0' && strcmp(pwm->label, "unused") != 0 && pwm->label[0] != 0) {
                return NULL;
            }
            if (label) strncpy(pwm->label, label, PWM_NAME_LEN - 1);
            else strncpy(pwm->label, "requested", PWM_NAME_LEN - 1);
            pwm_data.total_requests++;
            return pwm;
        }
        total += c->npwm;
        c = c->next;
    }
    return NULL;
}

void pwm_free(struct pwm_device *pwm) {
    if (!pwm) return;
    pwm_disable(pwm);
    strncpy(pwm->label, "unused", PWM_NAME_LEN);
    pwm_data.total_frees++;
}

int pwm_config(struct pwm_device *pwm, uint64_t duty_ns, uint64_t period_ns) {
    if (!pwm || !pwm->chip || !pwm->chip->config) return -1;
    pwm_data.total_configs++;
    return pwm->chip->config(pwm->chip, pwm, duty_ns, period_ns);
}

int pwm_set_polarity(struct pwm_device *pwm, uint32_t polarity) {
    if (!pwm || !pwm->chip || !pwm->chip->set_polarity) return -1;
    if (pwm->enabled) return -1;
    return pwm->chip->set_polarity(pwm->chip, pwm, polarity);
}

int pwm_enable(struct pwm_device *pwm) {
    if (!pwm || !pwm->chip || !pwm->chip->enable) return -1;
    if (pwm->enabled) return 0;
    pwm_data.total_enables++;
    return pwm->chip->enable(pwm->chip, pwm);
}

void pwm_disable(struct pwm_device *pwm) {
    if (!pwm || !pwm->chip || !pwm->chip->disable) return;
    if (!pwm->enabled) return;
    pwm->chip->disable(pwm->chip, pwm);
}

void pwm_print_stats(void) {
    struct pwm_device *pwm0 = pwm_request(0, "backlight");
    if (pwm0) {
        pwm_config(pwm0, 200000, 1000000);
        pwm_enable(pwm0);
    }
    struct pwm_device *pwm1 = pwm_request(1, "fan");
    if (pwm1) {
        pwm_config(pwm1, 700000, 1000000);
        pwm_set_polarity(pwm1, PWM_POLARITY_NORMAL);
        pwm_enable(pwm1);
    }

    klog_info("=== PWM Subsystem Statistics ===");
    klog_info("Initialized: %s", pwm_data.initialized ? "yes" : "no");
    klog_info("PWM chips: %u", pwm_data.n_chips);
    klog_info("Total requests: %llu, frees: %llu",
              (unsigned long long)pwm_data.total_requests,
              (unsigned long long)pwm_data.total_frees);
    klog_info("Total config calls: %llu, enables: %llu",
              (unsigned long long)pwm_data.total_configs,
              (unsigned long long)pwm_data.total_enables);
    klog_info("");

    struct pwm_chip *c = pwm_data.chip_list;
    uint32_t chip_idx = 0;
    while (c && chip_idx < PWM_MAX_CHIPS) {
        klog_info("Chip %u: %s (%u PWM devices)", chip_idx, c->name, c->npwm);
        uint32_t duty_pct = 0;
        for (uint32_t i = 0; i < c->npwm; i++) {
            struct pwm_device *p = &c->pwms[i];
            if (p->period_ns > 0) {
                duty_pct = (uint32_t)(p->duty_ns * 100 / p->period_ns);
            }
            klog_info("  PWM%u: label='%s', period=%llu ns, duty=%llu ns (%u%%), %s, polarity=%s, en=%llu dis=%llu",
                      i, p->label,
                      (unsigned long long)p->period_ns,
                      (unsigned long long)p->duty_ns,
                      duty_pct,
                      p->enabled ? "enabled" : "disabled",
                      p->polarity == PWM_POLARITY_INVERSED ? "inversed" : "normal",
                      (unsigned long long)p->enable_count,
                      (unsigned long long)p->disable_count);
        }
        klog_info("");
        c = c->next;
        chip_idx++;
    }
}
