#include "ahci.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "kheap.h"
#include "sync.h"
#include "string.h"
#include "stdio.h"
#include "stddef.h"
#include "irq.h"
#include "io.h"

static ahci_controller_t ahci_ctrl;

static uint32_t ahci_virt_to_phys(void *virt) {
    return vmm_get_physical(vmm_get_current_dir(), (uint32_t)(uintptr_t)virt);
}

static void *ahci_phys_to_virt(uint32_t phys) {
    return (void *)(phys + VMM_KERNEL_BASE);
}

int ahci_find_cmd_slot(hba_port_t *port) {
    uint32_t slots = port->sact | port->ci;
    for (int i = 0; i < 32; i++) {
        if (!(slots & (1 << i))) {
            return i;
        }
    }
    return -1;
}

static int ahci_check_type(hba_port_t *port) {
    uint32_t ssts = port->ssts;
    uint8_t det = ssts & 0x0F;
    uint8_t ipm = (ssts >> 8) & 0x0F;

    if (det != AHCI_PORT_SSTS_DET_PRESENT) return AHCI_DEV_NULL;
    if (ipm != AHCI_PORT_SSTS_IPM_ACTIVE && ipm != 0) return AHCI_DEV_NULL;

    switch (port->sig) {
        case 0x00000101:
            return AHCI_DEV_SATA;
        case 0xEB140101:
            return AHCI_DEV_SATAPI;
        default:
            return AHCI_DEV_NULL;
    }
}

static int ahci_port_rebase(hba_port_t *port, ahci_device_t *dev) {
    port->cmd &= ~AHCI_PORT_CMD_ST;
    while (port->cmd & AHCI_PORT_CMD_CR);

    port->cmd &= ~AHCI_PORT_CMD_FRE;
    while (port->cmd & AHCI_PORT_CMD_FR);

    void *cmd_list = pmm_alloc_page();
    if (!cmd_list) return -1;
    memset(cmd_list, 0, 4096);

    void *fis = pmm_alloc_page();
    if (!fis) {
        pmm_free_page(cmd_list);
        return -1;
    }
    memset(fis, 0, 4096);

    port->clb = ahci_virt_to_phys(cmd_list);
    port->clbu = 0;
    port->fb = ahci_virt_to_phys(fis);
    port->fbu = 0;
    port->serr = 0xFFFFFFFF;

    port->cmd |= AHCI_PORT_CMD_FRE;
    port->cmd |= AHCI_PORT_CMD_ST;

    dev->cmd_list = (ahci_cmd_header_t *)cmd_list;
    dev->fis = (uint32_t *)fis;

    return 0;
}

static int ahci_setup_prdt(ahci_cmd_header_t *cmd, void *buf, uint32_t len) {
    uint32_t remaining = len;
    uint8_t *current = (uint8_t *)buf;
    uint32_t prdt_count = 0;

    void *prdt_page = pmm_alloc_page();
    if (!prdt_page) return -1;
    memset(prdt_page, 0, 4096);

    ahci_prdt_entry_t *prdt = (ahci_prdt_entry_t *)prdt_page;

    while (remaining > 0 && prdt_count < AHCI_PRDT_MAX_ENTRIES) {
        uint32_t chunk = remaining;
        if (chunk > 0x400000) chunk = 0x400000;

        prdt[prdt_count].dba = ahci_virt_to_phys((void *)current);
        prdt[prdt_count].dbau = 0;
        prdt[prdt_count].dbc = chunk - 1;
        prdt[prdt_count].i = 1;

        current += chunk;
        remaining -= chunk;
        prdt_count++;
    }

    cmd->prdt = (uint64_t)ahci_virt_to_phys(prdt_page);
    return prdt_count;
}

