#include "xhci.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "kheap.h"
#include "string.h"

static xhci_controller_t xhci;

static uint32_t xhci_read_cap(uint32_t offset) {
    return *((volatile uint32_t *)((uint32_t)xhci.cap_base + offset));
}

static void xhci_write_op(uint32_t offset, uint32_t val) {
    *((volatile uint32_t *)((uint32_t)xhci.op_base + offset)) = val;
}

static uint32_t xhci_read_op(uint32_t offset) {
    return *((volatile uint32_t *)((uint32_t)xhci.op_base + offset));
}

static void xhci_ring_doorbell(uint32_t slot, uint32_t target) {
    *((volatile uint32_t *)((uint32_t)xhci.doorbell_base + slot * 4)) = target;
}

int xhci_init(void) {
    /* Scan PCI for xHCI controller: class 0x0C, subclass 0x03, prog_if 0x30 */
    pci_device_t *pcidev = NULL;
    pci_scan();
    /* We need to find by class - iterate known devices or use a helper.
       For now, try common vendor/device IDs or search manually. */
    /* Use pci_find_device with vendor 0xFFFF as wildcard won't work,
       so we scan by reading config directly for class matching */
    uint8_t bus, dev, func;
    int found = 0;
    for (bus = 0; bus < PCI_MAX_BUSES && !found; bus++) {
        for (dev = 0; dev < PCI_MAX_DEVICES && !found; dev++) {
            for (func = 0; func < PCI_MAX_FUNCTIONS && !found; func++) {
                uint32_t vd = pci_read_config(bus, dev, func, 0x00);
                if (vd == 0xFFFFFFFF || vd == 0) continue;
                uint32_t cl = pci_read_config(bus, dev, func, 0x08);
                uint8_t base_class = (cl >> 24) & 0xFF;
                uint8_t sub_class = (cl >> 16) & 0xFF;
                uint8_t prog_if = (cl >> 8) & 0xFF;
                if (base_class == 0x0C && sub_class == 0x03 && prog_if == 0x30) {
                    found = 1;
                }
            }
        }
    }
    if (!found) return -1;
    bus--; dev--; func--; /* undo last increment */

    xhci.pci_bus = bus;
    xhci.pci_dev = dev;
    xhci.pci_func = func;
    uint32_t bar0 = pci_read_config(bus, dev, func, 0x10);
    uint32_t bar1 = pci_read_config(bus, dev, func, 0x14);
    uint64_t phys_base = ((uint64_t)(bar1 & 0xFFFFFF) << 32) | (bar0 & 0xFFFFFFF0);

    /* Enable bus master */
    uint32_t cmd = pci_read_config(bus, dev, func, 0x04);
    pci_write_config(bus, dev, func, 0x04, cmd | (1 << 2));

    xhci.cap_base = (uint32_t *)vmm_map_physical((uint32_t)phys_base, 0x10000);
    uint32_t cap_length = xhci_read_cap(0x00) & 0xFF;
    xhci.op_base = (uint32_t *)((uint32_t)xhci.cap_base + cap_length);
    uint32_t rt_off = xhci_read_cap(0x18);
    xhci.run_base = (uint32_t *)((uint32_t)xhci.cap_base + rt_off);
    uint32_t db_off = xhci_read_cap(0x14);
    xhci.doorbell_base = (uint32_t *)((uint32_t)xhci.cap_base + db_off);
    xhci.max_slots = (xhci_read_cap(0x04) >> 0) & 0xFF;
    if (xhci_reset() != 0) return -1;
    xhci.cmd_ring = (xhci_trb_t *)pmm_alloc_pages(2);
    uint32_t cmd_ring_phys = (uint32_t)xhci.cmd_ring;
    xhci.cmd_ring = (xhci_trb_t *)vmm_map_physical(cmd_ring_phys, 8192);
    memset(xhci.cmd_ring, 0, 8192);
    xhci.cmd_ring_cycle = 1;
    xhci_write_op(0x18, cmd_ring_phys | 1);
    xhci.event_ring = (xhci_trb_t *)pmm_alloc_pages(2);
    uint32_t event_ring_phys = (uint32_t)xhci.event_ring;
    xhci.event_ring = (xhci_trb_t *)vmm_map_physical(event_ring_phys, 8192);
    memset(xhci.event_ring, 0, 8192);
    xhci.event_ring_cycle = 1;
    uint32_t *erst = (uint32_t *)pmm_alloc_pages(1);
    uint32_t erst_phys = (uint32_t)erst;
    erst = (uint32_t *)vmm_map_physical(erst_phys, 4096);
    erst[0] = event_ring_phys;
    erst[1] = 0;
    erst[2] = 512;
    erst[3] = 0;
    *((volatile uint32_t *)((uint32_t)xhci.run_base + 0x20)) = event_ring_phys;
    *((volatile uint32_t *)((uint32_t)xhci.run_base + 0x24)) = 0;
    *((volatile uint32_t *)((uint32_t)xhci.run_base + 0x28)) = erst_phys;
    *((volatile uint32_t *)((uint32_t)xhci.run_base + 0x2C)) = 0;
    *((volatile uint32_t *)((uint32_t)xhci.run_base + 0x30)) = 1;
    xhci.device_context_base = (uint32_t *)pmm_alloc_pages(1);
    uint32_t dcbaap_phys = (uint32_t)xhci.device_context_base;
    xhci.device_context_base = (uint32_t *)vmm_map_physical(dcbaap_phys, 4096);
    memset(xhci.device_context_base, 0, 4096);
    xhci_write_op(0x30, dcbaap_phys);
    xhci_write_op(0x38, xhci.max_slots);
    uint32_t usb_cmd = xhci_read_op(0x00);
    usb_cmd |= (1 << 0) | (1 << 2);
    xhci_write_op(0x00, usb_cmd);
    return 0;
}

