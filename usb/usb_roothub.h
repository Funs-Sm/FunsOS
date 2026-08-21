#ifndef USB_ROOTHUB_H
#define USB_ROOTHUB_H

#include "stdint.h"

/* USB Root Hub emulator.
 *
 * Implements a virtual root hub that bridges the host controller
 * scheduler with the kernel's USB class drivers. The root hub:
 *   - Tracks port status (powered, connected, enabled, suspended)
 *   - Detects hot-plug events via the HC's port status registers
 *   - Notifies the interface scheduler of new / removed devices
 *   - Issues port reset on a connect event
 *   - Provides a simple control interface (GET_DESCRIPTOR, SET_FEATURE)
 */

#define USB_ROOTHUB_MAX_PORTS 16

typedef struct {
    uint8_t  port;
    uint8_t  connected;
    uint8_t  enabled;
    uint8_t  speed;
    uint32_t status_change_count;
} usb_roothub_port_t;

typedef struct {
    usb_roothub_port_t ports[USB_ROOTHUB_MAX_PORTS];
    int n_ports;
    int inited;
    void *hc;  /* host controller instance */
    void *intf_sched;
    uint64_t reset_count;
    uint64_t connect_events;
} usb_roothub_t;

int  usb_roothub_init(usb_roothub_t *rh, void *hc, void *intf_sched);
int  usb_roothub_scan(usb_roothub_t *rh);
int  usb_roothub_handle_port_event(usb_roothub_t *rh, int port);
int  usb_roothub_reset_port(usb_roothub_t *rh, int port);
int  usb_roothub_get_descriptor(usb_roothub_t *rh, int port,
                                 void *buf, uint32_t len);
int  usb_roothub_set_address(usb_roothub_t *rh, int port, uint8_t addr);
int  usb_roothub_get_port_count(usb_roothub_t *rh);

#endif