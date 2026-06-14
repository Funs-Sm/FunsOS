#include "scsi.h"
#include "kheap.h"
#include "string.h"
#include "stdio.h"

static scsi_hba_t *hba_list[4];
static uint32_t hba_count = 0;

/* Helper: convert 16-bit big-endian to host */
static inline uint16_t be16_to_host(uint16_t v) {
    return ((v >> 8) & 0xFF) | ((v & 0xFF) << 8);
}

/* Helper: convert 32-bit big-endian to host */
static inline uint32_t be32_to_host(uint32_t v) {
    return ((v >> 24) & 0xFF) |
           ((v >> 8) & 0xFF00) |
           ((v & 0xFF00) << 8) |
           ((v & 0xFF) << 24);
}

/* Helper: convert 64-bit big-endian to host */
static inline uint64_t be64_to_host(uint64_t v) {
    return ((uint64_t)be32_to_host((uint32_t)(v >> 32))) |
           (((uint64_t)be32_to_host((uint32_t)(v & 0xFFFFFFFF))) << 32);
}

/* Helper: convert host to 16-bit big-endian */
static inline uint16_t host_to_be16(uint16_t v) {
    return be16_to_host(v);
}

/* Helper: convert host to 32-bit big-endian */
static inline uint32_t host_to_be32(uint32_t v) {
    return be32_to_host(v);
}

void scsi_init(void) {
    hba_count = 0;
    for (int i = 0; i < 4; i++) {
        hba_list[i] = 0;
    }
}

int32_t scsi_register_hba(scsi_hba_t *hba) {
    if (!hba || hba_count >= 4) return -1;
    hba_list[hba_count] = hba;
    mutex_init(&hba->lock);
    hba_count++;
    return 0;
}

scsi_hba_t *scsi_get_hba(uint32_t index) {
    if (index >= hba_count) return 0;
    return hba_list[index];
}

/* CDB construction helpers */

void scsi_build_inquiry_cdb(uint8_t *cdb, uint8_t page, uint16_t length) {
    memset(cdb, 0, SCSI_MAX_CDB_SIZE);
    cdb[0] = SCSI_CMD_INQUIRY;
    if (page != 0) {
        cdb[1] = 0x01;  /* EVPD = 1 */
        cdb[2] = page;
    }
    cdb[3] = (uint8_t)(length >> 8);
    cdb[4] = (uint8_t)(length & 0xFF);
}

void scsi_build_read_capacity10_cdb(uint8_t *cdb) {
    memset(cdb, 0, SCSI_MAX_CDB_SIZE);
    cdb[0] = SCSI_CMD_READ_CAPACITY_10;
}

void scsi_build_read_capacity16_cdb(uint8_t *cdb) {
    memset(cdb, 0, SCSI_MAX_CDB_SIZE);
    cdb[0] = SCSI_CMD_READ_CAPACITY_16;
    cdb[13] = 32;  /* Allocation length */
}

void scsi_build_read10_cdb(uint8_t *cdb, uint32_t lba, uint16_t count) {
    memset(cdb, 0, SCSI_MAX_CDB_SIZE);
    cdb[0] = SCSI_CMD_READ_10;
    cdb[2] = (uint8_t)(lba >> 24);
    cdb[3] = (uint8_t)(lba >> 16);
    cdb[4] = (uint8_t)(lba >> 8);
    cdb[5] = (uint8_t)(lba & 0xFF);
    cdb[7] = (uint8_t)(count >> 8);
    cdb[8] = (uint8_t)(count & 0xFF);
}

void scsi_build_write10_cdb(uint8_t *cdb, uint32_t lba, uint16_t count) {
    memset(cdb, 0, SCSI_MAX_CDB_SIZE);
    cdb[0] = SCSI_CMD_WRITE_10;
    cdb[2] = (uint8_t)(lba >> 24);
    cdb[3] = (uint8_t)(lba >> 16);
    cdb[4] = (uint8_t)(lba >> 8);
    cdb[5] = (uint8_t)(lba & 0xFF);
    cdb[7] = (uint8_t)(count >> 8);
    cdb[8] = (uint8_t)(count & 0xFF);
}

