#include "usb_hid.h"
#include "usb_core.h"
#include "kheap.h"
#include "string.h"

static usb_hid_device_t *hid_devices[16];
static uint32_t hid_count = 0;

/* USB HID Boot Protocol keyboard scan code to ASCII mapping (US layout) */
static const char kbd_key_map[128] = {
    0, 0, 0, 0, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',
    'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
    '\n', 0, '\b', '\t', ' ', '-', '=', '[', ']', '\\', 0, ';', '\'', '`', ',', '.', '/'
};

static const char kbd_shift_key_map[128] = {
    0, 0, 0, 0, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
    'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    '!', '@', '#', '$', '%', '^', '&', '*', '(', ')',
    '\n', 0, '\b', '\t', ' ', '_', '+', '{', '}', '|', 0, ':', '"', '~', '<', '>', '?'
};

/* Modifier key bit masks in boot protocol byte 0 */
#define KBD_MOD_LCTRL   0x01
#define KBD_MOD_LSHIFT  0x02
#define KBD_MOD_ALT     0x04
#define KBD_MOD_LGUI    0x08
#define KBD_MOD_RCTRL   0x10
#define KBD_MOD_RSHIFT  0x20
#define KBD_MOD_RALT    0x40
#define KBD_MOD_RGUI    0x80

void usb_hid_init(void) {
    for (uint8_t addr = 1; addr < USB_MAX_DEVICES; addr++) {
        usb_device_t *dev = usb_get_device(addr);
        if (!dev) continue;
        if (dev->class_code == 0x03) {
            usb_hid_probe(dev);
        }
    }
}

int usb_hid_probe(usb_device_t *dev) {
    if (hid_count >= 16) return -1;

    usb_hid_device_t *hid = (usb_hid_device_t *)kmalloc(sizeof(usb_hid_device_t));
    memset(hid, 0, sizeof(usb_hid_device_t));
    hid->dev_addr = dev->address;
    hid->interface = 0;
    hid->ep_in = 0x81;
    hid->report_size = 8;
    hid->subtype = USB_HID_SUBTYPE_UNKNOWN;

    /* Determine subtype from interface protocol:
       0=none, 1=keyboard, 2=mouse */
    if (dev->protocol == 1) {
        hid->subtype = USB_HID_SUBTYPE_KEYBOARD;
        hid->report_size = 8;  /* Boot keyboard: 8 bytes */
        usb_hid_init_keyboard(dev->address);
        hid->callback = usb_hid_keyboard_irq;
    } else if (dev->protocol == 2) {
        hid->subtype = USB_HID_SUBTYPE_MOUSE;
        hid->report_size = 4;  /* Boot mouse: 3-4 bytes */
        usb_hid_init_mouse(dev->address);
        hid->callback = usb_hid_mouse_irq;
    } else {
        /* Generic HID - set boot protocol as keyboard by default */
        usb_hid_set_protocol(dev->address, 0, USB_HID_BOOT_PROTOCOL);
        usb_hid_set_idle(dev->address, 0, 0);
    }

    hid_devices[hid_count++] = hid;
    return 0;
}

int usb_hid_set_protocol(uint8_t dev_addr, uint8_t iface, uint8_t protocol) {
    return usb_control_transfer(dev_addr, 0x21, 0x0B, protocol, iface, 0, 0);
}

int usb_hid_set_idle(uint8_t dev_addr, uint8_t iface, uint8_t duration) {
    return usb_control_transfer(dev_addr, 0x21, 0x0A, (duration << 8), iface, 0, 0);
}

int usb_hid_poll(usb_hid_device_t *hid) {
    memset(hid->report, 0, sizeof(hid->report));
    int ret = usb_interrupt_transfer(hid->dev_addr, hid->ep_in, hid->report, hid->report_size);
    if (ret == 0 && hid->callback) {
        hid->callback(hid->report, hid->report_size);
    }
    return ret;
}

void usb_hid_remove(uint8_t dev_addr) {
    for (uint32_t i = 0; i < hid_count; i++) {
        if (hid_devices[i] && hid_devices[i]->dev_addr == dev_addr) {
            kfree(hid_devices[i]);
            hid_devices[i] = (void *)0;
            /* Compact the array: shift remaining entries down */
            for (uint32_t j = i; j < hid_count - 1; j++) {
                hid_devices[j] = hid_devices[j + 1];
            }
            hid_devices[hid_count - 1] = (void *)0;
            hid_count--;
            return;
        }
    }
}

/* ---- Keyboard IRQ handler ---- */

