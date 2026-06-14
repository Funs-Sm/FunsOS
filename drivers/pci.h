#ifndef PCI_H
#define PCI_H

#include "stdint.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define PCI_MAX_BUSES     256
#define PCI_MAX_DEVICES   32
#define PCI_MAX_FUNCTIONS 8

typedef struct {
    uint8_t  bus;
    uint8_t  device;
    uint8_t  function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  revision;
    uint8_t  irq_line;
    uint32_t bar[6];
} pci_device_t;

void pci_init(void);
void pci_scan(void);
uint32_t pci_read_config(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
void pci_write_config(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value);
pci_device_t *pci_find_device(uint16_t vendor_id, uint16_t device_id);
void pci_get_device(uint8_t bus, uint8_t dev, uint8_t func, pci_device_t *out);

#endif
