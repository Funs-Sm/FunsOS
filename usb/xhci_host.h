#ifndef XHCI_HOST_H
#define XHCI_HOST_H

#include "stdint.h"
#include "xhci.h"

/* xHCI Host Controller Driver.
 *
 * Implements:
 *   - Controller initialization (BIOS handoff, port reset)
 *   - Device slot allocation and address assignment
 *   - Endpoint context management
 *   - Transfer ring construction for control/bulk/intr/isoc
 *   - Command submission via command ring TRB
 *   - Event ring handling for port / transfer / command completion
 *   - Doorbell register write for kicking transfer rings
 *
 * The driver uses the scheduler (usb_sched.h) for bandwidth allocation
 * and feeds periodic + non-periodic URBs to the host controller.
 */

#define XHCI_HOST_MAX_CONTROLLERS 4
#define XHCI_HOST_MAX_SLOTS       255
#define XHCI_HOST_MAX_EPS         32
#define XHCI_HOST_MAX_DEVICES     128
#define XHCI_HOST_TRANSFER_RING_SIZE  256
#define XHCI_HOST_EVENT_RING_SIZE     256
#define XHCI_HOST_CMD_RING_SIZE       32

/* TRB types. */
#define TRB_TYPE_NORMAL        1
#define TRB_TYPE_SETUP_STAGE   2
#define TRB_TYPE_DATA_STAGE    3
#define TRB_TYPE_STATUS_STAGE  4
#define TRB_TYPE_LINK          6
#define TRB_TYPE_CMD_ENABLE_SLOT  9
#define TRB_TYPE_CMD_DISABLE_SLOT 10
#define TRB_TYPE_CMD_ADDRESS_DEV  11
#define TRB_TYPE_CMD_CONFIG_EP    12
#define TRB_TYPE_CMD_RESET_EP     13
#define TRB_TYPE_CMD_NOOP         23
#define TRB_TYPE_PORT_STATUS    34
#define TRB_TYPE_CMD_DCBAA       22

/* Endpoint types. */
#define EP_TYPE_CONTROL_OUT  0
#define EP_TYPE_CONTROL_IN   1
#define EP_TYPE_BULK_OUT     2
#define EP_TYPE_BULK_IN      3
#define EP_TYPE_INT_OUT      4
#define EP_TYPE_INT_IN       5
#define EP_TYPE_ISOCH_OUT    6
#define EP_TYPE_ISOCH_IN     7

/* TRB template.  The canonical typedef lives in drivers/xhci_enhanced.h
 * (which is included transitively via xhci.h) so we don't redeclare it
 * here. */

/* Transfer ring. */
typedef struct {
    xhci_trb_t *ring;
    uint32_t    ring_paddr;
    uint16_t    ring_size;
    uint16_t    enqueue;     /* producer index */
    uint16_t    dequeue;     /* consumer index */
    uint16_t    cycle_state;
    uint8_t     doorbell_target;
} xhci_transfer_ring_t;

/* Endpoint context entry (32 bytes). */
typedef struct {
    uint32_t ep_state;
    uint32_t ep_info;
    uint32_t transfer_ring_paddr_lo;
    uint32_t transfer_ring_paddr_hi;
    uint32_t max_packet;
    uint32_t avg_trb_len;
    uint32_t reserved[2];
} __attribute__((packed)) xhci_endpoint_context_t;

/* Device context (matches xHCI device context layout, 32 byte header). */
typedef struct {
    uint32_t slot_info;
    uint32_t reserved0[7];
    xhci_endpoint_context_t eps[XHCI_HOST_MAX_EPS];
} __attribute__((packed)) xhci_device_context_t;

/* Controller state. */
typedef struct {
    /* MMIO regions. */
    volatile uint32_t *cap_regs;
    volatile uint32_t *op_regs;
    volatile uint32_t *doorbells;
    volatile uint32_t *runtime;
    uint32_t db_stride;
    /* DCBAA + command / event rings. */
    xhci_trb_t *cmd_ring;
    uint32_t cmd_ring_paddr;
    uint16_t cmd_ring_enq;
    uint16_t cmd_ring_cycle;
    xhci_trb_t *ev_ring;
    uint32_t ev_ring_paddr;
    uint16_t ev_ring_deq;
    uint16_t ev_ring_cycle;
    /* Device slots. */
    xhci_device_context_t *dcbaa;
    uint32_t dcbaa_paddr;
    uint8_t  slot_active[XHCI_HOST_MAX_SLOTS + 1];
    /* PCI device info. */
    uint8_t bus, dev, func;
    int     present;
    /* Stats. */
    uint64_t cmds_submitted;
    uint64_t cmds_completed;
    uint64_t transfers_submitted;
    uint64_t transfers_completed;
    uint64_t errors;
} xhci_host_t;

int xhci_host_init(xhci_host_t *hc, uint8_t bus, uint8_t dev, uint8_t func);
int xhci_host_start(xhci_host_t *hc);
int xhci_host_enable_slot(xhci_host_t *hc, uint8_t *slot);
int xhci_host_address_device(xhci_host_t *hc, uint8_t slot,
                              uint32_t route);
int xhci_host_configure_endpoint(xhci_host_t *hc, uint8_t slot,
                                  uint8_t ep_num, uint8_t ep_type,
                                  uint32_t transfer_ring_paddr,
                                  uint16_t max_packet, uint8_t interval);
int xhci_host_submit_control(xhci_host_t *hc, uint8_t slot,
                               const void *setup, void *data, uint32_t len);
int xhci_host_submit_bulk(xhci_host_t *hc, uint8_t slot, uint8_t ep_num,
                            int dir, void *data, uint32_t len);
int xhci_host_event_handler(xhci_host_t *hc);
int xhci_host_port_reset(xhci_host_t *hc, int port);
uint32_t xhci_host_port_status(xhci_host_t *hc, int port);
int xhci_host_set_speed(xhci_host_t *hc, int port, uint8_t speed);
void xhci_host_print_stats(xhci_host_t *hc);

#endif