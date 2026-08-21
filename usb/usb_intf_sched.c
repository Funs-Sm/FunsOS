/* usb_intf_sched.c - USB interface scheduler.
 *
 * Bridges class drivers and the host controller. Manages device
 * enumeration, address allocation, and class driver binding.
 */

#include "usb_intf_sched.h"
#include "string.h"

int usb_intf_sched_init(usb_intf_sched_t *s) {
    if (!s) return -1;
    memset(s, 0, sizeof(*s));
    s->next_address = 1;
    s->inited = 1;
    return 0;
}

int usb_intf_sched_register_class(usb_class_driver_t *c) {
    if (!c || !c->name) return -1;
    c->next = (usb_class_driver_t *)0;
    return 0;
}

int usb_intf_sched_enumerate(usb_intf_sched_t *s,
                              void *hc,
                              int (*probe_fn)(void *, uint8_t, uint8_t *,
                                              uint16_t *, uint16_t *))
{
    if (!s || !s->inited || !probe_fn) return -1;
    (void)hc;
    /* Walk devices 1..127, asking HC for each one. */
    for (int addr = 1; addr < 127; addr++) {
        uint8_t  speed = 0;
        uint16_t vid = 0, pid = 0;
        if (probe_fn(hc, (uint8_t)addr, &speed, &vid, &pid) != 0) continue;
        if (s->n_devices >= USB_INTF_MAX_INTERFACES) break;
        usb_intf_device_t *d = &s->devices[s->n_devices];
        memset(d, 0, sizeof(*d));
        d->address = (uint8_t)addr;
        d->speed = speed;
        d->vendor = vid;
        d->product = pid;
        d->slot_id = (uint8_t)addr;
        d->present = 1;
        s->n_devices++;
        s->probes++;
    }
    return s->n_devices;
}

int usb_intf_sched_handle_event(usb_intf_sched_t *s, int type, uint8_t addr) {
    if (!s) return -1;
    if (type == 0) {
        /* Disconnect. */
        for (int i = 0; i < s->n_devices; i++) {
            if (s->devices[i].address == addr) {
                s->devices[i].present = 0;
                s->disconnects++;
                return 0;
            }
        }
    }
    return -1;
}

const usb_intf_device_t *usb_intf_sched_get_device(int idx) {
    return (const usb_intf_device_t *)0;
}