void scsi_build_read16_cdb(uint8_t *cdb, uint64_t lba, uint32_t count) {
    memset(cdb, 0, SCSI_MAX_CDB_SIZE);
    cdb[0] = SCSI_CMD_READ_16;
    cdb[2] = (uint8_t)(lba >> 56);
    cdb[3] = (uint8_t)(lba >> 48);
    cdb[4] = (uint8_t)(lba >> 40);
    cdb[5] = (uint8_t)(lba >> 32);
    cdb[6] = (uint8_t)(lba >> 24);
    cdb[7] = (uint8_t)(lba >> 16);
    cdb[8] = (uint8_t)(lba >> 8);
    cdb[9] = (uint8_t)(lba & 0xFF);
    cdb[10] = (uint8_t)(count >> 24);
    cdb[11] = (uint8_t)(count >> 16);
    cdb[12] = (uint8_t)(count >> 8);
    cdb[13] = (uint8_t)(count & 0xFF);
}

void scsi_build_write16_cdb(uint8_t *cdb, uint64_t lba, uint32_t count) {
    memset(cdb, 0, SCSI_MAX_CDB_SIZE);
    cdb[0] = SCSI_CMD_WRITE_16;
    cdb[2] = (uint8_t)(lba >> 56);
    cdb[3] = (uint8_t)(lba >> 48);
    cdb[4] = (uint8_t)(lba >> 40);
    cdb[5] = (uint8_t)(lba >> 32);
    cdb[6] = (uint8_t)(lba >> 24);
    cdb[7] = (uint8_t)(lba >> 16);
    cdb[8] = (uint8_t)(lba >> 8);
    cdb[9] = (uint8_t)(lba & 0xFF);
    cdb[10] = (uint8_t)(count >> 24);
    cdb[11] = (uint8_t)(count >> 16);
    cdb[12] = (uint8_t)(count >> 8);
    cdb[13] = (uint8_t)(count & 0xFF);
}

void scsi_build_mode_sense10_cdb(uint8_t *cdb, uint8_t page, uint16_t length) {
    memset(cdb, 0, SCSI_MAX_CDB_SIZE);
    cdb[0] = SCSI_CMD_MODE_SENSE_10;
    cdb[2] = page & 0x3F;
    cdb[7] = (uint8_t)(length >> 8);
    cdb[8] = (uint8_t)(length & 0xFF);
}

void scsi_build_sync_cache10_cdb(uint8_t *cdb, uint32_t lba, uint16_t count) {
    memset(cdb, 0, SCSI_MAX_CDB_SIZE);
    cdb[0] = SCSI_CMD_SYNCHRONIZE_CACHE_10;
    cdb[2] = (uint8_t)(lba >> 24);
    cdb[3] = (uint8_t)(lba >> 16);
    cdb[4] = (uint8_t)(lba >> 8);
    cdb[5] = (uint8_t)(lba & 0xFF);
    cdb[7] = (uint8_t)(count >> 8);
    cdb[8] = (uint8_t)(count & 0xFF);
}

/* Command execution */

int32_t scsi_execute_command(scsi_target_t *target, scsi_command_t *cmd) {
    if (!target || !cmd) return -1;

    /* Find the HBA that owns this target */
    scsi_hba_t *hba = 0;
    for (uint32_t i = 0; i < hba_count; i++) {
        for (uint8_t j = 0; j < hba_list[i]->target_count; j++) {
            if (&hba_list[i]->targets[j] == target) {
                hba = hba_list[i];
                break;
            }
        }
        if (hba) break;
    }

    if (!hba) return -1;

    mutex_lock(&hba->lock);

    cmd->status = SCSI_STATUS_GOOD;
    cmd->sense_valid = 0;

    int32_t result;
    if (hba->execute_command) {
        result = hba->execute_command(target, cmd);
    } else {
        result = -1;
    }

    mutex_unlock(&hba->lock);
    return result;
}

int32_t scsi_test_unit_ready(scsi_target_t *target, uint8_t lun) {
    scsi_command_t cmd;
    memset(&cmd, 0, sizeof(scsi_command_t));
    cmd.cdb[0] = SCSI_CMD_TEST_UNIT_READY;
    cmd.cdb_length = 6;
    cmd.direction = 2;
    cmd.timeout_ms = SCSI_TIMEOUT_MS;

    return scsi_execute_command(target, &cmd);
}

