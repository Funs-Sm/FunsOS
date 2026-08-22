/*
 * kernel/cmd_hw.h - Hardware-subsystem shell commands (PR-7, v0.8.7)
 *
 *   sensors            list / read sensor values
 *   cpufreq [mhz|gov]  show / set CPU frequency / governor
 *   rtc [time]         read / write RTC time
 *   i2c                show I2C stats
 *   spi                show SPI stats
 *   gpio N [0|1]       read / set a GPIO line
 *   pinctrl            show pin-control stats
 *   clk                show clock-tree stats
 *   dmaengine          show DMA stats
 *   mfd                show MFD stats
 */
#ifndef _KERNEL_CMD_HW_H
#define _KERNEL_CMD_HW_H

void cmd_sensors(const char *args);
void cmd_cpufreq(const char *args);
void cmd_rtc(const char *args);
void cmd_i2c(const char *args);
void cmd_spi(const char *args);
void cmd_gpio(const char *args);
void cmd_pinctrl(const char *args);
void cmd_clk(const char *args);
void cmd_dmaengine(const char *args);
void cmd_mfd(const char *args);

#endif