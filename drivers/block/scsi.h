#ifndef SCSI_H
#define SCSI_H

#include "stdint.h"
#include "sync.h"

/* SCSI constants */
#define SCSI_MAX_DEVICES       16
#define SCSI_MAX_LUNS          8
#define SCSI_MAX_CDB_SIZE      16
#define SCSI_MAX_SENSE_SIZE    256
#define SCSI_MAX_CMD_SIZE      64
#define SCSI_TIMEOUT_MS        5000

/* SCSI peripheral device types */
#define SCSI_TYPE_DISK         0x00
#define SCSI_TYPE_TAPE         0x01
#define SCSI_TYPE_PRINTER      0x02
#define SCSI_TYPE_PROCESSOR    0x03
#define SCSI_TYPE_WORM         0x04
#define SCSI_TYPE_CDROM        0x05
#define SCSI_TYPE_SCANNER      0x06
#define SCSI_TYPE_OPTICAL      0x07
#define SCSI_TYPE_MEDIUM_CHGR  0x08
#define SCSI_TYPE_COMM         0x09
#define SCSI_TYPE_RAID         0x0C
#define SCSI_TYPE_UNKNOWN      0x1F

/* SCSI operation codes */
#define SCSI_CMD_TEST_UNIT_READY       0x00
#define SCSI_CMD_REQUEST_SENSE         0x03
#define SCSI_CMD_INQUIRY               0x12
#define SCSI_CMD_MODE_SELECT_6         0x15
#define SCSI_CMD_MODE_SENSE_6          0x1A
#define SCSI_CMD_START_STOP_UNIT       0x1B
#define SCSI_CMD_PREVENT_ALLOW_MEDIUM  0x1E
#define SCSI_CMD_READ_FORMAT_CAPACITY  0x23
#define SCSI_CMD_READ_CAPACITY_10      0x25
#define SCSI_CMD_READ_10               0x28
#define SCSI_CMD_WRITE_10              0x2A
#define SCSI_CMD_SEEK_10               0x2B
#define SCSI_CMD_WRITE_VERIFY_10       0x2E
#define SCSI_CMD_VERIFY_10             0x2F
#define SCSI_CMD_SYNCHRONIZE_CACHE_10  0x35
#define SCSI_CMD_READ_DEFECT_DATA_10   0x37
#define SCSI_CMD_WRITE_BUFFER          0x3B
#define SCSI_CMD_READ_BUFFER           0x3C
#define SCSI_CMD_READ_LONG_10          0x3E
#define SCSI_CMD_WRITE_LONG_10         0x3F
#define SCSI_CMD_MODE_SELECT_10        0x55
#define SCSI_CMD_MODE_SENSE_10         0x5A
#define SCSI_CMD_READ_16               0x88
#define SCSI_CMD_WRITE_16              0x8A
#define SCSI_CMD_WRITE_VERIFY_16       0x8E
#define SCSI_CMD_VERIFY_16             0x8F
#define SCSI_CMD_SYNCHRONIZE_CACHE_16  0x91
#define SCSI_CMD_READ_CAPACITY_16      0x9E
#define SCSI_CMD_REPORT_LUNS           0xA0

/* SCSI status codes */
#define SCSI_STATUS_GOOD            0x00
#define SCSI_STATUS_CHECK_CONDITION 0x02
#define SCSI_STATUS_CONDITION_MET   0x04
#define SCSI_STATUS_BUSY            0x08
#define SCSI_STATUS_RESERVATION     0x18
#define SCSI_STATUS_TASK_SET_FULL   0x28
#define SCSI_STATUS_ACA_ACTIVE      0x30
#define SCSI_STATUS_TASK_ABORTED    0x40

/* SCSI sense keys */
#define SENSE_KEY_NO_SENSE          0x00
#define SENSE_KEY_RECOVERED_ERROR   0x01
#define SENSE_KEY_NOT_READY         0x02
#define SENSE_KEY_MEDIUM_ERROR      0x03
#define SENSE_KEY_HARDWARE_ERROR    0x04
#define SENSE_KEY_ILLEGAL_REQUEST   0x05
#define SENSE_KEY_UNIT_ATTENTION    0x06
#define SENSE_KEY_DATA_PROTECT      0x07
#define SENSE_KEY_BLANK_CHECK       0x08
#define SENSE_KEY_ABORTED_COMMAND   0x0B
#define SENSE_KEY_MISCOMPARE        0x0E

/* SCSI additional sense codes (ASC) */
#define ASC_NO_ADDITIONAL_SENSE     0x0000
#define ASC_LUN_NOT_READY           0x0400
#define ASC_LUN_NOT_READY_FORMAT    0x0401
#define ASC_LUN_NOT_READY_REBUILD   0x0402
#define ASC_WRITE_FAULT             0x0300
#define ASC_UNRECOVERED_READ_ERROR  0x1100
#define ASC_MISCOMPARE_DURING_VERIFY 0x1D00
#define ASC_INVALID_FIELD_IN_CDB    0x2400
#define ASC_LUN_NOT_SUPPORTED       0x2500
#define ASC_INVALID_FIELD_IN_PARAM  0x2600
#define ASC_NOT_READY_TO_READY      0x2800
#define ASC_MEDIUM_NOT_PRESENT      0x3A00
#define ASC_INTERNAL_TARGET_FAILURE 0x4400

