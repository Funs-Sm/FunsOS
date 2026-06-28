#include "keyboard.h"
#include "keyboard_map.h"
#include "irq.h"
#include "idt.h"
#include "sync.h"
#include "kheap.h"
#include "vga_text.h"
#include "fb_console.h"
#include "../drivers/vesa.h"
#include "io.h"
#include "../kernel/klog.h"

static keyboard_event_t kb_buffer[KEYBOARD_BUFFER_SIZE];
static uint32_t kb_head = 0;
static uint32_t kb_tail = 0;

static int shift_pressed = 0;
static int ctrl_pressed = 0;
static int alt_pressed = 0;
static int caps_lock = 0;
static int extended_key = 0;

static sem_t kb_sem;

void keyboard_init(void) {
    kb_head = 0;
    kb_tail = 0;
    shift_pressed = 0;
    ctrl_pressed = 0;
    alt_pressed = 0;
    caps_lock = 0;
    extended_key = 0;
    sem_init(&kb_sem, 0);
    irq_register_handler(1, keyboard_handler);
    pic_unmask(1);
}

void keyboard_handler(regs_t *regs) {
    (void)regs;
    uint8_t status = inb(0x64);

    if (status & 0x20) {
        inb(0x60);
        return;
    }

    uint8_t scancode = inb(0x60);

    if (scancode == 0xE0) {
        extended_key = 1;
        return;
    }

    keyboard_event_t event;
    event.scancode = scancode;
    event.flags = 0;
    event.ascii = 0;

    if (extended_key) {
        event.flags |= KEY_EXTENDED;
        extended_key = 0;
    }

    int released = (scancode & 0x80) != 0;
    if (released) {
        scancode &= 0x7F;
        event.scancode = scancode;
    } else {
        event.flags |= KEY_PRESSED;
    }

    if (event.flags & KEY_EXTENDED) {
        if (scancode == 0x1D) {
            ctrl_pressed = !released;
        } else if (scancode == 0x38) {
            alt_pressed = !released;
        }
    } else {
        if (scancode == 0x2A || scancode == 0x36) {
            shift_pressed = !released;
        } else if (scancode == 0x1D) {
            ctrl_pressed = !released;
        } else if (scancode == 0x38) {
            alt_pressed = !released;
        } else if (scancode == 0x3A && !released) {
            caps_lock = !caps_lock;
        }
    }

    if (shift_pressed) event.flags |= KEY_SHIFT;
    if (ctrl_pressed) event.flags |= KEY_CTRL;
    if (alt_pressed) event.flags |= KEY_ALT;
    if (caps_lock) event.flags |= KEY_CAPS;

    if (!released && scancode < 128) {
        if (shift_pressed) {
            event.ascii = key_map_shift[scancode];
        } else {
            event.ascii = key_map_normal[scancode];
        }
        if (caps_lock) {
            if (event.ascii >= 'a' && event.ascii <= 'z') {
                event.ascii -= 32;
            } else if (event.ascii >= 'A' && event.ascii <= 'Z') {
                event.ascii += 32;
            }
        }
    }

    uint32_t next_tail = (kb_tail + 1) % KEYBOARD_BUFFER_SIZE;
    if (next_tail != kb_head) {
        kb_buffer[kb_tail] = event;
        kb_tail = next_tail;
        sem_post(&kb_sem);
    }
}

/* Poll keyboard hardware directly (for when IRQs don't work).
 * Reads port 0x64 for status, 0x60 for data, processes scancode
 * and puts event into buffer. Returns 1 if key processed. */
int keyboard_poll(void) {
    uint8_t status = inb(0x64);
    if (!(status & 0x01)) {
        return 0;
    }

    if (status & 0x20) {
        inb(0x60);
        return 0;
    }

    uint8_t scancode = inb(0x60);

    if (scancode == 0xE0) {
        extended_key = 1;
        return 0;
    }

    keyboard_event_t event;
    event.scancode = scancode;
    event.flags = 0;
    event.ascii = 0;

    if (extended_key) {
        event.flags |= KEY_EXTENDED;
        extended_key = 0;
    }

    int released = (scancode & 0x80) != 0;
    if (released) {
        scancode &= 0x7F;
        event.scancode = scancode;
    } else {
        event.flags |= KEY_PRESSED;
    }

    if (event.flags & KEY_EXTENDED) {
        if (scancode == 0x1D) ctrl_pressed = !released;
        else if (scancode == 0x38) alt_pressed = !released;
    } else {
        if (scancode == 0x2A || scancode == 0x36) shift_pressed = !released;
        else if (scancode == 0x1D) ctrl_pressed = !released;
        else if (scancode == 0x38) alt_pressed = !released;
        else if (scancode == 0x3A && !released) caps_lock = !caps_lock;
    }

    if (shift_pressed) event.flags |= KEY_SHIFT;
    if (ctrl_pressed) event.flags |= KEY_CTRL;
    if (alt_pressed) event.flags |= KEY_ALT;
    if (caps_lock) event.flags |= KEY_CAPS;

    if (!released && scancode < 128) {
        if (shift_pressed) event.ascii = key_map_shift[scancode];
        else event.ascii = key_map_normal[scancode];
        if (caps_lock) {
            if (event.ascii >= 'a' && event.ascii <= 'z') event.ascii -= 32;
            else if (event.ascii >= 'A' && event.ascii <= 'Z') event.ascii += 32;
        }
    }

    uint32_t next_tail = (kb_tail + 1) % KEYBOARD_BUFFER_SIZE;
    if (next_tail != kb_head) {
        kb_buffer[kb_tail] = event;
        kb_tail = next_tail;
        return 1;
    }
    return 0;
}

void keyboard_wait(void) {
    sem_wait(&kb_sem);
}

int keyboard_get_event(keyboard_event_t *event) {
    if (kb_head == kb_tail) {
        return 0;
    }
    *event = kb_buffer[kb_head];
    kb_head = (kb_head + 1) % KEYBOARD_BUFFER_SIZE;
    return 1;
}

int keyboard_has_data(void) {
    return kb_head != kb_tail ? 1 : 0;
}

uint8_t keyboard_get_scancode(void) {
    return inb(0x60);
}

int keyboard_read_line(char *buf, uint32_t size) {
    if (size == 0) return 0;
    uint32_t pos = 0;
    while (1) {
        while (!keyboard_has_data()) {
            sem_wait(&kb_sem);
        }
        keyboard_event_t event;
        if (!keyboard_get_event(&event)) continue;
        if (!(event.flags & KEY_PRESSED)) continue;
        if (event.ascii == '\n') {
            if (is_vbe_mode()) {
                fb_console_putchar('\n');
            } else {
                vga_text_putchar('\n');
            }
            break;
        }
        if (event.ascii == '\b') {
            if (pos > 0) {
                pos--;
                if (is_vbe_mode()) {
                    fb_console_putchar('\b');
                } else {
                    vga_text_putchar('\b');
                }
            }
            continue;
        }
        if (pos < size - 1) {
            buf[pos] = event.ascii;
            pos++;
            if (is_vbe_mode()) {
                fb_console_putchar(event.ascii);
            } else {
                vga_text_putchar(event.ascii);
            }
        }
    }
    buf[pos] = '\0';
    return (int)pos;
}
