#include "usb_storage.h"
#include "usb_core.h"
#include "disk_manager.h"
#include "kheap.h"
#include "string.h"
#include "stdio.h"

static usb_storage_dev_t *storage_devices[8];
static int usb_disk_index = 0;  /* For naming: sda, sdb, sdc... */

typedef struct {
    uint32_t signature;
    uint32_t tag;
    uint32_t transfer_length;
    uint8_t flags;
    uint8_t lun;
    uint8_t cb_length;
    uint8_t cb[16];
} usb_cbw_t;

typedef struct {
    uint32_t signature;
    uint32_t tag;
    uint32_t data_residue;
    uint8_t status;
} usb_csw_t;

static uint32_t cbw_tag = 1;

/* Helper: find storage device by dev_addr */
usb_storage_dev_t *usb_storage_get_by_addr(uint8_t dev_addr) {
    for (int i = 0; i < 8; i++) {
        if (storage_devices[i] && storage_devices[i]->dev_addr == dev_addr) {
            return storage_devices[i];
        }
    }
    return 0;
}

/* Helper: send CBW + data + CSW for a SCSI command */
static int usb_storage_send_scsi(usb_storage_dev_t *dev, uint8_t *cb,
                                  uint8_t cb_len, void *data, uint32_t data_len,
                                  uint8_t direction) {
    /* Build CBW */
    usb_cbw_t cbw;
    memset(&cbw, 0, sizeof(usb_cbw_t));
    cbw.signature = 0x43425355;  /* "USBC" */
    cbw.tag = cbw_tag++;
    cbw.transfer_length = data_len;
    cbw.flags = direction;  /* 0x80 = IN, 0x00 = OUT */
    cbw.lun = dev->lun;
    cbw.cb_length = cb_len;
    memcpy(cbw.cb, cb, cb_len);

    /* Send CBW */
    usb_bulk_transfer(dev->dev_addr, dev->ep_out, &cbw, 31);

    /* Send/receive data if any */
    if (data_len > 0 && data) {
        if (direction == 0x80) {
            usb_bulk_transfer(dev->dev_addr, dev->ep_in, data, data_len);
        } else {
            usb_bulk_transfer(dev->dev_addr, dev->ep_out, data, data_len);
        }
    }

    /* Receive CSW */
    usb_csw_t csw;
    memset(&csw, 0, sizeof(usb_csw_t));
    usb_bulk_transfer(dev->dev_addr, dev->ep_in, &csw, 13);

    if (csw.signature != 0x53425355) return -1;
    return (csw.status == 0) ? 0 : -1;
}

void usb_storage_init(void) {
    for (uint8_t addr = 1; addr < USB_MAX_DEVICES; addr++) {
        usb_device_t *dev = usb_get_device(addr);
        if (!dev) continue;
        if (dev->class_code == 0x08) {
            usb_storage_probe(dev);
        }
    }
}

/* Disk manager read/write callbacks */
static int usb_disk_read_sectors(disk_info_t *disk, uint64_t lba, uint32_t count, void *buf) {
    if (!disk || !disk->driver_data) return -1;
    usb_storage_dev_t *stor = (usb_storage_dev_t *)disk->driver_data;
    return usb_storage_read(stor, lba, count, buf);
}

static int usb_disk_write_sectors(disk_info_t *disk, uint64_t lba, uint32_t count, const void *buf) {
    if (!disk || !disk->driver_data) return -1;
    usb_storage_dev_t *stor = (usb_storage_dev_t *)disk->driver_data;
    return usb_storage_write(stor, lba, count, buf);
}