/* SCSI inquiry page codes */
#define INQUIRY_PAGE_STANDARD       0x00
#define INQUIRY_PAGE_SERIAL         0x80
#define INQUIRY_PAGE_DEVICE_ID      0x83
#define INQUIRY_PAGE_BLOCK_LIMITS   0xB0

/* SCSI CDB structures */

/* Standard 6-byte CDB */
typedef struct {
    uint8_t opcode;
    uint8_t lun_high;
    uint8_t lbn_high;
    uint8_t lbn_mid;
    uint8_t lbn_low;
    uint8_t transfer_length;
} __attribute__((packed)) scsi_cdb6_t;

/* Standard 10-byte CDB */
typedef struct {
    uint8_t  opcode;
    uint8_t  flags;
    uint32_t lba_be;
    uint8_t  group_number;
    uint16_t transfer_length_be;
    uint8_t  control;
} __attribute__((packed)) scsi_cdb10_t;

/* Standard 16-byte CDB */
typedef struct {
    uint8_t  opcode;
    uint8_t  flags;
    uint64_t lba_be;
    uint32_t transfer_length_be;
    uint8_t  group_number;
    uint8_t  control;
} __attribute__((packed)) scsi_cdb16_t;

/* Standard INQUIRY data */
typedef struct {
    uint8_t  peripheral_device_type : 5;
    uint8_t  peripheral_qualifier : 3;
    uint8_t  device_type_modifier : 7;
    uint8_t  rmb : 1;              /* Removable media */
    uint8_t  ansi_version : 3;
    uint8_t  ecma_version : 3;
    uint8_t  iso_version : 2;
    uint8_t  response_data_format : 4;
    uint8_t  hi_support : 1;
    uint8_t  naca : 1;
    uint8_t  additional_length;
    uint8_t  protect;
    uint8_t  bque : 1;
    uint8_t  enc_serv : 1;
    uint8_t  vs1 : 1;
    uint8_t  multi_port : 1;
    uint8_t  mchngr : 1;
    uint8_t  addr16 : 1;
    uint8_t  addr32 : 1;
    uint8_t  ackreqq : 1;
    uint8_t  medium_changer : 1;
    uint8_t  t10_vendor[8];
    uint8_t  product_id[16];
    uint8_t  product_rev[4];
    uint8_t  vendor_specific[20];
    uint8_t  reserved[40];
} __attribute__((packed)) scsi_inquiry_data_t;

/* READ CAPACITY (10) response */
typedef struct {
    uint32_t lba_be;
    uint32_t block_size_be;
} __attribute__((packed)) scsi_read_capacity10_t;

/* READ CAPACITY (16) response */
typedef struct {
    uint64_t lba_be;
    uint32_t block_size_be;
    uint8_t  reserved[20];
} __attribute__((packed)) scsi_read_capacity16_t;

/* SCSI sense data */
typedef struct {
    uint8_t  response_code : 7;
    uint8_t  valid : 1;
    uint8_t  obsolete;
    uint8_t  sense_key : 4;
    uint8_t  reserved : 1;
    uint8_t  ili : 1;
    uint8_t  eom : 1;
    uint8_t  filemark : 1;
    uint32_t information_be;
    uint8_t  additional_sense_length;
    uint32_t command_specific_be;
    uint8_t  additional_sense_code;
    uint8_t  additional_sense_code_qualifier;
    uint8_t  field_replaceable_unit_code;
    uint8_t  sense_key_specific[3];
    uint8_t  additional_bytes[0];
} __attribute__((packed)) scsi_sense_data_t;

/* MODE SENSE (6) parameter header */
typedef struct {
    uint8_t  mode_data_length;
    uint8_t  medium_type;
    uint8_t  device_specific;
    uint8_t  block_descriptor_length;
} __attribute__((packed)) scsi_mode_param_header6_t;

/* MODE SENSE (10) parameter header */
typedef struct {
    uint16_t mode_data_length_be;
    uint8_t  medium_type;
    uint8_t  device_specific;
    uint8_t  long_lba : 1;
    uint8_t  reserved : 7;
    uint8_t  reserved2;
    uint16_t block_descriptor_length_be;
} __attribute__((packed)) scsi_mode_param_header10_t;

/* SCSI command */
typedef struct {
    uint8_t  cdb[SCSI_MAX_CDB_SIZE];
    uint8_t  cdb_length;
    uint8_t  direction;     /* 0=host->device, 1=device->host, 2=no data */
    void    *data_buffer;
    uint32_t data_length;
    uint32_t transfer_length;
    uint8_t  status;
    uint8_t  sense_valid;
    scsi_sense_data_t sense_data;
    uint32_t timeout_ms;
} scsi_command_t;

