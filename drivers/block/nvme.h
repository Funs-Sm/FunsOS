#ifndef NVME_H
#define NVME_H

#include "stdint.h"

#define NVME_MAX_QUEUES 16
#define NVME_MAX_NAMESPACES 16
#define NVME_PRP2_LIMIT 4096

typedef struct {
    uint16_t sqid;
    uint16_t size;
    uint16_t head;
    uint16_t tail;
    uint32_t *entries;
    uint32_t *doorbell;
} nvme_sq_t;

typedef struct {
    uint16_t cqid;
    uint16_t size;
    uint16_t head;
    uint16_t tail;
    uint16_t phase;
    uint32_t *entries;
    uint32_t *doorbell;
} nvme_cq_t;

typedef struct {
    uint32_t pci_bus;
    uint32_t pci_dev;
    uint32_t pci_func;
    uint32_t reg_base;
    uint32_t ns_count;
    uint64_t ns_sizes[NVME_MAX_NAMESPACES];
    nvme_sq_t sq[NVME_MAX_QUEUES];
    nvme_cq_t cq[NVME_MAX_QUEUES];
    uint32_t admin_sq_tail;
    uint32_t admin_cq_head;
    uint8_t initialized;
    uint8_t *mmio;
} nvme_ctrl_t;

#define NVME_CMD_READ     0x02
#define NVME_CMD_WRITE    0x01
#define NVME_CMD_IDENTIFY 0x06
#define NVME_CMD_CREATE_IO_CQ 0x05
#define NVME_CMD_CREATE_IO_SQ 0x01
#define NVME_CMD_DELETE_IO_SQ 0x00
#define NVME_CMD_DELETE_IO_CQ 0x04
#define NVME_CMD_GET_LOG_PAGE 0x02

#define NVME_STATUS_SUCCESS 0x00
#define NVME_CAP_TIMEOUT 1000000

typedef struct {
    uint32_t cdw0;
    uint32_t nsid;
    uint32_t cdw2;
    uint32_t cdw3;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} nvme_command_t;

typedef struct {
    uint32_t cdw0;
    uint32_t rsvd;
    uint16_t sqhd;
    uint16_t sqid;
    uint16_t cid;
    uint16_t status;
} nvme_completion_t;

#define NVME_REG_CAP 0x00
#define NVME_REG_VS 0x08
#define NVME_REG_INTMS 0x0C
#define NVME_REG_INTMC 0x10
#define NVME_REG_CC 0x14
#define NVME_REG_CSTS 0x1C
#define NVME_REG_AQA 0x24
#define NVME_REG_ASQ 0x28
#define NVME_REG_ACQ 0x30
#define NVME_REG_SQ0TDBL 0x1000

void nvme_init(void);
int32_t nvme_read(uint32_t nsid, uint64_t lba, uint32_t count, void *buf);
int32_t nvme_write(uint32_t nsid, uint64_t lba, uint32_t count, const void *buf);
void nvme_identify(uint32_t nsid);
int32_t nvme_reset(void);

#endif
