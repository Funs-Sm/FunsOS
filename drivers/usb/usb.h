#ifndef USB_H
#define USB_H

#include "stdint.h"

#define USB_MAX_DEVICES 128
#define USB_MAX_ENDPOINTS 32

/* Forward declaration - usb_core.h has the full definition */
struct usb_device;
typedef struct usb_device usb_device_t;

/* Hub-specific control message API (implemented in usb/usb_core.c) */
int32_t usb_control_msg(usb_device_t *dev, uint8_t request_type, uint8_t request, uint16_t value, uint16_t index, void *buf, uint16_t len);

#endif