static int ahci_exec_cmd(uint8_t port, uint8_t cmd, uint64_t lba, uint32_t count, void *buf, int write) {
    if (port >= AHCI_MAX_PORTS || !ahci_ctrl.devices[port].initialized) {
        return -1;
    }

    ahci_device_t *dev = &ahci_ctrl.devices[port];
    hba_port_t *hba_port = dev->port;

    mutex_lock(&dev->lock);

    int slot = ahci_find_cmd_slot(hba_port);
    if (slot < 0) {
        mutex_unlock(&dev->lock);
        return -1;
    }

    ahci_cmd_header_t *cmd_header = &dev->cmd_list[slot];
    memset(cmd_header, 0, sizeof(ahci_cmd_header_t));

    cmd_header->cfl = sizeof(h2d_fis_t) / 4;
    cmd_header->w = write ? 1 : 0;
    cmd_header->a = 1;
    cmd_header->prdtl = 0;

    if (count > 0 && buf) {
        int prdt_count = ahci_setup_prdt(cmd_header, buf, count * 512);
        if (prdt_count < 0) {
            mutex_unlock(&dev->lock);
            return -1;
        }
        cmd_header->prdtl = prdt_count;
    }

    h2d_fis_t *fis = (h2d_fis_t *)(&cmd_header->cfis);
    memset(fis, 0, sizeof(h2d_fis_t));
    fis->fis_type = AHCI_FIS_TYPE_REG_H2D;
    fis->c = 1;
    fis->command = cmd;
    fis->device = 1 << 6;
    fis->lba0 = (uint8_t)lba;
    fis->lba1 = (uint8_t)(lba >> 8);
    fis->lba2 = (uint8_t)(lba >> 16);
    fis->lba3 = (uint8_t)(lba >> 24);
    fis->lba4 = (uint8_t)(lba >> 32);
    fis->lba5 = (uint8_t)(lba >> 40);
    fis->count_l = (uint8_t)count;
    fis->count_h = (uint8_t)(count >> 8);

    hba_port->is = (uint32_t)-1;
    hba_port->ci = 1 << slot;

    uint32_t timeout = 0x1000000;
    while ((hba_port->ci & (1 << slot)) && timeout > 0) {
        timeout--;
    }

    if (timeout == 0) {
        mutex_unlock(&dev->lock);
        return -1;
    }

    uint32_t is = hba_port->is;
    if (is & (1 << 30)) {
        mutex_unlock(&dev->lock);
        return -1;
    }

    if (cmd_header->prdtl > 0) {
        pmm_free_page((void *)((uint32_t)cmd_header->prdt & 0xFFFFF000));
    }

    mutex_unlock(&dev->lock);
    return 0;
}

int ahci_read(uint8_t port, uint64_t lba, uint32_t count, void *buf) {
    return ahci_exec_cmd(port, ATA_CMD_READ_DMA_EXT, lba, count, buf, 0);
}

int ahci_write(uint8_t port, uint64_t lba, uint32_t count, const void *buf) {
    return ahci_exec_cmd(port, ATA_CMD_WRITE_DMA_EXT, lba, count, (void *)buf, 1);
}

int ahci_identify_device(uint8_t port) {
    if (port >= AHCI_MAX_PORTS || !ahci_ctrl.devices[port].initialized) {
        return -1;
    }

    uint16_t *identify_data = (uint16_t *)pmm_alloc_page();
    if (!identify_data) return -1;
    memset(identify_data, 0, 512);

    int result = ahci_exec_cmd(port, ATA_CMD_IDENTIFY, 0, 1, identify_data, 0);
    if (result == 0) {
        ahci_device_t *dev = &ahci_ctrl.devices[port];
        dev->sector_count = ((uint64_t)identify_data[103] << 48) |
                            ((uint64_t)identify_data[102] << 32) |
                            ((uint64_t)identify_data[101] << 16) |
                            identify_data[100];
        dev->sector_size = 512;
    }

    pmm_free_page(identify_data);
    return result;
}

int ahci_flush_cache(uint8_t port) {
    return ahci_exec_cmd(port, ATA_CMD_FLUSH_CACHE, 0, 0, NULL, 1);
}

void ahci_reset_controller(void) {
    if (!ahci_ctrl.initialized) return;

    hba_mem_t *hba = ahci_ctrl.hba_mem;
    hba->ghc |= AHCI_GHC_HR;

    uint32_t timeout = 0x100000;
    while ((hba->ghc & AHCI_GHC_HR) && timeout > 0) {
        timeout--;
    }
}

int ahci_reset_port(uint8_t port) {
    if (port >= AHCI_MAX_PORTS || !ahci_ctrl.devices[port].initialized) {
        return -1;
    }

    hba_port_t *hba_port = ahci_ctrl.devices[port].port;
    hba_port->sctl &= ~0x0000000F;
    hba_port->sctl |= 0x00000001;

    uint32_t timeout = 0x100000;
    while (timeout > 0) {
        if ((hba_port->ssts & 0x0F) == AHCI_PORT_SSTS_DET_PRESENT) {
            break;
        }
        timeout--;
    }

    hba_port->sctl &= ~0x0000000F;

    if (timeout == 0) {
        return -1;
    }

    return 0;
}

