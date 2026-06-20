/* sata_advanced.h - SATA/AHCI 高级驱动 (NCQ/HOTPLUG/SMART) */
#ifndef SATA_ADVANCED_H
#define SATA_ADVANCED_H

#include "stdint.h"
#include "stddef.h"

/* AHCI 寄存器 (通用) */
#define AHCI_CAP          0x00   /* Global capabilities */
#define AHCI_GHC          0x04   /* Global host control */
#define AHCI_IS           0x08   /* Interrupt status */
#define AHCI_PI           0x0C   /* Ports implemented bitmask */
#define AHCI_VS           0x10   /* AHCI version */
#define AHCI_CCCC         0x14   /* CCC control */
#define AHCI_CCMP         0x18   /* CCC ports */
#define AHCI_EM_LOC       0x1C   /* Enclosure Mgmt location */
#define AHCI_EM_CTL       0x20   /* Enclosure Mgmt control */
#define AHCI_CAP2         0x24   /* Extended capabilities */
#define AHCI_BOHC         0x28   /* BIOS/OS handoff control */

/* GHC 寄存器位 */
#define AHCI_GHC_AE       (1 << 31)  /* AHCI Enable */
#define AHCI_GHC_IE       (1 << 1)   /* Interrupt Enable */
#define AHCI_GHC_HR       (1 << 0)   /* Host Controller Reset */

/* CAP 位域 */
#define AHCI_CAP_NCS_SHIFT 0         /* Number of Command Slots (5 bits) */
#define AHCI_CAP_NP_SHIFT  5         /* Number of Ports (5 bits) */
#define AHCI_CAP_NCQ      (1 << 31)  /* Native Command Queuing supported */
#define AHCI_CAP_SXS      (1 << 30)  /* External SATA */
#define AHCI_CAP_EMS      (1 << 27)  /* Enclosure Mgmt */
#define AHCI_CAP_SSS      (1 << 27)  /* Staggered Spin-up */
#define AHCI_CAP_SALP     (1 << 26)  /* Aggressive Link PM */
#define AHCI_CAP_SAL      (1 << 26)  /* Aggressive Link PM */
#define AHCI_CAP_SCLO     (1 << 24)  /* Command List Override */
#define AHCI_CAP_ISS_SHIFT 20        /* Interface Speed Support (4 bits) */
#define AHCI_CAP_SNZO     (1 << 19)  /* Non-zero DMA offsets */
#define AHCI_CAP_SSM      (1 << 18)  /* Mechanical Presence Switch */
#define AHCI_CAP_SXS      (1 << 17)  /* Supports External SATA */
#define AHCI_CAP_FBSS     (1 << 16)  /* FIS-based Switching */
#define AHCI_CAP_PMD      (1 << 15)  /* PIO Multiple DRQ */
#define AHCI_CAP_SSC      (1 << 14)  /* Slumber State Capable */
#define AHCI_CAP_PSC      (1 << 13)  /* Partial State Capable */
#define AHCI_CAP_NCS_MASK (0x1F << 0) /* NCS mask */
#define AHCI_CAP_NP_MASK  (0x1F << 5) /* NP mask */

/* 端口寄存器偏移 (每个端口 0x80 字节) */
#define AHCI_PxCLB        0x00   /* Command List Base Address */
#define AHCI_PxFB         0x08   /* FIS Base Address */
#define AHCI_PxIS         0x10   /* Interrupt Status */
#define AHCI_PxIE         0x14   /* Interrupt Enable */
#define AHCI_PxCMD        0x18   /* Command and Status */
#define AHCI_PxTFD        0x20   /* Task File Data */
#define AHCI_PxSIG        0x24   /* Signature */
#define AhCI_PxSSTS       0x28   /* SATA Status (SStatus) */
#define AhCI_PxSCTL       0x2C   /* SATA Control (SControl) */
#define AhCI_PxSERR       0x30   /* SATA Error (SError) */
#define AhCI_PxSACT       0x34   /* SActive (NCQ) */
#define AHCI_PxCI         0x38   /* Command Issue */
#define AHCI_PxSNTF       0x38   /* SNotification */
#define AHCI_PxFBS        0x40   /* FIS-based Switching */
#define AHCI_PxDEVSLP     0x44   /* Device Sleep */
#define AHCI_PxVS         0x70   /* Vendor Specific */

