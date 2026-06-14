#include "usb_core.h"
#include "xhci.h"
#include "usb_hid.h"
#include "usb_storage.h"
#include "kheap.h"
#include "string.h"

static usb_device_t *device_list = 0;
static uint8_t next_address = 1;

int usb_init(void) {
    if (xhci_init() != 0) return -1;
    return usb_enumerate();
}

int usb_enumerate(void) {
    for (int port = 0; port < 16; port++) {
        uint8_t slot_id;
        if (xhci_enable_slot(&slot_id) != 0) continue;

        usb_device_t *dev = (usb_device_t *)kmalloc(sizeof(usb_device_t));
        memset(dev, 0, sizeof(usb_device_t));
        dev->slot_id = slot_id;
        dev->address = next_address++;
        dev->speed = USB_SPEED_SUPER;
        dev->hub_port = port;

        void *input_ctx = kmalloc(4096);
        memset(input_ctx, 0, 4096);

        if (xhci_address_device(slot_id, input_ctx) != 0) {
            kfree(input_ctx);
            kfree(dev);
            continue;
        }

        usb_desc_device_t dev_desc;
        memset(&dev_desc, 0, sizeof(usb_desc_device_t));
        if (usb_control_transfer(dev->address, 0x80, 0x06, 0x0100, 0, &dev_desc, sizeof(usb_desc_device_t)) != 0) {
            kfree(input_ctx);
            kfree(dev);
            continue;
        }

        dev->vendor_id = dev_desc.idVendor;
        dev->device_id = dev_desc.idProduct;
        dev->class_code = dev_desc.bDeviceClass;
        dev->subclass = dev_desc.bDeviceSubClass;
        dev->protocol = dev_desc.bDeviceProtocol;

        if (usb_control_transfer(dev->address, 0x00, 0x05, dev->address, 0, 0, 0) != 0) {
            kfree(input_ctx);
            kfree(dev);
            continue;
        }

        usb_desc_config_t cfg_desc;
        memset(&cfg_desc, 0, sizeof(usb_desc_config_t));
        usb_control_transfer(dev->address, 0x80, 0x06, 0x0200, 0, &cfg_desc, sizeof(usb_desc_config_t));

        uint8_t *full_cfg = (uint8_t *)kmalloc(cfg_desc.wTotalLength);
        usb_control_transfer(dev->address, 0x80, 0x06, 0x0200, 0, full_cfg, cfg_desc.wTotalLength);

        usb_control_transfer(dev->address, 0x00, 0x09, 1, 0, 0, 0);

        dev->next = device_list;
        device_list = dev;

        kfree(full_cfg);
        kfree(input_ctx);
    }
    return 0;
}

usb_device_t *usb_get_device(uint8_t addr) {
    usb_device_t *cur = device_list;
    while (cur) {
        if (cur->address == addr) return cur;
        cur = cur->next;
    }
    return 0;
}

int usb_control_transfer(uint8_t dev_addr, uint8_t request_type, uint8_t request, uint16_t value, uint16_t index, void *data, uint16_t length) {
    usb_device_t *dev = usb_get_device(dev_addr);
    if (!dev) return -1;

    xhci_trb_t trbs[3];

    uint8_t setup[8];
    setup[0] = request_type;
    setup[1] = request;
    setup[2] = value & 0xFF;
    setup[3] = (value >> 8) & 0xFF;
    setup[4] = index & 0xFF;
    setup[5] = (index >> 8) & 0xFF;
    setup[6] = length & 0xFF;
    setup[7] = (length >> 8) & 0xFF;

    trbs[0].parameter = (uint32_t)setup;
    trbs[0].status = 8;
    trbs[0].control = (3 << 16) | (2 << 10);

    if (length > 0) {
        trbs[1].parameter = (uint32_t)data;
        trbs[1].status = length;
        uint32_t dir = (request_type & 0x80) ? (3 << 16) : (2 << 16);
        trbs[1].control = dir | (1 << 10);
    }

    trbs[2].parameter = 0;
    trbs[2].status = 0;
    uint32_t dir2 = (request_type & 0x80) ? (4 << 16) : (2 << 16);
    trbs[2].control = dir2 | (1 << 10);

    uint32_t trb_count = (length > 0) ? 3 : 2;
    return xhci_transfer(dev->slot_id, 1, trbs, trb_count);
}

int usb_bulk_transfer(uint8_t dev_addr, uint8_t ep, void *data, uint32_t len) {
    usb_device_t *dev = usb_get_device(dev_addr);
    if (!dev) return -1;

    xhci_trb_t trb;
    trb.parameter = (uint32_t)data;
    trb.status = len;
    uint32_t dir = (ep & 0x80) ? (3 << 16) : (2 << 16);
    trb.control = dir | (1 << 10);

    return xhci_transfer(dev->slot_id, ep & 0x0F, &trb, 1);
}