int32_t scsi_request_sense(scsi_target_t *target, uint8_t lun, scsi_sense_data_t *sense) {
    if (!target || !sense) return -1;

    scsi_command_t cmd;
    memset(&cmd, 0, sizeof(scsi_command_t));
    cmd.cdb[0] = SCSI_CMD_REQUEST_SENSE;
    cmd.cdb[4] = SCSI_MAX_SENSE_SIZE;
    cmd.cdb_length = 6;
    cmd.direction = 1;
    cmd.data_buffer = sense;
    cmd.data_length = SCSI_MAX_SENSE_SIZE;
    cmd.timeout_ms = SCSI_TIMEOUT_MS;

    int32_t ret = scsi_execute_command(target, &cmd);
    if (ret < 0) return ret;

    return cmd.sense_valid ? 0 : -1;
}

int32_t scsi_inquiry(scsi_target_t *target, uint8_t lun, scsi_inquiry_data_t *data) {
    if (!target || !data) return -1;

    scsi_command_t cmd;
    memset(&cmd, 0, sizeof(scsi_command_t));
    scsi_build_inquiry_cdb(cmd.cdb, INQUIRY_PAGE_STANDARD, sizeof(scsi_inquiry_data_t));
    cmd.cdb_length = 6;
    cmd.direction = 1;
    cmd.data_buffer = data;
    cmd.data_length = sizeof(scsi_inquiry_data_t);
    cmd.timeout_ms = SCSI_TIMEOUT_MS;

    return scsi_execute_command(target, &cmd);
}

int32_t scsi_read_capacity(scsi_target_t *target, uint8_t lun,
                            uint64_t *capacity, uint32_t *block_size) {
    if (!target) return -1;

    /* Try READ CAPACITY (10) first */
    scsi_command_t cmd;
    memset(&cmd, 0, sizeof(scsi_command_t));
    scsi_build_read_capacity10_cdb(cmd.cdb);
    cmd.cdb_length = 10;
    cmd.direction = 1;

    scsi_read_capacity10_t cap10;
    cmd.data_buffer = &cap10;
    cmd.data_length = sizeof(cap10);
    cmd.timeout_ms = SCSI_TIMEOUT_MS;

    int32_t ret = scsi_execute_command(target, &cmd);
    if (ret != 0) return ret;

    if (capacity) {
        *capacity = (uint64_t)be32_to_host(cap10.lba_be) + 1;
    }
    if (block_size) {
        *block_size = be32_to_host(cap10.block_size_be);
    }

    /* If capacity is at max (0xFFFFFFFF), use READ CAPACITY (16) */
    if (cap10.lba_be == 0xFFFFFFFF) {
        memset(&cmd, 0, sizeof(scsi_command_t));
        scsi_build_read_capacity16_cdb(cmd.cdb);
        cmd.cdb_length = 16;
        cmd.direction = 1;

        scsi_read_capacity16_t cap16;
        cmd.data_buffer = &cap16;
        cmd.data_length = sizeof(cap16);
        cmd.timeout_ms = SCSI_TIMEOUT_MS;

        ret = scsi_execute_command(target, &cmd);
        if (ret == 0 && capacity) {
            *capacity = be64_to_host(cap16.lba_be) + 1;
        }
    }

    return 0;
}

int32_t scsi_read_10(scsi_target_t *target, uint8_t lun,
                      uint32_t lba, uint16_t count, void *buf) {
    if (!target || !buf || count == 0) return -1;

    scsi_command_t cmd;
    memset(&cmd, 0, sizeof(scsi_command_t));
    scsi_build_read10_cdb(cmd.cdb, lba, count);
    cmd.cdb_length = 10;
    cmd.direction = 1;
    cmd.data_buffer = buf;
    cmd.data_length = (uint32_t)count * 512;
    cmd.timeout_ms = SCSI_TIMEOUT_MS;

    return scsi_execute_command(target, &cmd);
}