/* PxCMD 位 */
#define AHCI_PxCMD_ST     (1 << 0)   /* Start */
#define AHCI_PxCMD_SUD    (1 << 1)   /* Spin-Up Device */
#define AHCI_PxCMD_POD    (1 << 2)   /* Power On Device */
#define AHCI_PxCMD_FRE    (1 << 4)   /* FIS Receive Enable */
#define AHCI_PxCMD_COLD   (1 << 15)  /* Cold Presence Detection */
#define AHCI_PxCMP        (1 << 19)  /* Command List Running */
#define AHCI_PxCMD_FR     (1 << 14)  /* FIS Running */
#define AHCI_PxCMD_CR     (1 << 15)  /* Command List Running */
#define AHCI_PxCMD_ICC_SHIFT 28      /* Interface Communication Control */

/* SATA 传输速率 */
#define SATA_GEN1         1   /* 1.5 Gbps */
#define SATA_GEN2         2   /* 3 Gbps */
#define SATA_GEN3         3   /* 6 Gbps */

/* ATA 命令 */
#define ATA_CMD_READ_DMA_EXT    0x25
#define ATA_CMD_WRITE_DMA_EXT   0x35
#define ATA_CMD_READ_FDMA_EXT  0x60
#define ATA_CMD_WRITE_FDMA_EXT 0x61
#define ATA_CMD_READ_NCQ       0x60
#define ATA_CMD_WRITE_NCQ      0x61
#define ATA_CMD_IDENTIFY       0xEC
#define ATA_CMD_READ_LOG       0x30
#define ATA_CMD_SMART          0xB0
#define ATA_CMD_FLUSH_CACHE    0xE7
#define ATA_CMD_FLUSH_CACHE_EXT 0xEA
#define ATA_CMD_SET_FEATURES   0xEF

/* SMART 子命令 */
#define ATA_SMART_READ_DATA    0xD0
#define ATA_SMART_READ_THRESHOLDS 0xD1
#define ATA_SMART_ENABLE       0xD8
#define ATA_SMART_DISABLE      0xD9
#define ATA_SMART_STATUS       0xDA
#define ATA_SMART_AUTO_OFFLINE 0xDB

/* ATA 标识数据结构 */
typedef struct ata_identify {
    uint16_t general_conf;        /* 0x00 General configuration */
    uint16_t obsolete_1;          /* 0x02 Obsolete */
    uint16_t specific_config;     /* 0x04 Specific configuration */
    uint16_t obsolete_2[2];       /* 0x06-0x09 */
    uint16_t serial_no[10];       /* 0x0A-0x19 Serial number (ASCII) */
    uint16_t obsolete_3[2];       /* 0x1A-0x1D */
    uint16_t firmware_rev[4];     /* 0x1E-0x21 Firmware revision */
    uint16_t model_no[20];        /* 0x22-0x35 Model number (ASCII) */
    uint16_t max_sectors_per_irq; /* 0x36 */
    uint16_t reserved1;           /* 0x38 */
    uint16_t capabilities[2];     /* 0x39-0x3A */
    uint16_t obsolete_4;          /* 0x3B */
    uint16_t pio_timing;          /* 0x3C */
    uint16_t dma_timing;          /* 0x3D */
    uint16_t field_validity;      /* 0xE Field validity */
    uint16_t cur_cylinders;       /* 0x3F Current cylinders */
    uint16_t cur_heads;           /* 0x41 Current heads */
    uint16_t cur_sectors_per_track;/* 0x42 Current sectors/track */
    uint16_t capacity_sectors[2]; /* 0x43-0x44 Capacity in sectors */
    uint16_t obsolete_5[2];       /* 0x45-0x48 */
    uint32_t dword_io;            /* 0x49-0x4A Multiword DMA */
    uint16_t pio_modes;           /* 0x4B PIO modes */
    uint16_t dma_modes;           /* 0x4C Multiword DMA modes */
    uint16_t eide_pio_modes;     /* 0x4D EIDE PIO modes */
    uint16_t min_dma_cycle;      /* 0x4E Min multiword DMA cycle */
    uint16_t rec_dma_cycle;      /* 0x0F Recommended cycle */
    uint16_t min_pio_cycle;      /* 0x50 Min PIO cycle without flow */
    uint16_t min_pio_cycle_flow; /* 0x51 Min PIO with IORDY flow */
    uint16_t reserved2[7];       /* 0x52-0x58 */
    uint16_t features_support;   /* 0x59 Features/command set support */
    uint16_t features_enable;    /* 0x5A Features/command enable */
    uint16_t features_default;   /* 0x5B Default features */
    uint16_t dma_time;           /* 0x5C DMA time */
    uint16_t max_dmaword;        /* 0x5D Max DMAword */
    uint64_t lba48_sectors;      /* 0x60-0x63 LBA48 user addressable sectors */
    uint16_t reserved3[80];      /* 0x64-0x103 */
    uint16_t smart_commands;     /* 0x104 SMART commands */
    uint16_t security_status;    /* 0x106 Security status */
    uint16_t reserved4[48];      /* 0x108-0x137 */
    uint16_t rem_media_status;   /* 0x138 Removable media status notification */
    uint16_t security_status2;   /* 0x139 Security status enhanced */
    uint16_t vendor_specific[31];/* 0x13A-0x158 */
    uint16_t cfa_power;          /* 0x159 CFA power mode */
    uint16_t stream_mgmt;        /* 0x15A Stream management */
    uint16_t uuid[4];            /* 0x15B-0x158 Device UUID */
    uint16_t world_wide_name[4]; /* 0x15F-0x162 World wide name */
    uint16_t reserved5[96];      /* 0x163-0x1FE */
    uint16_t integrity_word;     /* 0x1FF Integrity word */
} ata_identify_t;