void usb_hid_keyboard_irq(uint8_t *report, uint32_t len) {
    if (len < 3) return;

    /* Find the keyboard HID device */
    usb_hid_device_t *kbd = 0;
    for (uint32_t i = 0; i < hid_count; i++) {
        if (hid_devices[i] && hid_devices[i]->subtype == USB_HID_SUBTYPE_KEYBOARD) {
            kbd = hid_devices[i];
            break;
        }
    }
    if (!kbd) return;

    /* Boot protocol keyboard report:
       byte 0: modifier keys (Ctrl/Shift/Alt/GUI)
       byte 1: reserved
       bytes 2-7: key codes (up to 6 simultaneous keys) */
    uint8_t modifiers = report[0];
    kbd->kbd_modifiers = modifiers;

    /* Process each key code in the report */
    for (uint32_t i = 2; i < len && i < 8; i++) {
        uint8_t key_code = report[i];
        if (key_code == 0) continue;

        /* Skip modifier-only keys (codes 0xE0-0xE7) */
        if (key_code >= 0xE0 && key_code <= 0xE7) continue;

        /* Convert key code to ASCII */
        char ascii = 0;
        int shifted = (modifiers & (KBD_MOD_LSHIFT | KBD_MOD_RSHIFT)) != 0;

        if (key_code < 128) {
            if (shifted) {
                ascii = kbd_shift_key_map[key_code];
            } else {
                ascii = kbd_key_map[key_code];
            }
        }

        /* Handle Ctrl key combinations */
        if ((modifiers & (KBD_MOD_LCTRL | KBD_MOD_RCTRL)) && ascii >= 'a' && ascii <= 'z') {
            ascii = ascii - 'a' + 1;  /* Ctrl+A = 0x01, etc. */
        }

        /* Store in circular buffer */
        if (ascii != 0) {
            uint32_t next_head = (kbd->kbd_buffer_head + 1) % USB_HID_KBD_BUFFER_SIZE;
            if (next_head != kbd->kbd_buffer_tail) {
                kbd->kbd_buffer[kbd->kbd_buffer_head] = (uint8_t)ascii;
                kbd->kbd_buffer_head = next_head;
            }
        }
    }
}

/* ---- Mouse IRQ handler ---- */

void usb_hid_mouse_irq(uint8_t *report, uint32_t len) {
    if (len < 3) return;

    /* Find the mouse HID device */
    usb_hid_device_t *mse = 0;
    for (uint32_t i = 0; i < hid_count; i++) {
        if (hid_devices[i] && hid_devices[i]->subtype == USB_HID_SUBTYPE_MOUSE) {
            mse = hid_devices[i];
            break;
        }
    }
    if (!mse) return;

    /* Boot protocol mouse report:
       byte 0: button states (bit0=left, bit1=right, bit2=middle)
       byte 1: X delta (signed)
       byte 2: Y delta (signed) */
    mse->mouse_event.buttons = report[0] & 0x07;
    mse->mouse_event.dx = (int8_t)report[1];
    mse->mouse_event.dy = (int8_t)report[2];
}

/* ---- Keyboard initialization ---- */

int usb_hid_init_keyboard(uint8_t dev_addr) {
    /* Set boot protocol (protocol = 0) */
    if (usb_hid_set_protocol(dev_addr, 0, USB_HID_BOOT_PROTOCOL) != 0) {
        return -1;
    }

    /* Set idle rate to 0 (send report only on change) */
    usb_hid_set_idle(dev_addr, 0, 0);

    /* The interrupt endpoint is already set up during enumeration.
       The keyboard will send data on the interrupt IN endpoint. */
    return 0;
}

/* ---- Mouse initialization ---- */

int usb_hid_init_mouse(uint8_t dev_addr) {
    /* Set boot protocol for mouse */
    if (usb_hid_set_protocol(dev_addr, 0, USB_HID_BOOT_PROTOCOL) != 0) {
        return -1;
    }

    /* Set idle rate to 0 */
    usb_hid_set_idle(dev_addr, 0, 0);

    return 0;
}

/* ---- Keyboard buffer access ---- */

int usb_hid_keyboard_has_data(void) {
    for (uint32_t i = 0; i < hid_count; i++) {
        if (hid_devices[i] && hid_devices[i]->subtype == USB_HID_SUBTYPE_KEYBOARD) {
            return hid_devices[i]->kbd_buffer_head != hid_devices[i]->kbd_buffer_tail;
        }
    }
    return 0;
}

int usb_hid_keyboard_get_char(void) {
    for (uint32_t i = 0; i < hid_count; i++) {
        if (hid_devices[i] && hid_devices[i]->subtype == USB_HID_SUBTYPE_KEYBOARD) {
            usb_hid_device_t *kbd = hid_devices[i];
            if (kbd->kbd_buffer_head != kbd->kbd_buffer_tail) {
                uint8_t ch = kbd->kbd_buffer[kbd->kbd_buffer_tail];
                kbd->kbd_buffer_tail = (kbd->kbd_buffer_tail + 1) % USB_HID_KBD_BUFFER_SIZE;
                return (int)ch;
            }
        }
    }
    return -1;
}

/* ---- Mouse event access ---- */

int usb_hid_mouse_has_event(void) {
    for (uint32_t i = 0; i < hid_count; i++) {
        if (hid_devices[i] && hid_devices[i]->subtype == USB_HID_SUBTYPE_MOUSE) {
            /* Has event if dx or dy is non-zero, or buttons changed */
            usb_hid_mouse_event_t *evt = &hid_devices[i]->mouse_event;
            return (evt->dx != 0 || evt->dy != 0 || evt->buttons != 0);
        }
    }
    return 0;
}

int usb_hid_mouse_get_event(usb_hid_mouse_event_t *evt) {
    if (!evt) return -1;

    for (uint32_t i = 0; i < hid_count; i++) {
        if (hid_devices[i] && hid_devices[i]->subtype == USB_HID_SUBTYPE_MOUSE) {
            evt->dx = hid_devices[i]->mouse_event.dx;
            evt->dy = hid_devices[i]->mouse_event.dy;
            evt->buttons = hid_devices[i]->mouse_event.buttons;

            /* Clear the event after reading */
            hid_devices[i]->mouse_event.dx = 0;
            hid_devices[i]->mouse_event.dy = 0;
            return 0;
        }
    }
    return -1;
}

/* ---- Poll all HID devices ---- */

void usb_hid_poll_all(void) {
    for (uint32_t i = 0; i < hid_count; i++) {
        if (hid_devices[i]) {
            usb_hid_poll(hid_devices[i]);
        }
    }
}