int usb_storage_probe(usb_device_t *dev) {
    usb_storage_dev_t *stor = (usb_storage_dev_t *)kmalloc(sizeof(usb_storage_dev_t));
    memset(stor, 0, sizeof(usb_storage_dev_t));
    stor->dev_addr = dev->address;
    stor->interface = 0;
    stor->ep_in = 0x81;
    stor->ep_out = 0x01;
    stor->block_size = 512;
    stor->lun = 0;

    usb_control_transfer(dev->address, 0xA1, 0xFE, 0, stor->interface, &stor->lun, 1);

    /* Wait for device to be ready */
    int retries = 5;
    while (retries-- > 0) {
        if (usb_storage_test_unit_ready(dev->address) == 0) break;
    }

    usb_storage_get_capacity(stor);

    /* Find a free slot */
    int slot = -1;
    for (int i = 0; i < 8; i++) {
        if (!storage_devices[i]) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        kfree(stor);
        return -1;
    }
    storage_devices[slot] = stor;

    /* Register with disk_manager */
    disk_info_t *disk = (disk_info_t *)kmalloc(sizeof(disk_info_t));
    if (disk) {
        memset(disk, 0, sizeof(disk_info_t));

        /* Generate name: sda, sdb, sdc, ... */
        disk->name[0] = 's';
        disk->name[1] = 'd';
        disk->name[2] = 'a' + usb_disk_index;
        disk->name[3] = '\0';
        usb_disk_index++;

        disk->type = DISK_TYPE_USB;
        disk->sector_size = stor->block_size;
        disk->sector_count = stor->block_count;
        disk->block_size = stor->block_size;
        disk->driver_data = stor;
        disk->read_sectors = usb_disk_read_sectors;
        disk->write_sectors = usb_disk_write_sectors;

        if (disk_register(disk) == 0) {
            stor->disk_registered = 1;
        } else {
            kfree(disk);
        }
    }

    return 0;
}

int usb_storage_read(usb_storage_dev_t *dev, uint64_t lba, uint32_t count, void *buf) {
    usb_cbw_t cbw;
    memset(&cbw, 0, sizeof(usb_cbw_t));
    cbw.signature = 0x43425355;
    cbw.tag = cbw_tag++;
    cbw.transfer_length = count * dev->block_size;
    cbw.flags = 0x80;
    cbw.lun = dev->lun;
    cbw.cb_length = 10;
    cbw.cb[0] = 0x28;  /* READ(10) */
    cbw.cb[2] = (lba >> 24) & 0xFF;
    cbw.cb[3] = (lba >> 16) & 0xFF;
    cbw.cb[4] = (lba >> 8) & 0xFF;
    cbw.cb[5] = lba & 0xFF;
    cbw.cb[7] = (count >> 8) & 0xFF;
    cbw.cb[8] = count & 0xFF;

    usb_bulk_transfer(dev->dev_addr, dev->ep_out, &cbw, 31);
    usb_bulk_transfer(dev->dev_addr, dev->ep_in, buf, count * dev->block_size);

    usb_csw_t csw;
    memset(&csw, 0, sizeof(usb_csw_t));
    usb_bulk_transfer(dev->dev_addr, dev->ep_in, &csw, 13);

    if (csw.signature != 0x53425355) return -1;
    return (csw.status == 0) ? 0 : -1;
}

int usb_storage_write(usb_storage_dev_t *dev, uint64_t lba, uint32_t count, const void *buf) {
    usb_cbw_t cbw;
    memset(&cbw, 0, sizeof(usb_cbw_t));
    cbw.signature = 0x43425355;
    cbw.tag = cbw_tag++;
    cbw.transfer_length = count * dev->block_size;
    cbw.flags = 0x00;
    cbw.lun = dev->lun;
    cbw.cb_length = 10;
    cbw.cb[0] = 0x2A;  /* WRITE(10) */
    cbw.cb[2] = (lba >> 24) & 0xFF;
    cbw.cb[3] = (lba >> 16) & 0xFF;
    cbw.cb[4] = (lba >> 8) & 0xFF;
    cbw.cb[5] = lba & 0xFF;
    cbw.cb[7] = (count >> 8) & 0xFF;
    cbw.cb[8] = count & 0xFF;

    usb_bulk_transfer(dev->dev_addr, dev->ep_out, &cbw, 31);
    usb_bulk_transfer(dev->dev_addr, dev->ep_out, (void *)buf, count * dev->block_size);

    usb_csw_t csw;
    memset(&csw, 0, sizeof(usb_csw_t));
    usb_bulk_transfer(dev->dev_addr, dev->ep_in, &csw, 13);

    if (csw.signature != 0x53425355) return -1;
    return (csw.status == 0) ? 0 : -1;
}

