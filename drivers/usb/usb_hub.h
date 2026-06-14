#ifndef USB_HUB_H
#define USB_HUB_H

#include "stdint.h"
#include "usb.h"

/* USB Hub class code */
#define USB_CLASS_HUB       0x09
#define USB_SUBCLASS_HUB    0x00

/* Hub descriptor types */
#define USB_DT_HUB          0x29
#define USB_DT_HUB3         0x2A

/* Hub request types */
#define USB_REQ_GET_STATUS          0x00
#define USB_REQ_CLEAR_FEATURE       0x01
#define USB_REQ_SET_FEATURE         0x03
#define USB_REQ_GET_DESCRIPTOR      0x06

/* Feature selectors */
#define USB_FEAT_PORT_CONNECTION       0x00
#define USB_FEAT_PORT_ENABLE           0x01
#define USB_FEAT_PORT_SUSPEND          0x02
#define USB_FEAT_PORT_OVER_CURRENT     0x03
#define USB_FEAT_PORT_RESET            0x04
#define USB_FEAT_PORT_POWER            0x08
#define USB_FEAT_PORT_LOW_SPEED        0x09
#define USB_FEAT_C_PORT_CONNECTION     0x10
#define USB_FEAT_C_PORT_ENABLE         0x11
#define USB_FEAT_C_PORT_SUSPEND        0x12
#define USB_FEAT_C_PORT_OVER_CURRENT   0x13
#define USB_FEAT_C_PORT_RESET          0x14
#define USB_FEAT_PORT_TEST             0x15
#define USB_FEAT_PORT_INDICATOR        0x16

/* Hub characteristics */
#define HUB_CHAR_LPSM_MASK      0x0003
#define HUB_CHAR_LPSM_GANGED    0x0000
#define HUB_CHAR_LPSM_INDIV     0x0001
#define HUB_CHAR_LPSM_GANGED2   0x0002
#define HUB_CHAR_LPSM_INDIV2    0x0003
#define HUB_CHAR_COMPOUND       0x0004
#define HUB_CHAR_OCPM_MASK      0x0018
#define HUB_CHAR_OCPM_GLOBAL    0x0000
#define HUB_CHAR_OCPM_INDIV     0x0008
#define HUB_CHAR_OCPM_NONE      0x0010
#define HUB_CHAR_OCPM_INDIV2    0x0018
#define HUB_CHAR_TTTT_MASK      0x0060
#define HUB_CHAR_TTTT_8         0x0000
#define HUB_CHAR_TTTT_16        0x0020
#define HUB_CHAR_TTTT_24        0x0040
#define HUB_CHAR_TTTT_32        0x0060
#define HUB_CHAR_PORTIND        0x0080

/* Port status bits */
#define USB_PORT_STAT_CONNECTION      0x0001
#define USB_PORT_STAT_ENABLE          0x0002
#define USB_PORT_STAT_SUSPEND         0x0004
#define USB_PORT_STAT_OVER_CURRENT    0x0008
#define USB_PORT_STAT_RESET           0x0010
#define USB_PORT_STAT_POWER           0x0100
#define USB_PORT_STAT_LOW_SPEED       0x0200
#define USB_PORT_STAT_HIGH_SPEED      0x0400
#define USB_PORT_STAT_TEST            0x0800
#define USB_PORT_STAT_INDICATOR       0x1000

/* Port change bits */
#define USB_PORT_STAT_C_CONNECTION    0x0001
#define USB_PORT_STAT_C_ENABLE        0x0002
#define USB_PORT_STAT_C_SUSPEND       0x0004
#define USB_PORT_STAT_C_OVER_CURRENT  0x0008
#define USB_PORT_STAT_C_RESET         0x0010

/* Hub limits */
#define USB_HUB_MAX_PORTS        15
#define USB_HUB_MAX_DEPTH        5
#define USB_HUB_MAX_HUBS         16
#define USB_HUB_DEBOUNCE_MS      100
#define USB_HUB_RESET_MS         50
#define USB_HUB_POWER_ON_MS      100

/* Hub descriptor */
typedef struct {
    uint8_t  bDescLength;
    uint8_t  bDescriptorType;
    uint8_t  bNbrPorts;
    uint16_t wHubCharacteristics;
    uint8_t  bPwrOn2PwrGood;
    uint8_t  bHubContrCurrent;
    /* DeviceRemovable and PortPwrCtrlMask follow */
    uint8_t  DeviceRemovable[2];
    uint8_t  PortPwrCtrlMask[2];
} __attribute__((packed)) usb_hub_desc_t;

/* Port status */
typedef struct {
    uint16_t wPortStatus;
    uint16_t wPortChange;
    uint32_t connect_time;
    uint32_t last_change_time;
    uint8_t  device_attached;
    uint8_t  device_addr;
    uint8_t  power_on;
    uint8_t  over_current;
    uint8_t  resetting;
    uint8_t  connected;
    uint8_t  enabled;
    uint8_t  suspended;
} usb_hub_port_t;

/* Hub instance */
typedef struct usb_hub {
    uint8_t  hub_id;
    uint8_t  hub_addr;
    uint8_t  hub_depth;
    uint8_t  num_ports;
    uint16_t characteristics;
    uint8_t  pwr_on_time;
    uint8_t  hub_current;
    uint8_t  port_pwr_ctrl_mask[2];
    uint8_t  device_removable[2];
    usb_device_t *usb_dev;
    usb_hub_port_t ports[USB_HUB_MAX_PORTS];
    struct usb_hub *parent;
    struct usb_hub *next;
    uint8_t  initialized;
    uint8_t  polling;
    uint32_t poll_interval;
    uint32_t last_poll_tick;
    /* Bitmap for power-on-delayed ports */
    uint16_t power_delayed;
} usb_hub_t;

/* Hub API */
void usb_hub_init(void);
int32_t usb_hub_probe(usb_device_t *dev, usb_hub_t *parent, uint8_t port);
int32_t usb_hub_get_descriptor(usb_device_t *dev, usb_hub_desc_t *desc);
int32_t usb_hub_set_port_feature(usb_device_t *dev, uint8_t port, uint16_t feature);
int32_t usb_hub_clear_port_feature(usb_device_t *dev, uint8_t port, uint16_t feature);
int32_t usb_hub_get_port_status(usb_device_t *dev, uint8_t port, uint16_t *status, uint16_t *change);
int32_t usb_hub_port_power(usb_hub_t *hub, uint8_t port, uint8_t on);
int32_t usb_hub_port_reset(usb_hub_t *hub, uint8_t port);
int32_t usb_hub_port_enable(usb_hub_t *hub, uint8_t port);
int32_t usb_hub_port_disable(usb_hub_t *hub, uint8_t port);
int32_t usb_hub_port_suspend(usb_hub_t *hub, uint8_t port);
int32_t usb_hub_port_resume(usb_hub_t *hub, uint8_t port);
void usb_hub_poll(void);
int32_t usb_hub_handle_status_change(usb_hub_t *hub);
usb_hub_t *usb_hub_find_by_addr(uint8_t addr);
usb_hub_t *usb_hub_get_root(void);
uint8_t usb_hub_get_depth(usb_hub_t *hub);
void usb_hub_enumerate_cascaded(usb_hub_t *hub, uint8_t max_depth);

#endif