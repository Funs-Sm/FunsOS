#include "pit.h"
#include "sync.h"
#include "io.h"

static uint32_t pit_ticks = 0;
static uint32_t pit_frequency = 0;

void pit_set_frequency(uint32_t freq) {
    uint16_t divisor = PIT_BASE_FREQ / freq;
    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
    pit_frequency = freq;
}

void pit_init(uint32_t freq) {
    pit_set_frequency(freq);
    pit_ticks = 0;
}

uint16_t pit_read_count(void) {
    outb(PIT_COMMAND, 0x00);
    uint8_t low = inb(PIT_CHANNEL0);
    uint8_t high = inb(PIT_CHANNEL0);
    return (uint16_t)((high << 8) | low);
}

void pit_wait_ticks(uint32_t ticks) {
    uint32_t target = pit_ticks + ticks;
    while (pit_ticks < target) {
        pit_ticks++;
    }
}

void pit_beep(uint32_t freq, uint32_t duration_ms) {
    uint16_t divisor = PIT_BASE_FREQ / freq;
    outb(PIT_COMMAND, 0xB6);
    outb(PIT_CHANNEL2, divisor & 0xFF);
    outb(PIT_CHANNEL2, (divisor >> 8) & 0xFF);

    uint8_t val = inb(0x61);
    outb(0x61, val | 0x03);

    pit_wait_ticks((duration_ms * pit_frequency) / 1000);

    val = inb(0x61);
    outb(0x61, val & ~0x03);
}

void pit_silence(void) {
    uint8_t val = inb(0x61);
    outb(0x61, val & ~0x03);
}