int32_t scsi_write_10(scsi_target_t *target, uint8_t lun,
                       uint32_t lba, uint16_t count, const void *buf) {
    if (!target || !buf || count == 0) return -1;

    scsi_command_t cmd;
    memset(&cmd, 0, sizeof(scsi_command_t));
    scsi_build_write10_cdb(cmd.cdb, lba, count);
    cmd.cdb_length = 10;
    cmd.direction = 0;
    cmd.data_buffer = (void *)buf;
    cmd.data_length = (uint32_t)count * 512;
    cmd.timeout_ms = SCSI_TIMEOUT_MS;

    return scsi_execute_command(target, &cmd);
}

int32_t scsi_read_16(scsi_target_t *target, uint8_t lun,
                      uint64_t lba, uint32_t count, void *buf) {
    if (!target || !buf || count == 0) return -1;

    scsi_command_t cmd;
    memset(&cmd, 0, sizeof(scsi_command_t));
    scsi_build_read16_cdb(cmd.cdb, lba, count);
    cmd.cdb_length = 16;
    cmd.direction = 1;
    cmd.data_buffer = buf;
    cmd.data_length = count * 512;
    cmd.timeout_ms = SCSI_TIMEOUT_MS;

    return scsi_execute_command(target, &cmd);
}

int32_t scsi_write_16(scsi_target_t *target, uint8_t lun,
                       uint64_t lba, uint32_t count, const void *buf) {
    if (!target || !buf || count == 0) return -1;

    scsi_command_t cmd;
    memset(&cmd, 0, sizeof(scsi_command_t));
    scsi_build_write16_cdb(cmd.cdb, lba, count);
    cmd.cdb_length = 16;
    cmd.direction = 0;
    cmd.data_buffer = (void *)buf;
    cmd.data_length = count * 512;
    cmd.timeout_ms = SCSI_TIMEOUT_MS;

    return scsi_execute_command(target, &cmd);
}

int32_t scsi_mode_sense(scsi_target_t *target, uint8_t lun, uint8_t page,
                         void *buf, uint32_t len) {
    if (!target || !buf) return -1;

    scsi_command_t cmd;
    memset(&cmd, 0, sizeof(scsi_command_t));
    scsi_build_mode_sense10_cdb(cmd.cdb, page, (uint16_t)len);
    cmd.cdb_length = 10;
    cmd.direction = 1;
    cmd.data_buffer = buf;
    cmd.data_length = len;
    cmd.timeout_ms = SCSI_TIMEOUT_MS;

    return scsi_execute_command(target, &cmd);
}

int32_t scsi_mode_select(scsi_target_t *target, uint8_t lun, uint8_t page,
                          const void *buf, uint32_t len) {
    if (!target || !buf) return -1;

    uint8_t cdb[SCSI_MAX_CDB_SIZE];
    memset(cdb, 0, SCSI_MAX_CDB_SIZE);
    cdb[0] = SCSI_CMD_MODE_SELECT_10;
    cdb[1] = 0x10;  /* PF = 1 */
    cdb[7] = (uint8_t)(len >> 8);
    cdb[8] = (uint8_t)(len & 0xFF);

    scsi_command_t cmd;
    memset(&cmd, 0, sizeof(scsi_command_t));
    memcpy(cmd.cdb, cdb, SCSI_MAX_CDB_SIZE);
    cmd.cdb_length = 10;
    cmd.direction = 0;
    cmd.data_buffer = (void *)buf;
    cmd.data_length = len;
    cmd.timeout_ms = SCSI_TIMEOUT_MS;

    return scsi_execute_command(target, &cmd);
}

int32_t scsi_sync_cache(scsi_target_t *target, uint8_t lun) {
    if (!target) return -1;

    scsi_command_t cmd;
    memset(&cmd, 0, sizeof(scsi_command_t));
    scsi_build_sync_cache10_cdb(cmd.cdb, 0, 0);
    cmd.cdb_length = 10;
    cmd.direction = 2;
    cmd.timeout_ms = SCSI_TIMEOUT_MS;

    return scsi_execute_command(target, &cmd);
}

int32_t scsi_start_stop_unit(scsi_target_t *target, uint8_t lun,
                              uint8_t start, uint8_t immediate) {
    if (!target) return -1;

    scsi_command_t cmd;
    memset(&cmd, 0, sizeof(scsi_command_t));
    cmd.cdb[0] = SCSI_CMD_START_STOP_UNIT;
    cmd.cdb[4] = ((immediate ? 1 : 0) << 0) | ((start ? 1 : 0) << 0);
    cmd.cdb_length = 6;
    cmd.direction = 2;
    cmd.timeout_ms = SCSI_TIMEOUT_MS;

    return scsi_execute_command(target, &cmd);
}