int xhci_reset(void) {
    uint32_t usb_cmd = xhci_read_op(0x00);
    usb_cmd |= (1 << 1);
    xhci_write_op(0x00, usb_cmd);
    uint32_t timeout = 1000000;
    while (timeout--) {
        if (!(xhci_read_op(0x00) & (1 << 1))) break;
    }
    if (timeout == 0) return -1;
    timeout = 1000000;
    while (timeout--) {
        if (!(xhci_read_op(0x04) & (1 << 11))) break;
    }
    return (timeout == 0) ? -1 : 0;
}

int xhci_send_command(xhci_trb_t *cmd) {
    static uint32_t cmd_index = 0;
    xhci.cmd_ring[cmd_index].parameter = cmd->parameter;
    xhci.cmd_ring[cmd_index].status = cmd->status;
    xhci.cmd_ring[cmd_index].control = cmd->control | (xhci.cmd_ring_cycle << 0);
    cmd_index++;
    if (cmd_index >= 511) {
        xhci.cmd_ring[cmd_index].parameter = 0;
        xhci.cmd_ring[cmd_index].status = 0;
        xhci.cmd_ring[cmd_index].control = (xhci.cmd_ring_cycle ^ 1) | (1 << 1) | (1 << 5);
        cmd_index = 0;
        xhci.cmd_ring_cycle ^= 1;
    }
    xhci_ring_doorbell(0, 0);
    uint32_t timeout = 1000000;
    while (timeout--) {
        uint32_t erst_dequeue = *((volatile uint32_t *)((uint32_t)xhci.run_base + 0x3C));
        if (erst_dequeue != 0) break;
    }
    return 0;
}

int xhci_enable_slot(uint8_t *slot_id) {
    xhci_trb_t cmd;
    cmd.parameter = 0;
    cmd.status = 0;
    cmd.control = (9 << 10);
    if (xhci_send_command(&cmd) != 0) return -1;
    *slot_id = (xhci.event_ring[0].control >> 24) & 0xFF;
    return 0;
}

