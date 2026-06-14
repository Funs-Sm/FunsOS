#include "pci_bus.h"
#include "io.h"
#include "klog.h"
#include "kheap.h"
#include "string.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define PCI_MAX_BUSES     256
#define PCI_MAX_SLOTS     32
#define PCI_MAX_FUNCS     8

/* 全局设备链表和驱动链表 */
static pci_device_t *device_head = (void *)0;
static pci_driver_t *driver_head = (void *)0;
static int device_count = 0;

/* ---- 底层 PCI 配置空间访问 ---- */

static uint32_t pci_raw_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) |
                       ((uint32_t)(offset & 0xFC)) |
                       0x80000000;
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static void pci_raw_write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) |
                       ((uint32_t)(offset & 0xFC)) |
                       0x80000000;
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

/* ---- 面向设备的配置空间访问（支持 8/16/32 位）---- */

uint32_t pci_bus_read_cfg(pci_device_t *dev, uint8_t offset, uint8_t size) {
    uint32_t val = pci_raw_read_config(dev->bus, dev->slot, dev->func, offset);
    switch (size) {
        case 1: return (val >> ((offset & 3) * 8)) & 0xFF;
        case 2: return (val >> ((offset & 2) * 8)) & 0xFFFF;
        case 4: return val;
        default: return val;
    }
}

void pci_bus_write_cfg(pci_device_t *dev, uint8_t offset, uint32_t value, uint8_t size) {
    switch (size) {
        case 1: {
            uint32_t align = offset & 0xFC;
            uint32_t val = pci_raw_read_config(dev->bus, dev->slot, dev->func, align);
            uint32_t shift = (offset & 3) * 8;
            val = (val & ~(0xFF << shift)) | ((value & 0xFF) << shift);
            pci_raw_write_config(dev->bus, dev->slot, dev->func, align, val);
            break;
        }
        case 2: {
            uint32_t align = offset & 0xFC;
            uint32_t val = pci_raw_read_config(dev->bus, dev->slot, dev->func, align);
            uint32_t shift = (offset & 2) * 8;
            val = (val & ~(0xFFFF << shift)) | ((value & 0xFFFF) << shift);
            pci_raw_write_config(dev->bus, dev->slot, dev->func, align, val);
            break;
        }
        case 4:
        default:
            pci_raw_write_config(dev->bus, dev->slot, dev->func, offset, value);
            break;
    }
}

/* ---- 设备创建和链表操作 ---- */

static pci_device_t *pci_create_device(uint8_t bus, uint8_t slot, uint8_t func) {
    pci_device_t *dev = (pci_device_t *)kmalloc(sizeof(pci_device_t));
    if (!dev) return (void *)0;

    memset(dev, 0, sizeof(pci_device_t));
    dev->bus = bus;
    dev->slot = slot;
    dev->func = func;

    uint32_t val = pci_raw_read_config(bus, slot, func, PCI_VENDOR_ID);
    dev->vendor_id = val & 0xFFFF;
    dev->device_id = (val >> 16) & 0xFFFF;

    val = pci_raw_read_config(bus, slot, func, PCI_REVISION_ID);
    dev->revision = val & 0xFF;
    dev->class_code = (val >> 24) & 0xFF;
    dev->subclass = (val >> 16) & 0xFF;
    dev->prog_if = (val >> 8) & 0xFF;

    val = pci_raw_read_config(bus, slot, func, PCI_HEADER_TYPE);
    dev->header_type = (val >> 16) & 0xFF;

    val = pci_raw_read_config(bus, slot, func, PCI_INTERRUPT_LINE);
    dev->irq_line = val & 0xFF;
    dev->irq_pin = (val >> 8) & 0xFF;

    int i;
    for (i = 0; i < 6; i++) {
        dev->bar[i] = pci_raw_read_config(bus, slot, func, PCI_BAR0 + i * 4);
        dev->bar_size[i] = 0;
    }

    /* Probe BAR sizes */
    for (i = 0; i < 6; i++) {
        if (dev->bar[i] != 0) {
            pci_raw_write_config(bus, slot, func, PCI_BAR0 + i * 4, 0xFFFFFFFF);
            uint32_t size_val = pci_raw_read_config(bus, slot, func, PCI_BAR0 + i * 4);
            pci_raw_write_config(bus, slot, func, PCI_BAR0 + i * 4, dev->bar[i]);

            if (size_val != 0 && size_val != 0xFFFFFFFF) {
                /* I/O space BAR */
                if (dev->bar[i] & 0x01) {
                    dev->bar_size[i] = (~(size_val & 0xFFFFFFFC) + 1) & 0xFFFF;
                } else {
                    dev->bar_size[i] = ~(size_val & 0xFFFFFFF0) + 1;
                }
            }
        }
    }

    /* 插入链表尾部 */
    dev->next = (void *)0;
    if (!device_head) {
        device_head = dev;
    } else {
        pci_device_t *cur = device_head;
        while (cur->next) {
            cur = cur->next;
        }
        cur->next = dev;
    }

    device_count++;
    return dev;
}

/* ---- 自动探测：匹配驱动 ---- */

