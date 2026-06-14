#include "usb_hub.h"
#include "usb_core.h"
#include "kheap.h"
#include "string.h"
#include "stdio.h"
#include "timer.h"

static usb_hub_t *hub_list = 0;
static usb_hub_t *root_hub = 0;
static uint8_t hub_count = 0;
static uint32_t global_tick = 0;

void usb_hub_init(void) {
    hub_list = 0;
    root_hub = 0;
    hub_count = 0;
    global_tick = 0;
}

int32_t usb_hub_get_descriptor(usb_device_t *dev, usb_hub_desc_t *desc) {
    if (!dev || !desc) return -1;

    memset(desc, 0, sizeof(usb_hub_desc_t));
    int32_t len = usb_control_msg(dev,
        0xA0, USB_REQ_GET_DESCRIPTOR,
        (USB_DT_HUB << 8), 0,
        desc, sizeof(usb_hub_desc_t));

    if (len < 0) return -1;
    return 0;
}

int32_t usb_hub_set_port_feature(usb_device_t *dev, uint8_t port, uint16_t feature) {
    if (!dev) return -1;
    int32_t ret = usb_control_msg(dev,
        0x23, USB_REQ_SET_FEATURE,
        feature, port + 1,
        0, 0);
    return ret;
}

int32_t usb_hub_clear_port_feature(usb_device_t *dev, uint8_t port, uint16_t feature) {
    if (!dev) return -1;
    int32_t ret = usb_control_msg(dev,
        0x23, USB_REQ_CLEAR_FEATURE,
        feature, port + 1,
        0, 0);
    return ret;
}

int32_t usb_hub_get_port_status(usb_device_t *dev, uint8_t port, uint16_t *status, uint16_t *change) {
    if (!dev || !status) return -1;

    uint32_t buf;
    int32_t ret = usb_control_msg(dev,
        0xA3, USB_REQ_GET_STATUS,
        0, port + 1,
        &buf, 4);

    if (ret < 0) return -1;

    *status = (uint16_t)(buf & 0xFFFF);
    if (change) *change = (uint16_t)((buf >> 16) & 0xFFFF);
    return 0;
}

int32_t usb_hub_port_power(usb_hub_t *hub, uint8_t port, uint8_t on) {
    if (!hub || port >= hub->num_ports) return -1;

    if (on) {
        if (usb_hub_set_port_feature(hub->usb_dev, port, USB_FEAT_PORT_POWER) != 0) {
            return -1;
        }
        hub->ports[port].power_on = 1;
        /* Wait for power-on-to-good time */
        uint32_t delay = hub->pwr_on_time * 2;
        if (delay < USB_HUB_POWER_ON_MS) delay = USB_HUB_POWER_ON_MS;
        for (volatile uint32_t i = 0; i < delay * 1000; i++) {
            __asm__ volatile("pause");
        }
    } else {
        if (usb_hub_clear_port_feature(hub->usb_dev, port, USB_FEAT_PORT_POWER) != 0) {
            return -1;
        }
        hub->ports[port].power_on = 0;
    }
    return 0;
}

int32_t usb_hub_port_reset(usb_hub_t *hub, uint8_t port) {
    if (!hub || port >= hub->num_ports) return -1;

    hub->ports[port].resetting = 1;

    if (usb_hub_set_port_feature(hub->usb_dev, port, USB_FEAT_PORT_RESET) != 0) {
        hub->ports[port].resetting = 0;
        return -1;
    }

    /* Wait for reset to complete */
    for (volatile uint32_t i = 0; i < USB_HUB_RESET_MS * 1000; i++) {
        __asm__ volatile("pause");
    }

    uint16_t status = 0, change = 0;
    if (usb_hub_get_port_status(hub->usb_dev, port, &status, &change) == 0) {
        if (!(status & USB_PORT_STAT_RESET)) {
            hub->ports[port].resetting = 0;
            return 0;
        }
    }

    if (usb_hub_clear_port_feature(hub->usb_dev, port, USB_FEAT_C_PORT_RESET) != 0) {
        hub->ports[port].resetting = 0;
        return -1;
    }

    hub->ports[port].resetting = 0;
    return 0;
}