int xhci_address_device(uint8_t slot_id, void *input_ctx) {
    xhci_trb_t cmd;
    cmd.parameter = (uint32_t)input_ctx;
    cmd.status = 0;
    cmd.control = (11 << 10) | (slot_id << 24);
    return xhci_send_command(&cmd);
}

int xhci_transfer(uint8_t slot_id, uint8_t ep, xhci_trb_t *trbs, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        trbs[i].control |= (1 << 2);
    }
    trbs[count - 1].control |= (1 << 5);
    xhci_ring_doorbell(slot_id, ep);
    uint32_t timeout = 1000000;
    while (timeout--) {
        uint32_t comp_code = (xhci.event_ring[0].status >> 24) & 0xFF;
        if (comp_code == 1) break;
    }
    return 0;
}

uint8_t xhci_get_max_ports(void) {
    /* Max ports is in HCSPARAMS1 bits [31:24] */
    uint32_t hcs_params1 = xhci_read_cap(0x04);
    return (hcs_params1 >> 24) & 0xFF;
}

/*
 * Check all xHCI ports for status changes (connect/disconnect).
 * Populates the events array with detected changes.
 * Returns the number of port change events detected.
 */
int xhci_check_ports(xhci_port_event_t *events, int max_events) {
    int count = 0;
    uint8_t max_ports = xhci_get_max_ports();

    for (uint8_t port = 0; port < max_ports && count < max_events; port++) {
        /* PORTSC register offset: 0x400 + (port * 0x10) in operational regs */
        uint32_t portsc_offset = 0x400 + (port * 0x10);
        uint32_t portsc = xhci_read_op(portsc_offset);

        /* Check Connect Status Change bit (bit 17) */
        if (portsc & XHCI_PORTSC_CSC) {
            events[count].port = port;
            /* Determine connect or disconnect from Current Connect Status (bit 0) */
            events[count].connected = (portsc & XHCI_PORTSC_CCS) ? 1 : 0;

            /* Clear the change bit by writing 1 to it */
            /* Must preserve read-only and write-1-to-clear bits properly:
             * Write back only the CSC bit to clear it, plus any W1C bits */
            xhci_write_op(portsc_offset, XHCI_PORTSC_CSC);

            count++;
        }
    }

    return count;
}

/*
 * Handle port status changes: enumerate new devices, clean up removed ones.
 * Called periodically from usb_hotplug_poll().
 */
void xhci_handle_port_change(void) {
    xhci_port_event_t events[32];
    int count = xhci_check_ports(events, 32);

    for (int i = 0; i < count; i++) {
        uint8_t port = events[i].port;

        if (events[i].connected) {
            /* New device connected - reset the port */
            uint32_t portsc_offset = 0x400 + (port * 0x10);
            uint32_t portsc = xhci_read_op(portsc_offset);

            /* Assert port reset (set PR bit) */
            portsc |= XHCI_PORTSC_PR;
            xhci_write_op(portsc_offset, portsc);

            /* Wait for reset to complete (PR bit clears) */
            uint32_t timeout = 1000000;
            while (timeout--) {
                portsc = xhci_read_op(portsc_offset);
                if (!(portsc & XHCI_PORTSC_PR)) break;
            }

            /* After reset, enable slot and address device */
            uint8_t slot_id;
            if (xhci_enable_slot(&slot_id) != 0) continue;

            void *input_ctx = kmalloc(4096);
            if (!input_ctx) continue;
            /* Zero the input context - set A0 flag in input control context */
            uint32_t *ictx = (uint32_t *)input_ctx;
            ictx[0] = 0;       /* Input control context - drop flags */
            ictx[1] = (1 << 0); /* Add flags: set A0 (slot context) */

            if (xhci_address_device(slot_id, input_ctx) != 0) {
                kfree(input_ctx);
                continue;
            }
            kfree(input_ctx);

            /* The rest of enumeration (reading descriptors, setting address,
             * configuring) is handled by usb_core's hotplug logic */
        } else {
            /* Device disconnected - slot cleanup is handled by
             * usb_core's usb_device_remove() after this returns */
        }
    }
}
