#ifndef PWM_H
#define PWM_H

#include "stdint.h"

#define PWM_NAME_LEN 20
#define PWM_MAX_CHIPS 8
#define PWM_MAX_PER_CHIP 8
#define PWM_MAX_DEVICES (PWM_MAX_CHIPS * PWM_MAX_PER_CHIP)

#define PWM_POLARITY_NORMAL 0
#define PWM_POLARITY_INVERSED 1

struct pwm_device {
    char label[PWM_NAME_LEN];
    uint32_t hwpwm;
    struct pwm_chip *chip;
    uint64_t period;
    uint64_t duty_cycle;
    uint32_t polarity;
    uint8_t enabled;
    uint64_t period_ns;
    uint64_t duty_ns;
    uint64_t enable_count;
    uint64_t disable_count;
    struct pwm_device *next;
};

struct pwm_chip;

typedef int (*pwm_config_t)(struct pwm_chip *chip, struct pwm_device *pwm, uint64_t duty_ns, uint64_t period_ns);
typedef int (*pwm_enable_t)(struct pwm_chip *chip, struct pwm_device *pwm);
typedef void (*pwm_disable_t)(struct pwm_chip *chip, struct pwm_device *pwm);
typedef int (*pwm_set_polarity_t)(struct pwm_chip *chip, struct pwm_device *pwm, uint32_t polarity);

struct pwm_chip {
    char name[PWM_NAME_LEN];
    void *dev;
    struct pwm_device pwms[PWM_MAX_PER_CHIP];
    uint32_t npwm;
    pwm_config_t config;
    pwm_enable_t enable;
    pwm_disable_t disable;
    pwm_set_polarity_t set_polarity;
    uint8_t registered;
    struct pwm_chip *next;
};

int pwm_init(void);
int pwmchip_add(struct pwm_chip *chip);
int pwmchip_remove(struct pwm_chip *chip);
struct pwm_device *pwm_request(uint32_t pwm_id, const char *label);
void pwm_free(struct pwm_device *pwm);
int pwm_config(struct pwm_device *pwm, uint64_t duty_ns, uint64_t period_ns);
int pwm_set_polarity(struct pwm_device *pwm, uint32_t polarity);
int pwm_enable(struct pwm_device *pwm);
void pwm_disable(struct pwm_device *pwm);
void pwm_print_stats(void);

#endif
