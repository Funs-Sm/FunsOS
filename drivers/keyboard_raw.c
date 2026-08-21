/* keyboard_raw.c - AT/PS2 keyboard driver (scancode set 2, raw).
 *
 * Set 2 is the native protocol for AT keyboards (and most modern PS/2
 * keyboards). In set 2, every key press sends the scancode, and every
 * release sends F0 followed by the scancode.
 *
 * We enable set 2 on the keyboard via the SET SCANCODES command (0xF0).
 */

#include "keyboard_raw.h"
#include "io.h"
#include "irq.h"
#include "kernel_types.h"
#define KB_PORT  0x60
#define KB_CMD_PORT 0x64

/* Keyboard commands */
#define KB_CMD_SET_SCANCODE_SET  0xF0
#define KB_CMD_ENABLE_SCAN       0xF4
#define KB_CMD_RESET             0xFF
#define KB_CMD_ACK               0xFA
#define KB_CMD_RESEND            0xFE

#define KB2_STATE_NORMAL         0
#define KB2_STATE_RELEASE_PREFIX 1
#define KB2_STATE_E0_PREFIX      2
#define KB2_STATE_RELEASE_E0     3

static kb2_event_t g_buffer[KB2_BUFFER_SIZE];
static volatile uint32_t g_head;
static volatile uint32_t g_tail;
static int g_state;
static int g_inited;

/* Wait for keyboard to accept a byte (the controller's input buffer must
 * be empty before we write to port 0x60). */
static void kb_wait_input(void) {
    int timeout = 100000;
    while (timeout--) {
        if ((inb(KB_CMD_PORT) & 0x02) == 0) return;
    }
}

/* Wait for keyboard to produce a response byte (output buffer full). */
static int kb_wait_output(int timeout_iters) {
    while (timeout_iters--) {
        if (inb(KB_CMD_PORT) & 0x01) return 0;
    }
    return -1;
}

static void kb_send(uint8_t cmd) {
    kb_wait_input();
    outb(KB_CMD_PORT, 0x60);  /* set command mode: next byte goes to controller */
    kb_wait_input();
    outb(KB_CMD_PORT, cmd);   /* emit the command */
}

static void kb_send_data(uint8_t data) {
    kb_wait_input();
    outb(KB_PORT, data);
}

static int kb_read_ack(int *ack) {
    uint8_t r;
    if (kb_wait_output(100000) != 0) return -1;
    r = inb(KB_PORT);
    if (r == KB_CMD_ACK) *ack = 1;
    else if (r == KB_CMD_RESEND) *ack = -1;
    else *ack = 0;
    return 0;
}

/* Switch the keyboard to scancode set 2. */
static int kb_select_set2(void) {
    int ack = 0;
    kb_send_data(KB_CMD_SET_SCANCODE_SET);
    if (kb_read_ack(&ack) != 0 || ack != 1) return -1;
    kb_send_data(0x02);  /* set 2 */
    if (kb_read_ack(&ack) != 0 || ack != 1) return -1;
    return 0;
}

static void kb_push(uint8_t scancode, int pressed) {
    uint32_t next = (g_tail + 1) % KB2_BUFFER_SIZE;
    if (next == g_head) return;  /* buffer full, drop event */
    g_buffer[g_tail].scancode = scancode;
    g_buffer[g_tail].pressed = (uint8_t)pressed;
    g_tail = next;
}

void kb2_init(void) {
    if (g_inited) return;
    g_head = 0;
    g_tail = 0;
    g_state = KB2_STATE_NORMAL;

    /* Enable keyboard and reset to a known state. */
    kb_send(0xAE);  /* enable keyboard interface */
    kb_send(KB_CMD_RESET);
    if (kb_wait_output(100000) == 0) {
        inb(KB_PORT);  /* consume BAT result */
    }

    kb_select_set2();
    kb_send_data(KB_CMD_ENABLE_SCAN);

    /* Hook the keyboard IRQ (IRQ1). */
    irq_register_handler(1, kb2_handler);
    pic_unmask(1);

    g_inited = 1;
}

void kb2_handler(regs_t *regs) {
    if (!g_inited) return;
    uint8_t sc = inb(KB_PORT);

    switch (g_state) {
    case KB2_STATE_NORMAL:
        if (sc == 0xF0) {
            g_state = KB2_STATE_RELEASE_PREFIX;
        } else if (sc == 0xE0) {
            g_state = KB2_STATE_E0_PREFIX;
        } else if (sc == 0xE1) {
            /* Pause key (E1 14 77 E1 F0 14 F0 77) - skip 7 bytes. */
            g_state = KB2_STATE_NORMAL;
            (void)inb(KB_PORT); (void)inb(KB_PORT); (void)inb(KB_PORT);
            (void)inb(KB_PORT); (void)inb(KB_PORT); (void)inb(KB_PORT);
        } else if (sc == 0x83) {
            /* Set 2 specific: some keyboards repeat F0 as 83. Treat as F0. */
            g_state = KB2_STATE_RELEASE_PREFIX;
        } else {
            kb_push(sc, 1);
        }
        break;

    case KB2_STATE_RELEASE_PREFIX:
        if (sc == 0xE0) {
            g_state = KB2_STATE_RELEASE_E0;
        } else if (sc != 0xE1 && sc != 0xF0) {
            kb_push(sc, 0);
            g_state = KB2_STATE_NORMAL;
        }
        break;

    case KB2_STATE_E0_PREFIX:
        if (sc == 0xF0) {
            g_state = KB2_STATE_RELEASE_E0;
        } else {
            kb_push((uint8_t)(0x80 | sc), 1);  /* mark extended */
            g_state = KB2_STATE_NORMAL;
        }
        break;

    case KB2_STATE_RELEASE_E0:
        if (sc != 0xF0 && sc != 0xE1) {
            kb_push((uint8_t)(0x80 | sc), 0);  /* mark extended */
            g_state = KB2_STATE_NORMAL;
        }
        break;
    }
}

