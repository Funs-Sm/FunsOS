#ifndef AHCI_H
#define AHCI_H

#include "stdint.h"
#include "sync.h"
#include "kernel_types.h"

#define AHCI_MAX_PORTS 32
#define AHCI_MAX_CMD_SLOTS 32
#define AHCI_GHC_AE    0x80000000
#define AHCI_GHC_IE    0x00000002
#define AHCI_GHC_HR    0x00000001

#define AHCI_PORT_CMD_ST  0x00000001
#define AHCI_PORT_CMD_FRE 0x00000010
#define AHCI_PORT_CMD_FR  0x00008000
#define AHCI_PORT_CMD_CR  0x00004000

#define AHCI_PORT_SSTS_DET_PRESENT 0x03
#define AHCI_PORT_SSTS_IPM_ACTIVE 0x0100

#define AHCI_FIS_TYPE_REG_H2D  0x27
#define AHCI_FIS_TYPE_REG_D2H  0x34
#define AHCI_FIS_TYPE_DMA_SETUP 0x41
#define AHCI_FIS_TYPE_PIO_SETUP 0x5F
#define AHCI_FIS_TYPE_DATA      0x46
#define AHCI_FIS_TYPE_BIST      0x58

#define ATA_CMD_READ_DMA_EXT   0x25
#define ATA_CMD_WRITE_DMA_EXT  0x35
#define ATA_CMD_IDENTIFY       0xEC
#define ATA_CMD_FLUSH_CACHE    0xE7
#define ATA_CMD_READ_SECTORS_EXT 0x24
#define ATA_CMD_WRITE_SECTORS_EXT 0x34

#define AHCI_DEV_NULL 0
#define AHCI_DEV_SATA 1
#define AHCI_DEV_SEMB 2
#define AHCI_DEV_PM   3
#define AHCI_DEV_SATAPI 4

#define AHCI_PRDT_MAX_ENTRIES 8192

typedef struct {
    uint32_t clb;
    uint32_t clbu;
    uint32_t fb;
    uint32_t fbu;
    uint32_t is;
    uint32_t ie;
    uint32_t cmd;
    uint32_t rsv0;
    uint32_t tfd;
    uint32_t sig;
    uint32_t ssts;
    uint32_t sctl;
    uint32_t serr;
    uint32_t sact;
    uint32_t ci;
    uint32_t sntf;
    uint32_t fbs;
    uint32_t rsv1[11];
    uint32_t vendor[4];
} hba_port_t;

typedef struct {
    uint32_t cap;
    uint32_t ghc;
    uint32_t is;
    uint32_t pi;
    uint32_t vs;
    uint32_t ccc_ctl;
    uint32_t ccc_pts;
    uint32_t em_loc;
    uint32_t em_ctl;
    uint32_t cap2;
    uint32_t bohc;
    uint32_t rsv[116];
    uint32_t vendor[24];
    hba_port_t ports[1];
} hba_mem_t;

typedef struct {
    uint8_t  cfl : 5;
    uint8_t  a   : 1;
    uint8_t  w   : 1;
    uint8_t  p   : 1;
    uint16_t prdtl;
    uint32_t prdtc;
    uint8_t  cfis[64];
    uint8_t  acmd[16];
    uint8_t  rsv[48];
    uint64_t prdt;
} __attribute__((packed)) ahci_cmd_header_t;

typedef struct {
    uint32_t dba;
    uint32_t dbau;
    uint32_t rsv;
    uint32_t dbc : 22;
    uint32_t rsv1 : 9;
    uint32_t i : 1;
} __attribute__((packed)) ahci_prdt_entry_t;

typedef struct {
    uint8_t  fis_type;
    uint8_t  pmport : 4;
    uint8_t  rsv0 : 3;
    uint8_t  c : 1;
    uint8_t  command;
    uint8_t  feature_l;
    uint8_t  lba0;
    uint8_t  lba1;
    uint8_t  lba2;
    uint8_t  device;
    uint8_t  lba3;
    uint8_t  lba4;
    uint8_t  lba5;
    uint8_t  feature_h;
    uint8_t  count_l;
    uint8_t  count_h;
    uint8_t  icc;
    uint8_t  control;
    uint8_t  rsv1[4];
} __attribute__((packed)) h2d_fis_t;

typedef struct {
    uint8_t  fis_type;
    uint8_t  pmport : 4;
    uint8_t  rsv0 : 2;
    uint8_t  i : 1;
    uint8_t  rsv1 : 1;
    uint8_t  status;
    uint8_t  error;
    uint8_t  lba0;
    uint8_t  lba1;
    uint8_t  lba2;
    uint8_t  device;
    uint8_t  lba3;
    uint8_t  lba4;
    uint8_t  lba5;
    uint8_t  rsv2;
    uint8_t  count_l;
    uint8_t  count_h;
    uint8_t  rsv3[6];
} __attribute__((packed)) d2h_fis_t;

typedef struct {
    uint8_t  fis_type;
    uint8_t  pmport : 4;
    uint8_t  rsv0 : 1;
    uint8_t  d : 1;
    uint8_t  i : 1;
    uint8_t  rsv1 : 1;
    uint8_t  rsv2;
    uint32_t dma_buffer_id_low;
    uint32_t dma_buffer_id_high;
    uint32_t rsv3;
    uint32_t dma_offset;
    uint32_t transfer_count;
    uint32_t rsv4;
} __attribute__((packed)) dma_setup_fis_t;

typedef struct {
    uint8_t  fis_type;
    uint8_t  pmport : 4;
    uint8_t  rsv0 : 1;
    uint8_t  d : 1;
    uint8_t  i : 1;
    uint8_t  rsv1 : 1;
    uint8_t  status;
    uint8_t  error;
    uint8_t  lba0;
    uint8_t  lba1;
    uint8_t  lba2;
    uint8_t  device;
    uint8_t  lba3;
    uint8_t  lba4;
    uint8_t  lba5;
    uint8_t  rsv2;
    uint16_t transfer_count;
    uint8_t  rsv3[2];
} __attribute__((packed)) pio_setup_fis_t;

typedef struct {
    uint32_t port_num;
    uint32_t signature;
    uint64_t sector_count;
    uint32_t sector_size;
    hba_port_t *port;
    ahci_cmd_header_t *cmd_list;
    uint32_t *fis;
    mutex_t lock;
    uint8_t initialized;
    volatile uint8_t cmd_complete;
    uint8_t cmd_error;
} ahci_device_t;

typedef struct {
    hba_mem_t *hba_mem;
    ahci_device_t devices[AHCI_MAX_PORTS];
    uint32_t port_count;
    mutex_t hba_lock;
    uint32_t mmio_base;
    uint8_t initialized;
} ahci_controller_t;

int ahci_init(void);
int ahci_read(uint8_t port, uint64_t lba, uint32_t count, void *buf);
int ahci_write(uint8_t port, uint64_t lba, uint32_t count, const void *buf);
int ahci_find_cmd_slot(hba_port_t *port);
int ahci_identify_device(uint8_t port);
void ahci_reset_controller(void);
int ahci_reset_port(uint8_t port);
int ahci_flush_cache(uint8_t port);
void ahci_irq_handler(regs_t *regs);
void ahci_port_interrupt_handler(int port);

#endif
