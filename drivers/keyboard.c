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

/* 全局信号标志 - 由键盘 ISR 设置，由 shell 检查 */
volatile int kb_sigint_pending = 0;
volatile int kb_sigquit_pending = 0;

int kb_signal_check(void) {
    if (kb_sigint_pending || kb_sigquit_pending) {
        kb_sigint_pending = 0;
        kb_sigquit_pending = 0;
        return 1;
    }
    return 0;
}

void kb_signal_clear(void) {
    kb_sigint_pending = 0;
    kb_sigquit_pending = 0;
}

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

    /* 若输出缓冲区为空, 说明数据已被 poll 路径取走, 直接返回避免读到无效数据 */
    if (!(status & 0x01)) {
        return;
    }

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
        /* Ctrl+C (scancode 0x2E) -> SIGINT, Ctrl+\ (scancode 0x2B) -> SIGQUIT */
        if (ctrl_pressed && scancode == 0x2E) {
            kb_sigint_pending = 1;
        }
        if (ctrl_pressed && scancode == 0x2B) {
            kb_sigquit_pending = 1;
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
static int keyboard_poll_locked(void) {
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
        /* Ctrl+C / Ctrl+\ 信号检测 */
        if (ctrl_pressed && scancode == 0x2E) kb_sigint_pending = 1;
        if (ctrl_pressed && scancode == 0x2B) kb_sigquit_pending = 1;
    }

    uint32_t next_tail = (kb_tail + 1) % KEYBOARD_BUFFER_SIZE;
    if (next_tail != kb_head) {
        kb_buffer[kb_tail] = event;
        kb_tail = next_tail;
        return 1;
    }
    return 0;
}

/* Wrapper: run the poll body with IF=0 so the IRQ1 handler cannot
 * interleave between the status check and the data read (which would
 * consume the byte first, leaving poll to read a stale duplicate from
 * port 0x60) or preempt the ring-buffer push mid-update.  The previous
 * IF state is restored on exit. */
int keyboard_poll(void) {
    uint32_t eflags;
    __asm__ volatile(
        "pushfl\n"
        "popl %0\n"
        "cli"
        : "=r"(eflags)
        :
        : "memory"
    );
    int ret = keyboard_poll_locked();
    if (eflags & 0x200) {
        __asm__ volatile("sti");
    }
    return ret;
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
