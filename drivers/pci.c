#include "pci.h"
#include "kheap.h"
#include "io.h"

static pci_device_t device_list[256];
static int device_count = 0;

uint32_t pci_read_config(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t address = ((uint32_t)bus << 16) |
                       ((uint32_t)dev << 11) |
                       ((uint32_t)func << 8) |
                       ((uint32_t)offset & 0xFC) |
                       0x80000000;
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

void pci_write_config(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = ((uint32_t)bus << 16) |
                       ((uint32_t)dev << 11) |
                       ((uint32_t)func << 8) |
                       ((uint32_t)offset & 0xFC) |
                       0x80000000;
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

static void pci_check_device(uint8_t bus, uint8_t dev, uint8_t func) {
    uint32_t val = pci_read_config(bus, dev, func, 0x00);
    uint16_t vendor = val & 0xFFFF;
    if (vendor == 0xFFFF) return;

    pci_device_t *d = &device_list[device_count];
    d->bus = bus;
    d->device = dev;
    d->function = func;
    d->vendor_id = vendor;
    d->device_id = (val >> 16) & 0xFFFF;

    val = pci_read_config(bus, dev, func, 0x08);
    d->class_code = (val >> 24) & 0xFF;
    d->subclass = (val >> 16) & 0xFF;
    d->prog_if = (val >> 8) & 0xFF;
    d->revision = val & 0xFF;

    val = pci_read_config(bus, dev, func, 0x3C);
    d->irq_line = val & 0xFF;

    for (int i = 0; i < 6; i++) {
        d->bar[i] = pci_read_config(bus, dev, func, 0x10 + i * 4);
    }

    device_count++;
}

void pci_scan(void) {
    device_count = 0;
    for (int bus = 0; bus < PCI_MAX_BUSES; bus++) {
        for (int dev = 0; dev < PCI_MAX_DEVICES; dev++) {
            uint32_t val = pci_read_config(bus, dev, 0, 0x00);
            if ((val & 0xFFFF) == 0xFFFF) continue;
            pci_check_device(bus, dev, 0);
            uint32_t hdr = pci_read_config(bus, dev, 0, 0x0C);
            uint8_t hdr_type = (hdr >> 16) & 0xFF;
            if (hdr_type & 0x80) {
                for (int func = 1; func < PCI_MAX_FUNCTIONS; func++) {
                    val = pci_read_config(bus, dev, func, 0x00);
                    if ((val & 0xFFFF) != 0xFFFF) {
                        pci_check_device(bus, dev, func);
                    }
                }
            }
        }
    }
}

void pci_init(void) {
    pci_scan();
}

pci_device_t *pci_find_device(uint16_t vendor_id, uint16_t device_id) {
    for (int i = 0; i < device_count; i++) {
        if (device_list[i].vendor_id == vendor_id && device_list[i].device_id == device_id) {
            return &device_list[i];
        }
    }
    return (void *)0;
}

void pci_get_device(uint8_t bus, uint8_t dev, uint8_t func, pci_device_t *out) {
    for (int i = 0; i < device_count; i++) {
        if (device_list[i].bus == bus && device_list[i].device == dev && device_list[i].function == func) {
            *out = device_list[i];
            return;
        }
    }
}