int32_t usb_hub_port_enable(usb_hub_t *hub, uint8_t port) {
    if (!hub || port >= hub->num_ports) return -1;
    if (usb_hub_set_port_feature(hub->usb_dev, port, USB_FEAT_PORT_ENABLE) != 0) {
        return -1;
    }
    hub->ports[port].enabled = 1;
    return 0;
}

int32_t usb_hub_port_disable(usb_hub_t *hub, uint8_t port) {
    if (!hub || port >= hub->num_ports) return -1;
    if (usb_hub_clear_port_feature(hub->usb_dev, port, USB_FEAT_PORT_ENABLE) != 0) {
        return -1;
    }
    hub->ports[port].enabled = 0;
    return 0;
}

int32_t usb_hub_port_suspend(usb_hub_t *hub, uint8_t port) {
    if (!hub || port >= hub->num_ports) return -1;
    if (usb_hub_set_port_feature(hub->usb_dev, port, USB_FEAT_PORT_SUSPEND) != 0) {
        return -1;
    }
    hub->ports[port].suspended = 1;
    return 0;
}

int32_t usb_hub_port_resume(usb_hub_t *hub, uint8_t port) {
    if (!hub || port >= hub->num_ports) return -1;
    if (usb_hub_clear_port_feature(hub->usb_dev, port, USB_FEAT_PORT_SUSPEND) != 0) {
        return -1;
    }
    hub->ports[port].suspended = 0;
    return 0;
}

static void hub_power_on_all_ports(usb_hub_t *hub) {
    uint8_t lpsm = (uint8_t)(hub->characteristics & HUB_CHAR_LPSM_MASK);

    for (uint8_t i = 0; i < hub->num_ports; i++) {
        /* Check if port power control is applicable */
        if (lpsm == HUB_CHAR_LPSM_GANGED || lpsm == HUB_CHAR_LPSM_GANGED2) {
            /* Ganged power: all ports share power */
            if (i == 0) {
                usb_hub_port_power(hub, i, 1);
            }
            hub->ports[i].power_on = 1;
        } else {
            usb_hub_port_power(hub, i, 1);
        }
    }
}

static void hub_handle_connect_change(usb_hub_t *hub, uint8_t port) {
    uint16_t status = 0, change = 0;
    if (usb_hub_get_port_status(hub->usb_dev, port, &status, &change) != 0) {
        return;
    }

    if (status & USB_PORT_STAT_CONNECTION) {
        /* Device connected */
        if (!hub->ports[port].connected) {
            /* Debounce delay */
            for (volatile uint32_t i = 0; i < USB_HUB_DEBOUNCE_MS * 500; i++) {
                __asm__ volatile("pause");
            }

            /* Verify connection is still present */
            if (usb_hub_get_port_status(hub->usb_dev, port, &status, 0) == 0 &&
                (status & USB_PORT_STAT_CONNECTION)) {

                /* Reset and enable the port */
                if (usb_hub_port_reset(hub, port) == 0) {
                    if (usb_hub_port_enable(hub, port) == 0) {
                        hub->ports[port].connected = 1;
                        hub->ports[port].device_attached = 1;
                        hub->ports[port].connect_time = global_tick;
                        hub->ports[port].last_change_time = global_tick;
                    }
                }
            }
        }
    } else {
        /* Device disconnected */
        if (hub->ports[port].connected) {
            hub->ports[port].connected = 0;
            hub->ports[port].device_attached = 0;
            hub->ports[port].enabled = 0;
            hub->ports[port].suspended = 0;
            hub->ports[port].device_addr = 0;
            hub->ports[port].last_change_time = global_tick;
        }
    }

    /* Clear the connection change bit */
    usb_hub_clear_port_feature(hub->usb_dev, port, USB_FEAT_C_PORT_CONNECTION);
}

