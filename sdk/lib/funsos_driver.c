/* funsos_driver.c - 驱动信息查询子模块实现
 *
 * 桥接 SDK 驱动查询 API 到内核 PCI/devfs/net/cpufreq 子系统
 */

#include "funsos.h"
#include "funsos_driver.h"
#include "pci.h"
#include "devfs.h"
#include "net.h"
#include "cpufreq.h"
#include "version.h"
#include "string.h"

/* ---- PCI 设备查询 ---- */

int funsos_driver_list_pci(funsos_pci_device_t *devices, uint32_t max_count) {
    if (!devices || max_count == 0) return -1;
    uint32_t count = 0;
    /* 扫描前 8 个总线（与 shell.c 中 lspci 一致） */
    for (uint32_t bus = 0; bus < 8 && count < max_count; bus++) {
        for (uint32_t dev = 0; dev < 32 && count < max_count; dev++) {
            uint32_t vendor_dev = pci_read_config(bus, dev, 0, 0x00);
            if (vendor_dev == 0xFFFFFFFF || vendor_dev == 0) continue;
            uint32_t class_rev = pci_read_config(bus, dev, 0, 0x08);
            uint32_t irq_info = pci_read_config(bus, dev, 0, 0x3C);
            funsos_pci_device_t *p = &devices[count++];
            p->bus = (uint8_t)bus;
            p->dev = (uint8_t)dev;
            p->func = 0;
            p->vendor_id = (uint16_t)(vendor_dev & 0xFFFF);
            p->device_id = (uint16_t)((vendor_dev >> 16) & 0xFFFF);
            p->class_code = (uint8_t)((class_rev >> 24) & 0xFF);
            p->subclass = (uint8_t)((class_rev >> 16) & 0xFF);
            p->irq = (uint8_t)(irq_info & 0xFF);
        }
    }
    return (int)count;
}

int funsos_driver_count_pci(void) {
    int count = 0;
    for (uint32_t bus = 0; bus < 8; bus++) {
        for (uint32_t dev = 0; dev < 32; dev++) {
            uint32_t vendor_dev = pci_read_config(bus, dev, 0, 0x00);
            if (vendor_dev == 0xFFFFFFFF || vendor_dev == 0) continue;
            count++;
        }
    }
    return count;
}

/* ---- 块设备查询 ---- */

int funsos_driver_list_block(funsos_block_device_t *devices, uint32_t max_count) {
    if (!devices || max_count == 0) return -1;
    uint32_t count = 0;
    /* 枚举 devfs 中常见的块设备 */
    const char *blk_names[] = { "hda", "hdb", "hdc", "hdd", "fd0", "fd1",
                                 "sda", "sdb", "sdc", "sr0", NULL };
    for (int i = 0; blk_names[i] && count < max_count; i++) {
        devfs_device_t *dev = devfs_find(blk_names[i]);
        if (!dev || dev->type != DEVICE_BLOCK) continue;
        funsos_block_device_t *b = &devices[count++];
        strncpy(b->name, dev->name, sizeof(b->name) - 1);
        b->name[sizeof(b->name) - 1] = '\0';
        b->major = dev->major;
        b->minor = dev->minor;
        b->size_bytes = 0;  /* 内核未提供 */
        b->removable = 0;
    }
    return (int)count;
}

/* ---- 网络接口查询 ---- */

int funsos_driver_list_net(funsos_net_iface_t *ifaces, uint32_t max_count) {
    if (!ifaces || max_count == 0) return -1;
    uint32_t net_count = net_get_interface_count();
    uint32_t out = 0;
    for (uint32_t i = 0; i < net_count && out < max_count; i++) {
        net_interface_t *iface = net_get_interface(i);
        if (!iface) continue;
        funsos_net_iface_t *n = &ifaces[out++];
        strncpy(n->name, iface->name, sizeof(n->name) - 1);
        n->name[sizeof(n->name) - 1] = '\0';
        for (int k = 0; k < 6; k++) n->mac[k] = iface->mac.bytes[k];
        n->ipv4 = iface->ip.addr;
        n->mask = iface->mask.addr;
        n->gateway = iface->gateway.addr;
        n->up = iface->up;
        n->mtu = iface->mtu;
        n->rx_packets = iface->rx_packets;
        n->tx_packets = iface->tx_packets;
        n->rx_bytes = iface->rx_bytes;
        n->tx_bytes = iface->tx_bytes;
    }
    return (int)out;
}

