#ifndef USB_BT_H
#define USB_BT_H

#include "stdint.h"

/* USB Bluetooth HCI transport driver.
 *
 * Implements the Bluetooth Host Controller Interface (HCI) over USB,
 * matching the standard USB Bluetooth transport described in the
 * Bluetooth Core Specification (Vol 4, Part A). Most modern USB Bluetooth
 * adapters follow one of these class codes:
 *
 *   bInterfaceClass=0xE0 Wireless, bInterfaceSubClass=0x01 RF,
 *   bInterfaceProtocol=0x01 Bluetooth primary
 *
 * The driver exposes:
 *   - HCI command submission via the bulk OUT endpoint
 *   - ACL data TX/RX via the bulk endpoints
 *   - SCO data via isochronous endpoints (optional)
 *   - Event reception via the interrupt IN endpoint
 *
 * For simplicity we implement the bulk + interrupt channel only.
 */

#define USB_BT_CLASS_WIRELESS       0xE0
#define USB_BT_SUBCLASS_RF          0x01
#define USB_BT_PROTOCOL_BT_PRIMARY  0x01

/* HCI command opcodes (subset) */
#define HCI_OP_RESET                0x0C03
#define HCI_OP_READ_LOCAL_VERSION   0x1001
#define HCI_OP_READ_LOCAL_FEATURES  0x1003
#define HCI_OP_READ_LOCAL_COMMANDS  0x1002
#define HCI_OP_SET_EVENT_FILTER     0x0C05
#define HCI_OP_WRITE_SCAN_ENABLE    0x0C1A
#define HCI_OP_WRITE_PAGE_SCAN_ACTIVITY   0x0C47
#define HCI_OP_LE_SET_SCAN_ENABLE   0x200C
#define HCI_OP_LE_SET_SCAN_PARAMETERS 0x200B

/* HCI event codes */
#define HCI_EV_COMMAND_COMPLETE     0x0E
#define HCI_EV_COMMAND_STATUS       0x0F
#define HCI_EV_INQUIRY_RESULT       0x02
#define HCI_EV_CONN_COMPLETE        0x03
#define HCI_EV_LE_META              0x3E
#define HCI_EV_DISCONN_COMPLETE     0x05

/* HCI command packet format */
typedef struct __attribute__((packed)) {
    uint16_t opcode;
    uint8_t  param_len;
    uint8_t  params[];
} hci_cmd_t;

/* ACL data packet header */
typedef struct __attribute__((packed)) {
    uint16_t handle_pb_bc;
    uint16_t length;
    uint8_t  data[];
} hci_acl_t;

/* HCI event packet header */
typedef struct __attribute__((packed)) {
    uint8_t  code;
    uint8_t  param_len;
    uint8_t  params[];
} hci_event_t;

/* Connection handle info */
typedef struct {
    uint16_t handle;
    uint8_t  bdaddr[6];
    uint8_t  type;   /* 0 = BR/EDR, 1 = LE */
    uint8_t  active;
} usb_bt_conn_t;

/* Maximum simultaneous connections */
#define USB_BT_MAX_CONNECTIONS  8
/* Command queue depth */
#define USB_BT_CMD_QUEUE_SIZE  16

int usb_bt_init(uint8_t usb_dev_addr,
                uint8_t ep_bulk_out, uint8_t ep_bulk_in,
                uint8_t ep_int_in);
int usb_bt_send_cmd(uint16_t opcode, const void *params, uint8_t param_len);
int usb_bt_read_event(uint8_t *buf, uint32_t buf_len);
int usb_bt_acl_tx(uint16_t handle, const void *data, uint16_t len);
int usb_bt_acl_rx(uint16_t *handle, void *buf, uint16_t buf_len);
int usb_bt_get_conn_count(void);
const usb_bt_conn_t *usb_bt_get_conn(int idx);

#endif