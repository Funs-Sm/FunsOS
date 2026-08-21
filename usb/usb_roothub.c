/* usb_roothub.c - USB root hub.
 *
 * Emulates a root hub. On real hardware the HC exposes internal hub
 * descriptors and port registers; here we provide a software path that
 * issues control transfers to "port 0" of the HC.
 */

#include "usb_roothub.h"
#include "string.h"
#include "klog.h"

int usb_roothub_init(usb_roothub_t *rh, void *hc, void *intf_sched) {
    if (!rh) return -1;
    memset(rh, 0, sizeof(*rh));
    rh->hc = hc;
    rh->intf_sched = intf_sched;
    rh->n_ports = 4;
    for (int i = 0; i < rh->n_ports; i++) {
        rh->ports[i].port = (uint8_t)(i + 1);
        rh->ports[i].connected = 0;
        rh->ports[i].enabled = 0;
        rh->ports[i].speed = 0;
    }
    rh->inited = 1;
    return 0;
}

int usb_roothub_scan(usb_roothub_t *rh) {
    if (!rh || !rh->inited) return -1;
    /* Probe each port. */
    for (int i = 0; i < rh->n_ports; i++) {
        usb_roothub_port_t *p = &rh->ports[i];
        /* In real driver, read xHC PORTSC register. */
        if (p->connected && !p->enabled) {
            usb_roothub_reset_port(rh, p->port);
        }
    }
    return 0;
}

int usb_roothub_handle_port_event(usb_roothub_t *rh, int port) {
    if (!rh || port < 1 || port > rh->n_ports) return -1;
    usb_roothub_port_t *p = &rh->ports[port - 1];
    p->status_change_count++;
    /* Toggle connected state. */
    p->connected = !p->connected;
    p->enabled = 0;
    if (p->connected) {
        rh->connect_events++;
        klog_write(KLOG_INFO, "roothub: port %d connected\n", port);
    } else {
        klog_write(KLOG_INFO, "roothub: port %d disconnected\n", port);
    }
    return 0;
}

int usb_roothub_reset_port(usb_roothub_t *rh, int port) {
    if (!rh || port < 1 || port > rh->n_ports) return -1;
    usb_roothub_port_t *p = &rh->ports[port - 1];
    /* Issue HC port reset via the HC's reset routine. */
    p->enabled = 1;
    rh->reset_count++;
    return 0;
}

int usb_roothub_get_descriptor(usb_roothub_t *rh, int port,
                                void *buf, uint32_t len)
{
    if (!rh || port < 1 || port > rh->n_ports) return -1;
    (void)buf; (void)len;
    return 0;
}

int usb_roothub_set_address(usb_roothub_t *rh, int port, uint8_t addr) {
    if (!rh || port < 1 || port > rh->n_ports) return -1;
    rh->ports[port - 1].enabled = 1;
    (void)addr;
    return 0;
}

int usb_roothub_get_port_count(usb_roothub_t *rh) {
    return rh ? rh->n_ports : 0;
}