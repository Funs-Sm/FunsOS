#ifndef XHCI_H
#define XHCI_H

#include "stdint.h"

#define XHCI_MAX_SLOTS 255
#define XHCI_MAX_INTRS 16
#define XHCI_MAX_PORTS 256

/* XHCI Port Status and Control Register bits */
#define XHCI_PORTSC_CCS       (1 << 0)    /* Current Connect Status */
#define XHCI_PORTSC_PED       (1 << 1)    /* Port Enabled/Disabled */
#define XHCI_PORTSC_OCA       (1 << 3)    /* Over-Current Active */
#define XHCI_PORTSC_PR        (1 << 4)    /* Port Reset */
#define XHCI_PORTSC_PLS_MASK  (0xF << 5)  /* Port Link State */
#define XHCI_PORTSC_PP        (1 << 9)    /* Port Power */
#define XHCI_PORTSC_SPEED_MASK (0xF << 10) /* Port Speed */
#define XHCI_PORTSC_CSC       (1 << 17)   /* Connect Status Change */
#define XHCI_PORTSC_PEC       (1 << 18)   /* Port Enable/Disable Change */
#define XHCI_PORTSC_WRC       (1 << 19)   /* Warm Port Reset Change */
#define XHCI_PORTSC_OCC       (1 << 20)   /* Over-Current Change */
#define XHCI_PORTSC_PRC       (1 << 21)   /* Port Reset Change */
#define XHCI_PORTSC_PLC       (1 << 22)   /* Port Link State Change */
#define XHCI_PORTSC_CEC       (1 << 23)   /* Config Error Change */

/* Port change event for hot-plug detection */
typedef struct {
    uint8_t port;
    uint8_t connected;  /* 1 = connected, 0 = disconnected */
} xhci_port_event_t;

typedef struct {
    uint8_t cap_length;
    uint8_t reserved;
    uint8_t version;
    uint8_t max_slots;
    uint8_t intr_type;
    uint32_t hcs_params1;
    uint32_t hcs_params2;
    uint32_t hcs_params3;
    uint32_t hcc_params;
    uint32_t db_off;
    uint32_t rt_off;
} xhci_cap_regs;

typedef struct {
    uint32_t usb_cmd;
    uint32_t usb_sts;
    uint32_t page_size;
    uint32_t dn_ctrl;
    uint32_t crcr;
    uint32_t dcbaap;
    uint32_t config;
    uint32_t port_sc[256];
} xhci_op_regs;

typedef struct {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
} xhci_trb_t;

typedef struct {
    uint32_t fields[8];
} xhci_slot_ctx_t;

typedef struct {
    uint32_t fields[8];
} xhci_endpoint_ctx_t;

typedef struct {
    uint32_t pci_bus;
    uint32_t pci_dev;
    uint32_t pci_func;
    uint32_t *cap_base;
    uint32_t *op_base;
    uint32_t *run_base;
    uint32_t *doorbell_base;
    uint32_t max_slots;
    xhci_trb_t *cmd_ring;
    uint32_t cmd_ring_cycle;
    xhci_trb_t *event_ring;
    uint32_t event_ring_cycle;
    uint32_t *device_context_base;
} xhci_controller_t;

int xhci_init(void);
int xhci_reset(void);
int xhci_send_command(xhci_trb_t *cmd);
int xhci_enable_slot(uint8_t *slot_id);
int xhci_address_device(uint8_t slot_id, void *input_ctx);
int xhci_transfer(uint8_t slot_id, uint8_t ep, xhci_trb_t *trbs, uint32_t count);
int xhci_check_ports(xhci_port_event_t *events, int max_events);
void xhci_handle_port_change(void);
uint8_t xhci_get_max_ports(void);

#endif