/* ---- CPU 信息 ---- */

int funsos_driver_get_cpu_info(funsos_cpu_info_t *info) {
    if (!info) return -1;
    memset(info, 0, sizeof(*info));
    cpufreq_info_t *ci = cpufreq_get_info();
    if (ci) {
        info->current_freq_khz = ci->current_freq * 1000;
        info->max_freq_khz = ci->max_freq * 1000;
        info->min_freq_khz = ci->min_freq * 1000;
        /* 将 governor 名称映射为枚举值 */
        if (ci->governor[0] == 'p' && ci->governor[1] == 'e') {
            info->governor = 0;  /* performance */
        } else if (ci->governor[0] == 'p' && ci->governor[1] == 'o') {
            info->governor = 1;  /* powersave */
        } else if (ci->governor[0] == 'o') {
            info->governor = 2;  /* ondemand */
        } else {
            info->governor = 3;  /* conservative / unknown */
        }
    }
    info->temperature_celsius = 35;
    info->usage_percent = 0;
    return 0;
}

/* ---- 通用驱动信息 ---- */

int funsos_driver_count_modules(void) {
    /* 内置模块数量（与 lsmod 显示一致） */
    return 14;
}

const char *funsos_driver_kernel_version(void) {
    return KERNEL_VERSION;
}

int funsos_driver_get_name(int dev_type, uint32_t index, char *buf, uint32_t len) {
    if (!buf || len == 0) return -1;
    switch (dev_type) {
        case FUNSOS_DEV_PCI: {
            uint32_t cnt = 0;
            for (uint32_t bus = 0; bus < 8; bus++) {
                for (uint32_t dev = 0; dev < 32; dev++) {
                    uint32_t vd = pci_read_config(bus, dev, 0, 0x00);
                    if (vd == 0xFFFFFFFF || vd == 0) continue;
                    if (cnt == index) {
                        /* 格式: "PCI Bus:Dev.Func" */
                        char tmp[32];
                        /* 手工拼接避免 snprintf */
                        const char hex[] = "0123456789ABCDEF";
                        int p = 0;
                        tmp[p++] = 'P'; tmp[p++] = 'C'; tmp[p++] = 'I'; tmp[p++] = ' ';
                        tmp[p++] = hex[(bus >> 4) & 0xF]; tmp[p++] = hex[bus & 0xF];
                        tmp[p++] = ':';
                        tmp[p++] = hex[(dev >> 4) & 0xF]; tmp[p++] = hex[dev & 0xF];
                        tmp[p++] = '.';
                        tmp[p++] = '0';
                        tmp[p++] = '\0';
                        strncpy(buf, tmp, len - 1);
                        buf[len - 1] = '\0';
                        return 0;
                    }
                    cnt++;
                }
            }
            return -1;
        }
        case FUNSOS_DEV_BLOCK: {
            const char *blk_names[] = { "hda", "hdb", "hdc", "hdd", "fd0", "fd1",
                                         "sda", "sdb", "sdc", "sr0", NULL };
            if (index >= 10) return -1;
            if (!blk_names[index]) return -1;
            devfs_device_t *dev = devfs_find(blk_names[index]);
            if (!dev) return -1;
            strncpy(buf, dev->name, len - 1);
            buf[len - 1] = '\0';
            return 0;
        }
        case FUNSOS_DEV_NET: {
            net_interface_t *iface = net_get_interface(index);
            if (!iface) return -1;
            strncpy(buf, iface->name, len - 1);
            buf[len - 1] = '\0';
            return 0;
        }
        default:
            return -1;
    }
}