/* SMART 属性 */
typedef struct smart_attribute {
    uint8_t  id;                 /* Attribute ID */
    uint16_t flags;              /* Flags (threshold/updated/etc) */
    uint8_t  current_value;      /* Normalized current value */
    uint8_t  worst_value;        /* Worst ever normalized value */
    uint32_t raw_value;          /* Raw value (vendor specific encoding) */
    uint8_t  threshold;          /* Failure threshold */
} smart_attribute_t;

/* SMART 数据 */
typedef struct smart_data {
    uint16_t version;                    /* Structure version */
    smart_attribute_t attributes[30];   /* Up to 30 attributes */
    uint8_t  offline_status;             /* Offline data collection status */
    uint8_t  self_test_status;           /* Self-test execution status */
    uint16_t total_time;                 /* Total offline data collection time */
} smart_data_t;

/* NCQ 命令标签 */
typedef struct ncq_tag {
    uint8_t  tag;                /* NCQ tag (0-31) */
    uint8_t  active;             /* Is this tag in flight? */
    uint32_t lba;                /* Starting LBA */
    uint16_t sector_count;       /* Sector count */
    uint8_t  direction;          /* 0=read, 1=write */
    void    *buffer;             /* Data buffer */
    uint32_t prdtl;              /* Physical Region Descriptor Table Length */
} ncq_tag_t;

/* AHCI 端口上下文 */
typedef struct ahci_port {
    uint8_t  port_num;           /* Port number (0-based) */
    volatile void *base;         /* Port register base */
    volatile void *cmd_list;     /* Command list (DMA) */
    volatile void *fis_base;     /* Received FIS area (DMA) */
    uint32_t cmd_slot_count;     /* Number of command slots */

    /* 设备信息 */
    int      present;            /* Device present */
    int      active;             /* Port started */
    uint8_t  link_speed;         /* Current link speed (GEN1/GEN2/GEN3) */
    uint32_t sstatus;            /* SStatus register */
    uint32_t scontrol;           /* SControl register */

    /* ATA Identify data */
    ata_identify_t identify;     /* IDENTIFY DEVICE data */
    char model[41];              /* Model number (null-terminated ASCII) */
    char serial[21];             /* Serial number */
    char firmware[9];            /* Firmware revision */
    uint64_t max_lba;            /* Maximum LBA addressable */
    uint64_t max_lba48;          /* LBA48 maximum */
    uint32_t sector_size;        /* Logical sector size (usually 512) */

    /* NCQ */
    int      ncq_enabled;        /* NCQ enabled */
    uint8_t  queue_depth;        /* Queue depth (up to 32) */
    ncq_tag_t ncq_tags[32];      /* NCQ tags */
    uint32_t sactive;            /* SActive register shadow */

    /* SMART */
    int      smart_supported;    /* SMART supported */
    int      smart_enabled;      /* SMART enabled */

    /* 热插拔 */
    int      hotplug_capable;    /* Hot-plug capable */
    int      cold_presence;      /* Cold presence detection */
    void (*on_hotplug_insert)(struct ahci_port *port);  /* Callback */
    void (*on_hotplug_remove)(struct ahci_port *port);  /* Callback */

    /* 统计 */
    uint64_t read_ops;
    uint64_t write_ops;
    uint64_t bytes_read;
    uint64_t bytes_written;
    uint32_t errors;
} ahci_port_t;