int32_t scsi_report_luns(scsi_target_t *target, uint64_t *luns, uint32_t *count) {
    if (!target || !luns || !count) return -1;

    uint8_t buf[256];
    memset(buf, 0, sizeof(buf));

    uint8_t cdb[SCSI_MAX_CDB_SIZE];
    memset(cdb, 0, SCSI_MAX_CDB_SIZE);
    cdb[0] = SCSI_CMD_REPORT_LUNS;
    cdb[1] = 0x00;  /* Select report = 0 */
    cdb[6] = 0x01;  /* Allocation length MSB */
    cdb[9] = 0x00;  /* Allocation length LSB = 256 */

    scsi_command_t cmd;
    memset(&cmd, 0, sizeof(scsi_command_t));
    memcpy(cmd.cdb, cdb, SCSI_MAX_CDB_SIZE);
    cmd.cdb_length = 12;
    cmd.direction = 1;
    cmd.data_buffer = buf;
    cmd.data_length = sizeof(buf);
    cmd.timeout_ms = SCSI_TIMEOUT_MS;

    int32_t ret = scsi_execute_command(target, &cmd);
    if (ret != 0) {
        *count = 1;
        luns[0] = 0;
        return 0;
    }

    uint32_t list_length = be32_to_host(*(uint32_t *)buf);
    uint32_t num_luns = list_length / 8;
    if (num_luns > SCSI_MAX_LUNS) num_luns = SCSI_MAX_LUNS;

    for (uint32_t i = 0; i < num_luns; i++) {
        uint64_t *lun_ptr = (uint64_t *)(buf + 8 + i * 8);
        luns[i] = be64_to_host(*lun_ptr);
    }

    *count = num_luns;
    return 0;
}

/* Sense data parsing */

uint8_t scsi_sense_get_key(scsi_sense_data_t *sense) {
    if (!sense) return SENSE_KEY_NO_SENSE;
    return sense->sense_key & 0x0F;
}

uint16_t scsi_sense_get_asc(scsi_sense_data_t *sense) {
    if (!sense) return 0;
    return ((uint16_t)sense->additional_sense_code << 8) |
           sense->additional_sense_code_qualifier;
}

int32_t scsi_sense_is_recoverable(scsi_sense_data_t *sense) {
    if (!sense) return 0;
    return (sense->sense_key == SENSE_KEY_RECOVERED_ERROR) ? 1 : 0;
}

const char *scsi_sense_to_string(scsi_sense_data_t *sense) {
    if (!sense) return "No sense data";

    switch (sense->sense_key & 0x0F) {
        case SENSE_KEY_NO_SENSE:           return "No error";
        case SENSE_KEY_RECOVERED_ERROR:    return "Recovered error";
        case SENSE_KEY_NOT_READY:          return "Not ready";
        case SENSE_KEY_MEDIUM_ERROR:       return "Medium error";
        case SENSE_KEY_HARDWARE_ERROR:     return "Hardware error";
        case SENSE_KEY_ILLEGAL_REQUEST:    return "Illegal request";
        case SENSE_KEY_UNIT_ATTENTION:     return "Unit attention";
        case SENSE_KEY_DATA_PROTECT:       return "Data protect";
        case SENSE_KEY_BLANK_CHECK:        return "Blank check";
        case SENSE_KEY_ABORTED_COMMAND:    return "Aborted command";
        case SENSE_KEY_MISCOMPARE:         return "Miscompare";
        default:                           return "Unknown sense";
    }
}

/* Error recovery */

int32_t scsi_recover_device(scsi_target_t *target, uint8_t lun) {
    if (!target) return -1;

    /* Try getting sense data */
    scsi_sense_data_t sense;
    if (scsi_request_sense(target, lun, &sense) == 0) {
        /* Analyze sense data */
        uint8_t key = scsi_sense_get_key(&sense);

        if (key == SENSE_KEY_UNIT_ATTENTION) {
            /* Retry the command */
            return 1;
        }
        if (key == SENSE_KEY_NOT_READY) {
            /* Try start unit */
            scsi_start_stop_unit(target, lun, 1, 0);
            return 1;
        }
    }

    /* Try test unit ready */
    if (scsi_test_unit_ready(target, lun) == 0) {
        return 0;
    }

    return -1;
}

