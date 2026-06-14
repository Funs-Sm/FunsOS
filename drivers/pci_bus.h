#ifndef PCI_BUS_H
#define PCI_BUS_H
#include "stdint.h"

/* PCI 配置空间寄存器 */
#define PCI_VENDOR_ID      0x00
#define PCI_DEVICE_ID      0x02
#define PCI_COMMAND        0x04
#define PCI_STATUS         0x06
#define PCI_REVISION_ID    0x08
#define PCI_CLASS_CODE     0x0B
#define PCI_HEADER_TYPE    0x0E
#define PCI_BAR0           0x10
#define PCI_BAR1           0x14
#define PCI_BAR2           0x18
#define PCI_BAR3           0x1C
#define PCI_BAR4           0x20
#define PCI_BAR5           0x24
#define PCI_CAP_PTR        0x34
#define PCI_INTERRUPT_LINE 0x3C
#define PCI_INTERRUPT_PIN  0x3D

/* PCI 设备类 */
#define PCI_CLASS_NETWORK  0x02
#define PCI_CLASS_DISPLAY  0x03
#define PCI_CLASS_MULTIMEDIA 0x04
#define PCI_CLASS_BRIDGE   0x06
#define PCI_CLASS_STORAGE  0x01
#define PCI_CLASS_USB      0x0C

/* PCI 设备结构 */
typedef struct pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t header_type;
    uint8_t irq_line;
    uint8_t irq_pin;
    uint32_t bar[6];
    uint32_t bar_size[6];
    struct pci_device *next;
} pci_device_t;

/* PCI 驱动 */
typedef struct pci_driver {
    uint16_t vendor_id;
    uint16_t device_id;
    const char *name;
    int (*probe)(pci_device_t *dev);
    int (*init)(pci_device_t *dev);
    void (*remove)(pci_device_t *dev);
    struct pci_driver *next;
} pci_driver_t;

/* API */
void pci_bus_init(void);
void pci_bus_scan(void);
pci_device_t *pci_bus_find_device(uint16_t vendor, uint16_t device);
pci_device_t *pci_find_class(uint8_t class_code, uint8_t subclass);
uint32_t pci_bus_read_cfg(pci_device_t *dev, uint8_t offset, uint8_t size);
void pci_bus_write_cfg(pci_device_t *dev, uint8_t offset, uint32_t value, uint8_t size);
int pci_register_driver(pci_driver_t *driver);
void pci_enable_bus_mastering(pci_device_t *dev);
void pci_set_irq(pci_device_t *dev, uint8_t irq);
uint32_t pci_get_bar_size(pci_device_t *dev, int bar);
void pci_list_devices(void);
int pci_device_count(void);

#endif