int kb2_get_event(kb2_event_t *event) {
    if (g_head == g_tail) return 0;
    *event = g_buffer[g_head];
    g_head = (g_head + 1) % KB2_BUFFER_SIZE;
    return 1;
}

int kb2_has_data(void) {
    return g_head != g_tail ? 1 : 0;
}

/* Minimal set 1 translation table for the most common keys.
 * Returns the set 1 scancode; returns 0 for keys without a mapping. */
uint8_t kb2_to_set1(uint8_t sc2) {
    static const uint8_t map[128] = {
        [0x01] = 0x3B,  /* F9 -> F9 */
        [0x03] = 0x3F,  /* F5 */
        [0x04] = 0x3D,  /* F3 */
        [0x05] = 0x3B,  /* F1 */
        [0x06] = 0x3C,  /* F2 */
        [0x07] = 0x58,  /* F12 */
        [0x09] = 0x42,  /* F10 */
        [0x0A] = 0x40,  /* F8 */
        [0x0B] = 0x3E,  /* F6 */
        [0x0C] = 0x41,  /* F11 */
        [0x0D] = 0x43,  /* F7 */
        [0x0E] = 0x57,  /* F11? */
        [0x11] = 0x38,  /* Alt */
        [0x12] = 0x2A,  /* Shift */
        [0x14] = 0x1D,  /* Ctrl */
        [0x15] = 0x10,  /* Q */
        [0x16] = 0x02,  /* 1 */
        [0x1A] = 0x2C,  /* Z */
        [0x1B] = 0x1F,  /* S */
        [0x1C] = 0x1E,  /* A */
        [0x1D] = 0x11,  /* W */
        [0x1E] = 0x03,  /* 2 */
        [0x21] = 0x2E,  /* C */
        [0x22] = 0x20,  /* X */
        [0x23] = 0x2F,  /* D */
        [0x24] = 0x12,  /* E */
        [0x25] = 0x05,  /* 4 */
        [0x26] = 0x04,  /* 3 */
        [0x29] = 0x39,  /* Space */
        [0x2A] = 0x2D,  /* V */
        [0x2B] = 0x21,  /* F */
        [0x2C] = 0x14,  /* T */
        [0x2D] = 0x13,  /* R */
        [0x2E] = 0x06,  /* 5 */
        [0x31] = 0x31,  /* N */
        [0x32] = 0x23,  /* B */
        [0x33] = 0x22,  /* H */
        [0x34] = 0x15,  /* G */
        [0x35] = 0x1A,  /* Y */
        [0x36] = 0x07,  /* 6 */
        [0x3A] = 0x30,  /* M */
        [0x3B] = 0x24,  /* J */
        [0x3C] = 0x16,  /* U */
        [0x3D] = 0x08,  /* 7 */
        [0x3E] = 0x09,  /* 8 */
        [0x41] = 0x33,  /* , */
        [0x42] = 0x25,  /* K */
        [0x43] = 0x17,  /* I */
        [0x44] = 0x18,  /* O */
        [0x45] = 0x0B,  /* 0 */
        [0x46] = 0x0A,  /* 9 */
        [0x49] = 0x34,  /* . */
        [0x4A] = 0x35,  /* / */
        [0x4B] = 0x26,  /* L */
        [0x4C] = 0x27,  /* ; */
        [0x4D] = 0x19,  /* P */
        [0x4E] = 0x0C,  /* - */
        [0x52] = 0x28,  /* ' */
        [0x54] = 0x1B,  /* [ */
        [0x55] = 0x0D,  /* = */
        [0x58] = 0x3A,  /* CapsLock */
        [0x59] = 0x36,  /* Shift right */
        [0x5A] = 0x1C,  /* Enter */
        [0x5B] = 0x1A,  /* ] */
        [0x5D] = 0x2B,  /* \ */
        [0x66] = 0x0E,  /* Backspace */
        [0x69] = 0x4F,  /* End (numeric 1) */
        [0x6B] = 0x4B,  /* Left arrow */
        [0x6C] = 0x47,  /* Home */
        [0x70] = 0x52,  /* Insert */
        [0x71] = 0x53,  /* Delete */
        [0x72] = 0x50,  /* Down arrow */
        [0x74] = 0x4D,  /* Right arrow */
        [0x75] = 0x48,  /* Up arrow */
        [0x76] = 0x01,  /* Esc */
        [0x77] = 0x45,  /* NumLock */
        [0x78] = 0x57,  /* F11 */
        [0x79] = 0x4E,  /* KP+ */
        [0x7A] = 0x51,  /* PageDown */
        [0x7B] = 0x4A,  /* KP- */
        [0x7C] = 0x37,  /* KP* */
        [0x7D] = 0x49,  /* PageUp */
        [0x7E] = 0x46,  /* ScrollLock */
    };
    if (sc2 & 0x80) return 0;  /* extended: caller should handle separately */
    return map[sc2 & 0x7F];
}