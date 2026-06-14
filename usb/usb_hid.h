#ifndef USB_HID_H
#define USB_HID_H

#include "stdint.h"
#include "usb_core.h"

#define USB_HID_BOOT_PROTOCOL   0
#define USB_HID_REPORT_PROTOCOL 1

/* HID Set Protocol request */
#define HID_SET_PROTOCOL  0x0B
#define HID_GET_PROTOCOL  0x03
#define HID_SET_IDLE      0x0A
#define HID_SET_REPORT    0x09
#define HID_GET_REPORT    0x01

/* HID Report types */
#define HID_REPORT_INPUT   1
#define HID_REPORT_OUTPUT  2
#define HID_REPORT_FEATURE 3

/* Keyboard input buffer size */
#define USB_HID_KBD_BUFFER_SIZE 256

/* Mouse event for USB HID mouse */
typedef struct {
    int8_t dx;
    int8_t dy;
    uint8_t buttons;   /* bit0=left, bit1=right, bit2=middle */
} usb_hid_mouse_event_t;

typedef struct {
    uint8_t dev_addr;
    uint8_t interface;
    uint8_t ep_in;
    uint8_t report_size;
    uint8_t report[64];
    void (*callback)(uint8_t *report, uint32_t len);

    /* HID subtype: 0=unknown, 1=keyboard, 2=mouse */
    uint8_t subtype;

    /* Keyboard state */
    uint8_t kbd_buffer[USB_HID_KBD_BUFFER_SIZE];
    uint32_t kbd_buffer_head;
    uint32_t kbd_buffer_tail;
    uint8_t kbd_modifiers;  /* Current modifier key state */

    /* Mouse state */
    usb_hid_mouse_event_t mouse_event;
} usb_hid_device_t;

/* HID device subtypes */
#define USB_HID_SUBTYPE_UNKNOWN  0
#define USB_HID_SUBTYPE_KEYBOARD 1
#define USB_HID_SUBTYPE_MOUSE    2

void usb_hid_init(void);
int usb_hid_probe(usb_device_t *dev);
int usb_hid_set_protocol(uint8_t dev_addr, uint8_t iface, uint8_t protocol);
int usb_hid_set_idle(uint8_t dev_addr, uint8_t iface, uint8_t duration);
int usb_hid_poll(usb_hid_device_t *hid);
void usb_hid_remove(uint8_t dev_addr);

/* New keyboard/mouse functions */
void usb_hid_keyboard_irq(uint8_t *report, uint32_t len);
void usb_hid_mouse_irq(uint8_t *report, uint32_t len);
int usb_hid_init_keyboard(uint8_t dev_addr);
int usb_hid_init_mouse(uint8_t dev_addr);

/* Keyboard buffer access */
int usb_hid_keyboard_has_data(void);
int usb_hid_keyboard_get_char(void);

/* Mouse event access */
int usb_hid_mouse_has_event(void);
int usb_hid_mouse_get_event(usb_hid_mouse_event_t *evt);

/* Poll all HID devices */
void usb_hid_poll_all(void);

#endif