int usb_interrupt_transfer(uint8_t dev_addr, uint8_t ep, void *data, uint32_t len) {
    usb_device_t *dev = usb_get_device(dev_addr);
    if (!dev) return -1;

    xhci_trb_t trb;
    trb.parameter = (uint32_t)data;
    trb.status = len;
    uint32_t dir = (ep & 0x80) ? (3 << 16) : (2 << 16);
    trb.control = dir | (1 << 10);

    return xhci_transfer(dev->slot_id, ep & 0x0F, &trb, 1);
}

/*
 * Enumerate a single USB device at the given address.
 * Reads device descriptor, sets address, reads config descriptor,
 * and configures the device. Called after a new device is detected
 * via hot-plug polling.
 * Returns 0 on success, -1 on failure.
 */
int usb_enumerate_device(uint8_t addr) {
    usb_device_t *dev = usb_get_device(addr);
    if (!dev) return -1;

    usb_desc_device_t dev_desc;
    memset(&dev_desc, 0, sizeof(usb_desc_device_t));
    if (usb_control_transfer(addr, 0x80, 0x06, 0x0100, 0, &dev_desc, sizeof(usb_desc_device_t)) != 0) {
        return -1;
    }

    dev->vendor_id = dev_desc.idVendor;
    dev->device_id = dev_desc.idProduct;
    dev->class_code = dev_desc.bDeviceClass;
    dev->subclass = dev_desc.bDeviceSubClass;
    dev->protocol = dev_desc.bDeviceProtocol;

    if (usb_control_transfer(addr, 0x00, 0x05, addr, 0, 0, 0) != 0) {
        return -1;
    }

    usb_desc_config_t cfg_desc;
    memset(&cfg_desc, 0, sizeof(usb_desc_config_t));
    usb_control_transfer(addr, 0x80, 0x06, 0x0200, 0, &cfg_desc, sizeof(usb_desc_config_t));

    uint8_t *full_cfg = (uint8_t *)kmalloc(cfg_desc.wTotalLength);
    if (full_cfg) {
        usb_control_transfer(addr, 0x80, 0x06, 0x0200, 0, full_cfg, cfg_desc.wTotalLength);
        kfree(full_cfg);
    }

    usb_control_transfer(addr, 0x00, 0x09, 1, 0, 0, 0);

    return 0;
}

/*
 * Remove a USB device by address.
 * Cleans up HID or storage subsystems, then removes from device list
 * and frees the device structure.
 */
void usb_device_remove(uint8_t addr) {
    usb_device_t *cur = device_list;
    usb_device_t *prev = 0;

    while (cur) {
        if (cur->address == addr) {
            /* Found the device - clean up subsystem-specific state */
            if (cur->class_code == 0x03) {
                /* HID device */
                usb_hid_remove(addr);
            } else if (cur->class_code == 0x08) {
                /* Mass storage device */
                usb_storage_remove(cur);
            }

            /* Remove from linked list */
            if (prev) {
                prev->next = cur->next;
            } else {
                device_list = cur->next;
            }

            kfree(cur);
            return;
        }
        prev = cur;
        cur = cur->next;
    }
}

/*
 * Periodic hot-plug poll function.
 * Checks xHCI port status changes and handles new connections/disconnections.
 * Should be called approximately every 500ms.
 */
void usb_hotplug_poll(void) {
    xhci_port_event_t events[32];
    int count = xhci_check_ports(events, 32);

    for (int i = 0; i < count; i++) {
        if (events[i].connected) {
            /* New device connected */
            xhci_handle_port_change();

            /* Create a new device entry and enumerate it */
            uint8_t slot_id;
            if (xhci_enable_slot(&slot_id) != 0) continue;

            usb_device_t *dev = (usb_device_t *)kmalloc(sizeof(usb_device_t));
            if (!dev) continue;
            memset(dev, 0, sizeof(usb_device_t));
            dev->slot_id = slot_id;
            dev->address = next_address++;
            dev->speed = USB_SPEED_SUPER;
            dev->hub_port = events[i].port;

            void *input_ctx = kmalloc(4096);
            if (!input_ctx) {
                kfree(dev);
                continue;
            }
            memset(input_ctx, 0, 4096);

            if (xhci_address_device(slot_id, input_ctx) != 0) {
                kfree(input_ctx);
                kfree(dev);
                continue;
            }

            dev->next = device_list;
            device_list = dev;

            /* Full enumeration: read descriptors, configure */
            usb_enumerate_device(dev->address);

            /* Probe for class-specific drivers */
            if (dev->class_code == 0x03) {
                usb_hid_probe(dev);
            } else if (dev->class_code == 0x08) {
                usb_storage_probe(dev);
            }

            kfree(input_ctx);
        } else {
            /* Device disconnected - find device by port and remove */
            usb_device_t *cur = device_list;
            while (cur) {
                if (cur->hub_port == events[i].port) {
                    usb_device_remove(cur->address);
                    break;
                }
                cur = cur->next;
            }
        }
    }
}