static void hub_handle_overcurrent(usb_hub_t *hub, uint8_t port) {
    uint16_t status = 0, change = 0;
    if (usb_hub_get_port_status(hub->usb_dev, port, &status, &change) != 0) {
        return;
    }

    if (status & USB_PORT_STAT_OVER_CURRENT) {
        hub->ports[port].over_current = 1;
        /* Disable port to protect hardware */
        usb_hub_port_disable(hub, port);
        usb_hub_port_power(hub, port, 0);
    }

    usb_hub_clear_port_feature(hub->usb_dev, port, USB_FEAT_C_PORT_OVER_CURRENT);
}

static void hub_handle_enable_change(usb_hub_t *hub, uint8_t port) {
    uint16_t status = 0;
    if (usb_hub_get_port_status(hub->usb_dev, port, &status, 0) == 0) {
        hub->ports[port].enabled = (status & USB_PORT_STAT_ENABLE) ? 1 : 0;
    }
    usb_hub_clear_port_feature(hub->usb_dev, port, USB_FEAT_C_PORT_ENABLE);
}

static void hub_handle_suspend_change(usb_hub_t *hub, uint8_t port) {
    uint16_t status = 0;
    if (usb_hub_get_port_status(hub->usb_dev, port, &status, 0) == 0) {
        hub->ports[port].suspended = (status & USB_PORT_STAT_SUSPEND) ? 1 : 0;
    }
    usb_hub_clear_port_feature(hub->usb_dev, port, USB_FEAT_C_PORT_SUSPEND);
}

int32_t usb_hub_handle_status_change(usb_hub_t *hub) {
    if (!hub || !hub->initialized) return -1;

    /* Read hub status to get the port change bitmap */
    uint32_t hub_status;
    int32_t ret = usb_control_msg(hub->usb_dev,
        0xA0, USB_REQ_GET_STATUS,
        0, 0,
        &hub_status, 4);

    if (ret < 0) return -1;

    uint16_t port_change_map = (uint16_t)((hub_status >> 16) & 0xFFFF);

    for (uint8_t port = 0; port < hub->num_ports; port++) {
        if (!(port_change_map & (1 << (port + 1)))) {
            continue;
        }

        uint16_t status = 0, change = 0;
        if (usb_hub_get_port_status(hub->usb_dev, port, &status, &change) != 0) {
            continue;
        }

        hub->ports[port].wPortStatus = status;
        hub->ports[port].wPortChange = change;

        if (change & USB_PORT_STAT_C_CONNECTION) {
            hub_handle_connect_change(hub, port);
        }
        if (change & USB_PORT_STAT_C_OVER_CURRENT) {
            hub_handle_overcurrent(hub, port);
        }
        if (change & USB_PORT_STAT_C_ENABLE) {
            hub_handle_enable_change(hub, port);
        }
        if (change & USB_PORT_STAT_C_SUSPEND) {
            hub_handle_suspend_change(hub, port);
        }
        if (change & USB_PORT_STAT_C_RESET) {
            usb_hub_clear_port_feature(hub->usb_dev, port, USB_FEAT_C_PORT_RESET);
        }
    }

    return 0;
}

void usb_hub_poll(void) {
    global_tick++;

    usb_hub_t *hub = hub_list;
    while (hub) {
        if (hub->initialized && hub->polling) {
            if (global_tick - hub->last_poll_tick >= hub->poll_interval) {
                usb_hub_handle_status_change(hub);
                hub->last_poll_tick = global_tick;
            }
        }
        hub = hub->next;
    }
}