/* Device discovery */

static void parse_inquiry(scsi_inquiry_data_t *inq, scsi_lun_t *lun) {
    lun->device_type = inq->peripheral_device_type & 0x1F;
    lun->removable = inq->rmb;

    uint32_t i;
    for (i = 0; i < 8 && inq->t10_vendor[i] != ' ' && inq->t10_vendor[i]; i++) {
        lun->vendor[i] = inq->t10_vendor[i];
    }
    lun->vendor[i] = '\0';

    for (i = 0; i < 16 && inq->product_id[i] != ' ' && inq->product_id[i]; i++) {
        lun->product[i] = inq->product_id[i];
    }
    lun->product[i] = '\0';

    for (i = 0; i < 4 && inq->product_rev[i] != ' ' && inq->product_rev[i]; i++) {
        lun->revision[i] = inq->product_rev[i];
    }
    lun->revision[i] = '\0';
}

int32_t scsi_discover(scsi_hba_t *hba) {
    if (!hba) return -1;

    for (uint8_t tid = 0; tid < SCSI_MAX_DEVICES; tid++) {
        scsi_target_t *target = &hba->targets[tid];
        if (target->initialized) continue;

        /* Initialize target */
        memset(target, 0, sizeof(scsi_target_t));
        target->target_id = tid;

        /* Report LUNs */
        uint64_t luns[SCSI_MAX_LUNS];
        uint32_t lun_count;
        if (scsi_report_luns(target, luns, &lun_count) != 0) {
            /* If REPORT LUNS fails, try LUN 0 */
            luns[0] = 0;
            lun_count = 1;
        }

        if (lun_count > SCSI_MAX_LUNS) lun_count = SCSI_MAX_LUNS;

        target->max_luns = SCSI_MAX_LUNS;
        uint8_t found_luns = 0;

        for (uint32_t l = 0; l < lun_count; l++) {
            uint8_t lun = (uint8_t)(luns[l] & 0xFF);

            /* Send TEST UNIT READY */
            if (scsi_test_unit_ready(target, lun) != 0) {
                continue;
            }

            /* Send INQUIRY */
            scsi_inquiry_data_t inq;
            memset(&inq, 0, sizeof(inq));
            if (scsi_inquiry(target, lun, &inq) != 0) {
                continue;
            }

            scsi_lun_t *slun = &target->luns[target->lun_count];
            slun->lun = lun;
            parse_inquiry(&inq, slun);

            /* Only probe block devices */
            if (slun->device_type == SCSI_TYPE_DISK ||
                slun->device_type == SCSI_TYPE_CDROM ||
                slun->device_type == SCSI_TYPE_OPTICAL) {

                uint64_t capacity;
                uint32_t block_size;
                if (scsi_read_capacity(target, lun, &capacity, &block_size) == 0) {
                    slun->capacity = capacity;
                    slun->block_size = block_size;
                    slun->online = 1;
                }
            }

            target->lun_count++;
            found_luns++;
        }

        if (found_luns > 0) {
            target->initialized = 1;
            hba->target_count = tid + 1;
        }
    }

    return 0;
}

void scsi_dump_device_info(scsi_target_t *target, uint8_t lun) {
    if (!target || lun >= target->lun_count) return;

    scsi_lun_t *slun = &target->luns[lun];
    printf("  LUN %d: %s %s %s\n", slun->lun, slun->vendor, slun->product, slun->revision);
    printf("    Type: %d, Removable: %s\n", slun->device_type,
        slun->removable ? "Yes" : "No");
    if (slun->capacity > 0) {
        uint64_t size_mb = (slun->capacity * slun->block_size) / (1024 * 1024);
        printf("    Capacity: %llu MB (%u bytes/sector)\n",
            size_mb, slun->block_size);
    }
}

int32_t scsi_scan_bus(scsi_hba_t *hba) {
    if (!hba) return -1;
    return scsi_discover(hba);
}