/* AHCI 主控制器 */
typedef struct ahci_controller {
    volatile uint32_t *mmio_base;    /* MMIO register base */
    uint32_t         version;         /* AHCI version (BCD) */
    uint32_t         cap;             /* CAP register */
    uint32_t         cap2;            /* CAP2 register */
    uint32_t         ghc;             /* GHC register */
    uint32_t         ports_implemented;/* PI bitmask */
    uint32_t         nports;          /* Total number of ports */
    uint32_t         ncs;             /* Number of command slots per port */
    uint32_t         has_ncq;         /* NCQ supported globally */
    ahci_port_t      ports[32];       /* Port array (max 32) */
} ahci_controller_t;

/* ===== 公共API ===== */

/* 初始化AHCI控制器 */
int sata_init(volatile uint32_t *mmio_base);

/* 扫描所有端口上的设备 */
int sata_scan_ports(ahci_controller_t *ctrl);

/* 读取扇区 (LBA48) */
int sata_read_sectors(ahci_port_t *port, uint64_t lba, uint16_t count, void *buffer);

/* 写入扇区 (LBA48) */
int sata_write_sectors(ahci_port_t *port, uint64_t lba, uint16_t count, const void *buffer);

/* NCQ 读取 (异步) */
int sata_ncq_read(ahci_port_t *port, uint64_t lba, uint16_t count, void *buffer, uint8_t tag);

/* NCQ 写入 (异步) */
int sata_ncq_write(ahci_port_t *port, uint64_t lba, uint16_t count, const void *buffer, uint8_t tag);

/* 等待NCQ完成 */
int sata_ncq_wait(ahci_port_t *port, uint8_t tag, uint32_t timeout_ms);

/* ATA IDENTIFY DEVICE */
int sata_identify(ahci_port_t *port);

/* SMART 操作 */
int sata_smart_enable(ahci_port_t *port);
int sata_smart_read_data(ahci_port_t *port, smart_data_t *out);
int sata_smart_read_thresholds(ahci_port_t *port, smart_attribute_t attrs[30]);
int sata_smart_self_test_short(ahci_port_t *port);
int sata_smart_self_test_long(ahci_port_t *port);

/* FLUSH CACHE */
int sata_flush(ahci_port_t *port);

/* 热插拔检测 */
int sata_check_hotplug(ahci_controller_t *ctrl);
void sata_set_hotplug_callback(ahci_port_t *port, void (*insert_cb)(ahci_port_t*), void (*remove_cb)(ahci_port_t*));

/* 链路电源管理 */
int sata_set_link_pm(ahci_port_t *port, uint8_t level);  /* 0=none, 1=partial, 2=slumber */
int sata_set_dev_sleep(ahci_port_t *port, int enable);

/* 获取端口信息 */
void sata_print_port_info(ahci_port_t *port);

/* 获取控制器统计 */
typedef struct {
    uint32_t total_ports;
    uint32_t active_ports;
    uint64_t total_reads;
    uint64_t total_writes;
    uint64_t total_bytes_read;
    uint64_t total_bytes_written;
    uint32_t ncq_ops;
    uint32_t errors;
} sata_stats_t;

void sata_get_stats(ahci_controller_t *ctrl, sata_stats_t *out);

#endif /* SATA_ADVANCED_H */
