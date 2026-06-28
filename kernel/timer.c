#include "stdint.h"
#include "timer.h"
#include "irq.h"
#include "net.h"
#include "sched.h"
#include "io.h"
#include "usb_core.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43

#define PIT_FREQUENCY 1193182

#define PIT_CMD_BINARY      0x00
#define PIT_CMD_MODE3       0x06
#define PIT_CMD_LATCH       0x30
#define PIT_CMD_CHANNEL0    0x00

static volatile uint32_t ticks = 0;
static volatile uint32_t usb_poll_divider = 0;

void timer_set_frequency(uint32_t freq) {
    uint16_t divisor = (uint16_t)(PIT_FREQUENCY / freq);
    outb(PIT_COMMAND, PIT_CMD_BINARY | PIT_CMD_MODE3 | PIT_CMD_LATCH | PIT_CMD_CHANNEL0);
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
}

static volatile uint32_t net_tick_divider = 0;

static void timer_handler(regs_t *regs) {
    (void)regs;
    ticks++;
    sched_tick();

    /* Drive the network stack once every 10 ms (= once per tick at 100 Hz). */
    net_tick_divider++;
    if (net_tick_divider >= 1) {
        net_tick_divider = 0;
        net_tick(ticks * 10U);
    }

    /* USB hot-plug poll every 500ms (50 ticks at 100 Hz) */
    usb_poll_divider++;
    if (usb_poll_divider >= 50) {
        usb_poll_divider = 0;
        usb_hotplug_poll();
    }
}

uint32_t timer_get_ticks(void) {
    return ticks;
}

void timer_sleep(uint32_t ms) {
    uint32_t target = ticks + (ms + 9) / 10;  /* 向上取整, 避免短延迟无效 */
    while (ticks < target) {
        asm volatile("hlt");
    }
}

void init_timer(void) {
    timer_set_frequency(100);
    irq_register_handler(0, timer_handler);
}

static uint32_t next_timer_id = 1;

uint32_t sys_timer_create(uint32_t interval_ms, void (*callback)(void), void *arg) {
    (void)interval_ms; (void)callback; (void)arg;
    return next_timer_id++;
}

void sys_timer_stop(uint32_t timer_id) {
    (void)timer_id;
}

int sys_timer_destroy(uint32_t timer_id) {
    (void)timer_id;
    return 0;
}
