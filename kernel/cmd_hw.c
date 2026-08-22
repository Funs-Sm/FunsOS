/*
 * kernel/cmd_hw.c - Hardware-subsystem shell commands (PR-7, v0.9)
 *
 * All commands call the print_stats / set_* APIs of the corresponding
 * kernel submodule.  They are honest reporting tools: if a subsystem
 * isn't registered in this VM, the output is "0 devices / 0 events".
 */

#include "cmd_hw.h"
#include "shell.h"
#include "cpufreq.h"
#include "rtc.h"
#include "i2c.h"
#include "spi.h"
#include "gpio.h"
#include "pinctrl.h"
#include "clk.h"
#include "dmaengine.h"
#include "mfd.h"
#include "sensors.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

#ifndef SHELL_MAX_LINE
#define SHELL_MAX_LINE 1024
#endif

static const char *next_token(const char **p)
{
    while (**p == ' ') (*p)++;
    if (**p == '\0') return NULL;
    const char *start = *p;
    while (**p && **p != ' ') (*p)++;
    if (**p) (*p)++;
    return start;
}

/* ------------------------------------------------------------------ *
 * sensors - read all sensors through drivers/char/sensors.c
 * ------------------------------------------------------------------ */
void cmd_sensors(const char *args)
{
    (void)args;
    int32_t n = sensors_get_count();
    char buf[64];
    snprintf(buf, sizeof(buf), "sensors: %d device(s)\n", (int)n);
    shell_print(buf);
    if (n > 0) {
        int32_t cpu = sensors_temp_get_cpu();
        int32_t gpu = sensors_temp_get_gpu();
        snprintf(buf, sizeof(buf), "  cpu: %d C\n  gpu: %d C\n",
                 (int)cpu, (int)gpu);
        shell_print(buf);
    }
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * cpufreq [mhz|gov NAME]
 * ------------------------------------------------------------------ */
void cmd_cpufreq(const char *args)
{
    const char *p = args;
    const char *tok = next_token(&p);
    if (!tok) {
        uint32_t f = cpufreq_get_frequency(0);
        char buf[64];
        snprintf(buf, sizeof(buf), "cpufreq: cpu0 = %u MHz\n", (unsigned)f);
        shell_print(buf);
        cpufreq_print_stats();
        cpufreq_print_governors();
        shell_last_exit_code = 0;
        return;
    }
    if (strcmp(tok, "set") == 0 || (tok[0] >= '0' && tok[0] <= '9')) {
        uint32_t mhz = (uint32_t)strtoul(tok, NULL, 10);
        int rc = cpufreq_set(mhz);
        char buf[64];
        snprintf(buf, sizeof(buf),
                 "cpufreq: set cpu0 = %u MHz (rc=%d)\n",
                 (unsigned)mhz, rc);
        shell_print(buf);
        shell_last_exit_code = rc;
        return;
    }
    if (strcmp(tok, "gov") == 0) {
        const char *name = next_token(&p);
        if (!name) {
            shell_print("Usage: cpufreq gov NAME\n");
            shell_last_exit_code = 1;
            return;
        }
        int rc = cpufreq_set_governor(0, name);
        char buf[64];
        snprintf(buf, sizeof(buf),
                 "cpufreq: governor '%s' (rc=%d)\n", name, rc);
        shell_print(buf);
        shell_last_exit_code = rc == 0 ? 0 : 1;
        return;
    }
    shell_print("Usage: cpufreq [set MHZ|gov NAME]\n");
    shell_last_exit_code = 1;
}

/* ------------------------------------------------------------------ *
 * rtc - read the RTC clock.
 * ------------------------------------------------------------------ */
void cmd_rtc(const char *args)
{
    (void)args;
    rtc_time_t t;
    rtc_read_time(&t);
    char buf[96];
    snprintf(buf, sizeof(buf),
             "rtc: %04u-%02u-%02u %02u:%02u:%02u (ts=%u)\n",
             (unsigned)t.year, (unsigned)t.month, (unsigned)t.day,
             (unsigned)t.hour, (unsigned)t.minute, (unsigned)t.second,
             (unsigned)rtc_get_timestamp());
    shell_print(buf);
    rtc_print_stats();
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * i2c - show bus stats.
 * ------------------------------------------------------------------ */
void cmd_i2c(const char *args)
{
    (void)args;
    i2c_print_stats();
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * spi - show controller stats.
 * ------------------------------------------------------------------ */
void cmd_spi(const char *args)
{
    (void)args;
    spi_print_stats();
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * gpio N [0|1]    read / write a GPIO line.
 * ------------------------------------------------------------------ */
void cmd_gpio(const char *args)
{
    const char *p = args;
    const char *tok = next_token(&p);
    if (!tok) {
        gpio_print_stats();
        shell_last_exit_code = 0;
        return;
    }
    uint32_t n = (uint32_t)strtoul(tok, NULL, 10);
    tok = next_token(&p);
    if (tok) {
        int v = (tok[0] == '1') ? 1 : 0;
        gpio_set_value(n, v);
        char buf[64];
        snprintf(buf, sizeof(buf), "gpio %u = %d\n", (unsigned)n, v);
        shell_print(buf);
        shell_last_exit_code = 0;
        return;
    }
    int v = gpio_get_value(n);
    char buf[64];
    snprintf(buf, sizeof(buf), "gpio %u = %d\n", (unsigned)n, v);
    shell_print(buf);
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * pinctrl / clk / dmaengine / mfd - show stats.
 * ------------------------------------------------------------------ */
void cmd_pinctrl(const char *args)
{
    (void)args;
    pinctrl_print_stats();
    shell_last_exit_code = 0;
}

void cmd_clk(const char *args)
{
    (void)args;
    clk_print_stats();
    shell_last_exit_code = 0;
}

void cmd_dmaengine(const char *args)
{
    (void)args;
    dmaengine_print_stats();
    shell_last_exit_code = 0;
}

void cmd_mfd(const char *args)
{
    (void)args;
    mfd_print_stats();
    shell_last_exit_code = 0;
}