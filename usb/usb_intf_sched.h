#ifndef USB_INTF_SCHED_H
#define USB_INTF_SCHED_H

#include "stdint.h"

/* USB Interface Scheduler.
 *
 * Sits between USB class drivers (HID, storage, BT) and the host
 * controller scheduler. Each interface is opened by a class driver
 * and consumes a small amount of bandwidth. The interface scheduler:
 *   - Tracks open interfaces and their endpoint usage
 *   - Assigns each interface to a USB bandwidth bin (LS / FS / HS / SS)
 *   - Multiplexes class driver requests across endpoints
 *   - Forwards requests to the HC scheduler (usb_sched.h)
 *   - Hot-plug awareness: enumerates devices on a poll loop
 */

#define USB_INTF_MAX_CLASSES   8
#define USB_INTF_MAX_INTERFACES 128

/* Per-class driver registration. */
typedef struct usb_class_driver {
    const char *name;
    int  (*probe)(void *dev);   /* device enumerated */
    void (*disconnect)(void *dev);
    int  (*open)(void *dev);
    void (*close)(void *dev);
    struct usb_class_driver *next;
} usb_class_driver_t;

/* Per-device state. */
typedef struct usb_intf_device {
    uint8_t  address;
    uint8_t  speed;
    uint8_t  slot_id;
    uint16_t vendor;
    uint16_t product;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  protocol;
    uint8_t  in_ep_count;
    uint8_t  out_ep_count;
    uint16_t max_packet[16];
    uint8_t  ep_count;
    int      present;
    void    *class_data;
} usb_intf_device_t;

typedef struct {
    usb_class_driver_t *classes;
    usb_intf_device_t devices[USB_INTF_MAX_INTERFACES];
    int      n_devices;
    int      next_address;
    int      inited;
    uint64_t probes;
    uint64_t disconnects;
} usb_intf_sched_t;

int usb_intf_sched_init(usb_intf_sched_t *s);
int usb_intf_sched_register_class(usb_class_driver_t *c);
int usb_intf_sched_enumerate(usb_intf_sched_t *s,
                              void *hc,
                              int (*probe_fn)(void *, uint8_t, uint8_t *,
                                              uint16_t *, uint16_t *));
int usb_intf_sched_handle_event(usb_intf_sched_t *s,
                                int type, uint8_t addr);
const usb_intf_device_t *usb_intf_sched_get_device(int idx);

#endif