/* SCSI logical unit */
typedef struct {
    uint8_t  lun;
    uint8_t  device_type;
    uint8_t  removable;
    uint8_t  online;
    char     vendor[9];
    char     product[17];
    char     revision[5];
    uint64_t capacity;
    uint32_t block_size;
    uint32_t max_transfer_blocks;
    uint8_t  wb_cache_enabled;
} scsi_lun_t;

/* SCSI target device */
typedef struct {
    uint8_t  target_id;
    uint8_t  max_luns;
    scsi_lun_t luns[SCSI_MAX_LUNS];
    uint8_t  lun_count;
    uint8_t  initialized;
} scsi_target_t;

/* SCSI HBA controller */
typedef struct {
    uint32_t pci_bus;
    uint32_t pci_dev;
    uint32_t pci_func;
    uint32_t io_base;
    scsi_target_t targets[SCSI_MAX_DEVICES];
    uint8_t  target_count;
    mutex_t  lock;
    uint8_t  initialized;
    /* Transport-specific operations */
    int32_t (*execute_command)(scsi_target_t *target, scsi_command_t *cmd);
    int32_t (*reset_bus)(void);
    void    *hba_private;
} scsi_hba_t;

/* SCSI API */
void scsi_init(void);
int32_t scsi_register_hba(scsi_hba_t *hba);
scsi_hba_t *scsi_get_hba(uint32_t index);
int32_t scsi_scan_bus(scsi_hba_t *hba);

/* Command execution */
int32_t scsi_execute_command(scsi_target_t *target, scsi_command_t *cmd);
int32_t scsi_test_unit_ready(scsi_target_t *target, uint8_t lun);
int32_t scsi_request_sense(scsi_target_t *target, uint8_t lun, scsi_sense_data_t *sense);

/* INQUIRY */
int32_t scsi_inquiry(scsi_target_t *target, uint8_t lun, scsi_inquiry_data_t *data);

/* READ CAPACITY */
int32_t scsi_read_capacity(scsi_target_t *target, uint8_t lun,
                            uint64_t *capacity, uint32_t *block_size);

/* READ/WRITE (10) - up to 2TB */
int32_t scsi_read_10(scsi_target_t *target, uint8_t lun,
                      uint32_t lba, uint16_t count, void *buf);
int32_t scsi_write_10(scsi_target_t *target, uint8_t lun,
                       uint32_t lba, uint16_t count, const void *buf);

/* READ/WRITE (16) - beyond 2TB */
int32_t scsi_read_16(scsi_target_t *target, uint8_t lun,
                      uint64_t lba, uint32_t count, void *buf);
int32_t scsi_write_16(scsi_target_t *target, uint8_t lun,
                       uint64_t lba, uint32_t count, const void *buf);

/* MODE SENSE/SELECT */
int32_t scsi_mode_sense(scsi_target_t *target, uint8_t lun, uint8_t page,
                         void *buf, uint32_t len);
int32_t scsi_mode_select(scsi_target_t *target, uint8_t lun, uint8_t page,
                          const void *buf, uint32_t len);

/* Synchronize cache */
int32_t scsi_sync_cache(scsi_target_t *target, uint8_t lun);

/* Start/Stop unit */
int32_t scsi_start_stop_unit(scsi_target_t *target, uint8_t lun,
                              uint8_t start, uint8_t immediate);

/* Report LUNs */
int32_t scsi_report_luns(scsi_target_t *target, uint64_t *luns, uint32_t *count);

/* CDB construction helpers */
void scsi_build_inquiry_cdb(uint8_t *cdb, uint8_t page, uint16_t length);
void scsi_build_read_capacity10_cdb(uint8_t *cdb);
void scsi_build_read_capacity16_cdb(uint8_t *cdb);
void scsi_build_read10_cdb(uint8_t *cdb, uint32_t lba, uint16_t count);
void scsi_build_write10_cdb(uint8_t *cdb, uint32_t lba, uint16_t count);
void scsi_build_read16_cdb(uint8_t *cdb, uint64_t lba, uint32_t count);
void scsi_build_write16_cdb(uint8_t *cdb, uint64_t lba, uint32_t count);
void scsi_build_mode_sense10_cdb(uint8_t *cdb, uint8_t page, uint16_t length);
void scsi_build_sync_cache10_cdb(uint8_t *cdb, uint32_t lba, uint16_t count);

/* Sense data parsing */
uint8_t scsi_sense_get_key(scsi_sense_data_t *sense);
uint16_t scsi_sense_get_asc(scsi_sense_data_t *sense);
int32_t scsi_sense_is_recoverable(scsi_sense_data_t *sense);
const char *scsi_sense_to_string(scsi_sense_data_t *sense);

/* Error recovery */
int32_t scsi_recover_device(scsi_target_t *target, uint8_t lun);

/* Device discovery */
int32_t scsi_discover(scsi_hba_t *hba);
void scsi_dump_device_info(scsi_target_t *target, uint8_t lun);

#endif