static void pci_probe_device(pci_device_t *dev) {
    pci_driver_t *drv = driver_head;
    while (drv) {
        if (drv->vendor_id == dev->vendor_id && drv->device_id == dev->device_id) {
            klog_info("[PCI] Found device %04X:%04X at %02X:%02X.%d -> driver '%s'",
                      dev->vendor_id, dev->device_id,
                      dev->bus, dev->slot, dev->func, drv->name);
            if (drv->probe) {
                int ret = drv->probe(dev);
                if (ret == 0 && drv->init) {
                    drv->init(dev);
                }
            }
        }
        drv = drv->next;
    }
}

/* ---- 扫描单个设备 ---- */

static void pci_scan_device(uint8_t bus, uint8_t slot, uint8_t func) {
    uint32_t val = pci_raw_read_config(bus, slot, func, PCI_VENDOR_ID);
    uint16_t vendor = val & 0xFFFF;
    if (vendor == 0xFFFF) return;

    pci_device_t *dev = pci_create_device(bus, slot, func);
    if (!dev) return;

    pci_probe_device(dev);
}

/* ---- 扫描所有总线 ---- */

void pci_bus_scan(void) {
    device_count = 0;

    /* 释放旧设备链表 */
    pci_device_t *cur = device_head;
    while (cur) {
        pci_device_t *next = cur->next;
        kfree(cur);
        cur = next;
    }
    device_head = (void *)0;

    uint16_t bus;
    uint8_t slot, func;
    for (bus = 0; bus < PCI_MAX_BUSES; bus++) {
        for (slot = 0; slot < PCI_MAX_SLOTS; slot++) {
            uint32_t val = pci_raw_read_config((uint8_t)bus, slot, 0, PCI_VENDOR_ID);
            if ((val & 0xFFFF) == 0xFFFF) continue;

            pci_scan_device((uint8_t)bus, slot, 0);

            /* 检查多功能设备 */
            uint32_t hdr = pci_raw_read_config((uint8_t)bus, slot, 0, PCI_HEADER_TYPE);
            uint8_t hdr_type = (hdr >> 16) & 0xFF;
            if (hdr_type & 0x80) {
                for (func = 1; func < PCI_MAX_FUNCS; func++) {
                    val = pci_raw_read_config((uint8_t)bus, slot, func, PCI_VENDOR_ID);
                    if ((val & 0xFFFF) != 0xFFFF) {
                        pci_scan_device((uint8_t)bus, slot, func);
                    }
                }
            }
        }
    }

    klog_info("[PCI] Bus scan complete: %d device(s) found", device_count);
}

void pci_bus_init(void) {
    device_head = (void *)0;
    driver_head = (void *)0;
    device_count = 0;
    pci_bus_scan();
}

/* ---- 查找设备 ---- */

pci_device_t *pci_bus_find_device(uint16_t vendor, uint16_t device) {
    pci_device_t *cur = device_head;
    while (cur) {
        if (cur->vendor_id == vendor && cur->device_id == device) {
            return cur;
        }
        cur = cur->next;
    }
    return (void *)0;
}

pci_device_t *pci_find_class(uint8_t class_code, uint8_t subclass) {
    pci_device_t *cur = device_head;
    while (cur) {
        if (cur->class_code == class_code && cur->subclass == subclass) {
            return cur;
        }
        cur = cur->next;
    }
    return (void *)0;
}

/* ---- 驱动注册 ---- */

int pci_register_driver(pci_driver_t *driver) {
    if (!driver) return -1;

    driver->next = (void *)0;

    if (!driver_head) {
        driver_head = driver;
    } else {
        pci_driver_t *cur = driver_head;
        while (cur->next) {
            cur = cur->next;
        }
        cur->next = driver;
    }

    /* 对已存在的设备进行探测匹配 */
    pci_device_t *dev = device_head;
    while (dev) {
        if (dev->vendor_id == driver->vendor_id && dev->device_id == driver->device_id) {
            klog_info("[PCI] Matching existing device %04X:%04X to driver '%s'",
                      dev->vendor_id, dev->device_id, driver->name);
            if (driver->probe) {
                driver->probe(dev);
            }
            if (driver->init) {
                driver->init(dev);
            }
        }
        dev = dev->next;
    }

    return 0;
}

/* ---- 设备操作 ---- */

void pci_enable_bus_mastering(pci_device_t *dev) {
    if (!dev) return;
    uint32_t cmd = pci_bus_read_cfg(dev, PCI_COMMAND, 4);
    cmd |= 0x07; /* I/O space + Memory space + Bus master */
    pci_bus_write_cfg(dev, PCI_COMMAND, cmd, 4);
}

void pci_set_irq(pci_device_t *dev, uint8_t irq) {
    if (!dev) return;
    pci_bus_write_cfg(dev, PCI_INTERRUPT_LINE, irq, 1);
    dev->irq_line = irq;
}

uint32_t pci_get_bar_size(pci_device_t *dev, int bar) {
    if (!dev || bar < 0 || bar > 5) return 0;
    return dev->bar_size[bar];
}

/* ---- 列出所有设备 ---- */

void pci_list_devices(void) {
    klog_info("=== PCI Device List (%d devices) ===", device_count);
    klog_info("Bus  Slot Func  Vendor  Device  Class  Subclass  IRQ");

    pci_device_t *cur = device_head;
    while (cur) {
        klog_info("%02X   %02X   %d     0x%04X  0x%04X  0x%02X   0x%02X       %d",
                  cur->bus, cur->slot, cur->func,
                  cur->vendor_id, cur->device_id,
                  cur->class_code, cur->subclass,
                  cur->irq_line);
        cur = cur->next;
    }
}

int pci_device_count(void) {
    return device_count;
}