void ahci_port_interrupt_handler(int port) {
    if (port < 0 || port >= AHCI_MAX_PORTS || !ahci_ctrl.devices[port].initialized) {
        return;
    }

    ahci_device_t *dev = &ahci_ctrl.devices[port];
    hba_port_t *hba_port = dev->port;

    uint32_t is = hba_port->is;
    uint32_t serr = hba_port->serr;

    if (is & 0x01) {
        /* DHRS - Device to Host Register FIS Interrupt */
        d2h_fis_t *d2h = (d2h_fis_t *)dev->fis;
        if (d2h->fis_type == AHCI_FIS_TYPE_REG_D2H) {
            uint8_t status = d2h->status;
            if (status & 0x01) {
                /* Check for error bit */
                dev->cmd_error = d2h->error;
                dev->cmd_complete = 0;
            } else {
                dev->cmd_complete = 1;
                dev->cmd_error = 0;
            }
        }
    }

    if (is & 0x02) {
        /* PSS - PIO Setup FIS Interrupt */
        pio_setup_fis_t *pio = (pio_setup_fis_t *)dev->fis;
        if (pio->fis_type == AHCI_FIS_TYPE_PIO_SETUP) {
            dev->cmd_complete = 1;
            dev->cmd_error = 0;
        }
    }

    if (is & 0x04) {
        /* DSS - DMA Setup FIS Interrupt */
        dma_setup_fis_t *dma = (dma_setup_fis_t *)dev->fis;
        if (dma->fis_type == AHCI_FIS_TYPE_DMA_SETUP) {
            dev->cmd_complete = 1;
            dev->cmd_error = 0;
        }
    }

    /* Clear IS and SERR by writing 1s */
    hba_port->is = is;
    hba_port->serr = serr;
}

void ahci_irq_handler(regs_t *regs) {
    (void)regs;

    if (!ahci_ctrl.initialized) return;

    hba_mem_t *hba = ahci_ctrl.hba_mem;
    uint32_t ghc_is = hba->is;

    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        if (!(ghc_is & (1 << i))) continue;
        if (!ahci_ctrl.devices[i].initialized) continue;

        ahci_port_interrupt_handler(i);
    }
}

int ahci_init(void) {
    if (ahci_ctrl.initialized) {
        return 0;
    }

    pci_device_t *pci_dev = pci_find_device(0x01, 0x06);
    if (!pci_dev) {
        return -1;
    }

    uint32_t abar = pci_read_config(pci_dev->bus, pci_dev->device, pci_dev->function, 0x24);
    abar &= 0xFFFFFFF0;

    ahci_ctrl.mmio_base = abar;
    ahci_ctrl.hba_mem = (hba_mem_t *)vmm_map_physical(abar, 0x2000);
    if (!ahci_ctrl.hba_mem) {
        return -1;
    }

    mutex_init(&ahci_ctrl.hba_lock);
    mutex_lock(&ahci_ctrl.hba_lock);

    hba_mem_t *hba = ahci_ctrl.hba_mem;
    hba->ghc |= AHCI_GHC_AE;
    hba->ghc |= AHCI_GHC_IE;

    ahci_ctrl.port_count = 0;
    uint32_t pi = hba->pi;

    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        if (!(pi & (1 << i))) continue;

        int type = ahci_check_type(&hba->ports[i]);
        if (type == AHCI_DEV_NULL) continue;

        ahci_device_t *dev = &ahci_ctrl.devices[i];
        mutex_init(&dev->lock);

        if (ahci_port_rebase(&hba->ports[i], dev) != 0) {
            continue;
        }

        dev->port_num = i;
        dev->signature = hba->ports[i].sig;
        dev->port = &hba->ports[i];
        dev->initialized = 1;
        dev->cmd_complete = 0;
        dev->cmd_error = 0;

        /* Enable all interrupts on this port */
        hba->ports[i].ie = 0xFFFFFFFF;

        ahci_identify_device(i);

        ahci_ctrl.port_count++;
    }

    /* Read IRQ from PCI configuration and register handler */
    uint32_t irq_line = pci_read_config(pci_dev->bus, pci_dev->device, pci_dev->function, 0x3C);
    uint8_t irq = (uint8_t)(irq_line & 0xFF);
    if (irq != 0xFF && irq < 16) {
        irq_register_handler(irq, ahci_irq_handler);
    }

    ahci_ctrl.initialized = 1;
    mutex_unlock(&ahci_ctrl.hba_lock);

    return 0;
}
