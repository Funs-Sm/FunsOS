#ifndef USB_CORE_H
#define USB_CORE_H

#include "stdint.h"

#define USB_MAX_DEVICES 128

#define USB_SPEED_LOW   0
#define USB_SPEED_FULL  1
#define USB_SPEED_HIGH  2
#define USB_SPEED_SUPER 3

typedef struct usb_device {
    uint8_t address;
    uint8_t speed;
    uint8_t hub_port;
    uint8_t slot_id;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t protocol;
    struct usb_device *next;
} usb_device_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
} usb_desc_device_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces;
    uint8_t bConfigurationValue;
    uint8_t iConfiguration;
    uint8_t bmAttributes;
    uint8_t bMaxPower;
} usb_desc_config_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bNumEndpoints;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t iInterface;
} usb_desc_interface_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bEndpointAddress;
    uint8_t bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t bInterval;
} usb_desc_endpoint_t;

int usb_init(void);
int usb_enumerate(void);
int usb_enumerate_device(uint8_t addr);
usb_device_t *usb_get_device(uint8_t addr);
int usb_control_transfer(uint8_t dev_addr, uint8_t request_type, uint8_t request, uint16_t value, uint16_t index, void *data, uint16_t length);
int usb_bulk_transfer(uint8_t dev_addr, uint8_t ep, void *data, uint32_t len);
int usb_interrupt_transfer(uint8_t dev_addr, uint8_t ep, void *data, uint32_t len);
void usb_hotplug_poll(void);
void usb_device_remove(uint8_t addr);

#endif
