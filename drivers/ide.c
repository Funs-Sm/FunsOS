#include "ide.h"
#include "kheap.h"
#include "sync.h"
#include "io.h"

static ide_device_t devices[IDE_MAX_DRIVES];

static void ide_wait_bsy(uint16_t port) {
    while (inb(port + 7) & 0x80);
}

static void ide_wait_drq(uint16_t port) {
    while (!(inb(port + 7) & 0x08));
}

static uint8_t ide_read_reg(uint16_t port, uint8_t reg) {
    return inb(port + reg);
}

static void ide_write_reg(uint16_t port, uint8_t reg, uint8_t val) {
    outb(port + reg, val);
}

int ide_identify(uint8_t drive, ide_device_t *dev) {
    uint16_t base, ctrl;
    uint8_t master;
    uint16_t id_data[256];

    if (drive >= IDE_MAX_DRIVES) return -1;

    if (drive < 2) {
        base = IDE_PRIMARY_BASE;
        ctrl = IDE_PRIMARY_CTRL;
        master = (drive == 0) ? 0 : 1;
    } else {
        base = IDE_SECONDARY_BASE;
        ctrl = IDE_SECONDARY_CTRL;
        master = (drive == 2) ? 0 : 1;
    }

    ide_wait_bsy(base);

    if (master) {
        ide_write_reg(base, 6, 0xB0);
    } else {
        ide_write_reg(base, 6, 0xA0);
    }

    ide_write_reg(base, 7, 0xEC);

    uint8_t status = ide_read_reg(base, 7);
    if (status == 0) return -1;

    ide_wait_bsy(base);

    status = ide_read_reg(base, 7);
    if (status & 0x01) return -1;

    ide_wait_drq(base);

    for (int i = 0; i < 256; i++) {
        id_data[i] = inw(base);
    }

    for (int i = 0; i < 40; i += 2) {
        dev->model[i] = (id_data[27 + i / 2] >> 8) & 0xFF;
        dev->model[i + 1] = id_data[27 + i / 2] & 0xFF;
    }
    dev->model[40] = '\0';

    dev->cylinders = id_data[1];
    dev->heads = id_data[3];
    dev->sectors = id_data[6];
    dev->size_sectors = id_data[60] | ((uint32_t)id_data[61] << 16);
    dev->type = 1;
    dev->base_port = base;
    dev->ctrl_port = ctrl;
    dev->master = master;

    devices[drive] = *dev;
    return 0;
}

int ide_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, void *buf) {
    if (drive >= IDE_MAX_DRIVES || devices[drive].type == 0) return -1;

    uint16_t base = devices[drive].base_port;
    uint8_t master = devices[drive].master;

    ide_wait_bsy(base);

    ide_write_reg(base, 2, count);
    ide_write_reg(base, 3, (uint8_t)(lba & 0xFF));
    ide_write_reg(base, 4, (uint8_t)((lba >> 8) & 0xFF));
    ide_write_reg(base, 5, (uint8_t)((lba >> 16) & 0xFF));

    uint8_t drive_head = 0xE0 | (master << 4) | ((lba >> 24) & 0x0F);
    ide_write_reg(base, 6, drive_head);

    ide_write_reg(base, 7, 0x20);

    uint32_t *buf32 = (uint32_t *)buf;
    for (int s = 0; s < count; s++) {
        ide_wait_bsy(base);
        ide_wait_drq(base);

        uint8_t status = ide_read_reg(base, 7);
        if (status & 0x01) return -1;

        for (int i = 0; i < IDE_SECTOR_SIZE / 4; i++) {
            buf32[s * (IDE_SECTOR_SIZE / 4) + i] = inl(base);
        }
    }

    return 0;
}

int ide_write_sectors(uint8_t drive, uint32_t lba, uint8_t count, const void *buf) {
    if (drive >= IDE_MAX_DRIVES || devices[drive].type == 0) return -1;

    uint16_t base = devices[drive].base_port;
    uint8_t master = devices[drive].master;

    ide_wait_bsy(base);

    ide_write_reg(base, 2, count);
    ide_write_reg(base, 3, (uint8_t)(lba & 0xFF));
    ide_write_reg(base, 4, (uint8_t)((lba >> 8) & 0xFF));
    ide_write_reg(base, 5, (uint8_t)((lba >> 16) & 0xFF));

    uint8_t drive_head = 0xE0 | (master << 4) | ((lba >> 24) & 0x0F);
    ide_write_reg(base, 6, drive_head);

    ide_write_reg(base, 7, 0x30);

    const uint32_t *buf32 = (const uint32_t *)buf;
    for (int s = 0; s < count; s++) {
        ide_wait_bsy(base);
        ide_wait_drq(base);

        for (int i = 0; i < IDE_SECTOR_SIZE / 4; i++) {
            outl(base, buf32[s * (IDE_SECTOR_SIZE / 4) + i]);
        }
    }

    ide_wait_bsy(base);
    ide_write_reg(base, 7, 0xE7);
    ide_wait_bsy(base);

    return 0;
}

void ide_init(void) {
    ide_device_t dev;

    for (int i = 0; i < IDE_MAX_DRIVES; i++) {
        devices[i].type = 0;
    }

    for (int i = 0; i < IDE_MAX_DRIVES; i++) {
        if (ide_identify(i, &dev) != 0) {
            devices[i].type = 0;
        }
    }
}