int usb_storage_get_capacity(usb_storage_dev_t *dev) {
    usb_cbw_t cbw;
    memset(&cbw, 0, sizeof(usb_cbw_t));
    cbw.signature = 0x43425355;
    cbw.tag = cbw_tag++;
    cbw.transfer_length = 8;
    cbw.flags = 0x80;
    cbw.lun = dev->lun;
    cbw.cb_length = 10;
    cbw.cb[0] = 0x25;  /* READ CAPACITY */

    usb_bulk_transfer(dev->dev_addr, dev->ep_out, &cbw, 31);

    uint8_t cap_data[8];
    memset(cap_data, 0, 8);
    usb_bulk_transfer(dev->dev_addr, dev->ep_in, cap_data, 8);

    usb_csw_t csw;
    memset(&csw, 0, sizeof(usb_csw_t));
    usb_bulk_transfer(dev->dev_addr, dev->ep_in, &csw, 13);

    uint32_t last_lba = (cap_data[0] << 24) | (cap_data[1] << 16) | (cap_data[2] << 8) | cap_data[3];
    uint32_t block_len = (cap_data[4] << 24) | (cap_data[5] << 16) | (cap_data[6] << 8) | cap_data[7];
    dev->block_size = block_len;
    dev->block_count = (uint64_t)(last_lba + 1);
    dev->capacity = dev->block_count * block_len;

    return 0;
}

void usb_storage_remove(usb_device_t *dev) {
    if (!dev) return;

    for (int i = 0; i < 8; i++) {
        if (storage_devices[i] && storage_devices[i]->dev_addr == dev->address) {
            /* Unregister from disk_manager if registered */
            if (storage_devices[i]->disk_registered) {
                /* Find and unregister the disk - search by driver_data pointer */
                for (uint32_t d = 0; d < disk_get_count(); d++) {
                    disk_info_t *disk = disk_get_by_index(d);
                    if (disk && disk->driver_data == storage_devices[i]) {
                        disk_unregister(disk->name);
                        kfree(disk);
                        break;
                    }
                }
            }
            kfree(storage_devices[i]);
            storage_devices[i] = (void *)0;
            return;
        }
    }
}

/* ---- New SCSI command functions ---- */

int usb_storage_read_capacity(uint8_t dev_addr, uint32_t *block_count, uint32_t *block_size) {
    usb_storage_dev_t *dev = usb_storage_get_by_addr(dev_addr);
    if (!dev) return -1;

    uint8_t cb[16];
    memset(cb, 0, 16);
    cb[0] = 0x25;  /* READ CAPACITY */

    uint8_t cap_data[8];
    memset(cap_data, 0, 8);

    int ret = usb_storage_send_scsi(dev, cb, 10, cap_data, 8, 0x80);
    if (ret != 0) return -1;

    uint32_t last_lba = (cap_data[0] << 24) | (cap_data[1] << 16) |
                        (cap_data[2] << 8) | cap_data[3];
    uint32_t blk_sz = (cap_data[4] << 24) | (cap_data[5] << 16) |
                      (cap_data[6] << 8) | cap_data[7];

    if (block_count) *block_count = last_lba + 1;
    if (block_size) *block_size = blk_sz;

    dev->block_count = (uint64_t)(last_lba + 1);
    dev->block_size = blk_sz;
    dev->capacity = dev->block_count * blk_sz;

    return 0;
}

int usb_storage_read_blocks(uint8_t dev_addr, uint64_t lba, uint32_t count, void *buf) {
    usb_storage_dev_t *dev = usb_storage_get_by_addr(dev_addr);
    if (!dev) return -1;
    return usb_storage_read(dev, lba, count, buf);
}

int usb_storage_write_blocks(uint8_t dev_addr, uint64_t lba, uint32_t count, const void *buf) {
    usb_storage_dev_t *dev = usb_storage_get_by_addr(dev_addr);
    if (!dev) return -1;
    return usb_storage_write(dev, lba, count, buf);
}

int usb_storage_test_unit_ready(uint8_t dev_addr) {
    usb_storage_dev_t *dev = usb_storage_get_by_addr(dev_addr);
    if (!dev) return -1;

    uint8_t cb[16];
    memset(cb, 0, 16);
    cb[0] = 0x00;  /* TEST UNIT READY */

    return usb_storage_send_scsi(dev, cb, 6, 0, 0, 0x80);
}

int usb_storage_request_sense(uint8_t dev_addr, void *buf) {
    usb_storage_dev_t *dev = usb_storage_get_by_addr(dev_addr);
    if (!dev) return -1;

    uint8_t cb[16];
    memset(cb, 0, 16);
    cb[0] = 0x03;  /* REQUEST SENSE */
    cb[4] = 18;    /* Allocation length */

    return usb_storage_send_scsi(dev, cb, 6, buf, 18, 0x80);
}