int32_t usb_hub_probe(usb_device_t *dev, usb_hub_t *parent, uint8_t port) {
    if (!dev) return -1;
    if (hub_count >= USB_HUB_MAX_HUBS) return -1;

    usb_hub_desc_t desc;
    if (usb_hub_get_descriptor(dev, &desc) != 0) {
        return -1;
    }

    if (desc.bDescriptorType != USB_DT_HUB) {
        return -1;
    }

    usb_hub_t *hub = (usb_hub_t *)kmalloc(sizeof(usb_hub_t));
    if (!hub) return -1;
    memset(hub, 0, sizeof(usb_hub_t));

    hub->hub_id = hub_count++;
    hub->hub_addr = dev->address;
    hub->usb_dev = dev;
    hub->num_ports = desc.bNbrPorts;
    if (hub->num_ports > USB_HUB_MAX_PORTS) {
        hub->num_ports = USB_HUB_MAX_PORTS;
    }
    hub->characteristics = desc.wHubCharacteristics;
    hub->pwr_on_time = desc.bPwrOn2PwrGood;
    hub->hub_current = desc.bHubContrCurrent;
    hub->parent = parent;

    memcpy(hub->port_pwr_ctrl_mask, desc.PortPwrCtrlMask, 2);
    memcpy(hub->device_removable, desc.DeviceRemovable, 2);

    if (parent) {
        hub->hub_depth = parent->hub_depth + 1;
    } else {
        hub->hub_depth = 0;
        root_hub = hub;
    }

    if (hub->hub_depth > USB_HUB_MAX_DEPTH) {
        kfree(hub);
        hub_count--;
        return -1;
    }

    hub->poll_interval = 100;
    hub->polling = 1;
    hub->last_poll_tick = global_tick;

    /* Power on all ports */
    hub_power_on_all_ports(hub);

    /* Initialize port states */
    for (uint8_t i = 0; i < hub->num_ports; i++) {
        uint16_t status = 0, change = 0;
        if (usb_hub_get_port_status(dev, i, &status, &change) == 0) {
            hub->ports[i].wPortStatus = status;
            hub->ports[i].wPortChange = change;
            hub->ports[i].power_on = (status & USB_PORT_STAT_POWER) ? 1 : 0;
            hub->ports[i].connected = (status & USB_PORT_STAT_CONNECTION) ? 1 : 0;
            hub->ports[i].enabled = (status & USB_PORT_STAT_ENABLE) ? 1 : 0;
            hub->ports[i].suspended = (status & USB_PORT_STAT_SUSPEND) ? 1 : 0;
            hub->ports[i].device_attached = hub->ports[i].connected;
            hub->ports[i].last_change_time = global_tick;
        }
    }

    hub->initialized = 1;

    hub->next = hub_list;
    hub_list = hub;

    printf("USB Hub %d: %d ports, depth %d, power %d mA\n",
        hub->hub_id, hub->num_ports, hub->hub_depth,
        hub->hub_current * 2);

    return 0;
}

void usb_hub_enumerate_cascaded(usb_hub_t *hub, uint8_t max_depth) {
    if (!hub || !hub->initialized) return;
    if (hub->hub_depth >= max_depth) return;

    for (uint8_t port = 0; port < hub->num_ports; port++) {
        if (hub->ports[port].connected && hub->ports[port].device_attached) {
            /* Check if this port has a hub device */
            uint16_t status = 0;
            if (usb_hub_get_port_status(hub->usb_dev, port, &status, 0) == 0) {
                if (status & USB_PORT_STAT_CONNECTION) {
                    /* This is where device enumeration would happen for cascaded hubs */
                    /* The USB core would detect the class and call usb_hub_probe */
                }
            }
        }
    }
}

usb_hub_t *usb_hub_find_by_addr(uint8_t addr) {
    usb_hub_t *hub = hub_list;
    while (hub) {
        if (hub->hub_addr == addr) return hub;
        hub = hub->next;
    }
    return 0;
}

usb_hub_t *usb_hub_get_root(void) {
    return root_hub;
}

uint8_t usb_hub_get_depth(usb_hub_t *hub) {
    if (!hub) return 0;
    return hub->hub_depth;
}