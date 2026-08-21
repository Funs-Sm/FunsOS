#ifndef NVME_PCIE_H
#define NVME_PCIE_H

#include "stdint.h"

/* NVMe-over-PCIe storage driver.
 *
 * Implements a subset of the NVM Express interface (NVMe 1.4c spec)
 * mapped onto a generic PCIe device. The driver handles:
 *   - PCIe BAR0 register access (Controller Memory Buffer - MMIO)
 *   - Admin queue (submission / completion) and I/O queues
 *   - Identify controller, Identify namespace, Create I/O completion queue
 *   - Read / Write of arbitrary LBAs (block size from Identify)
 *   - Submission queue doorbell and completion polling
 *
 * It also implements NVMe PRP (Physical Region Page) lists for buffers
 * that exceed a single 4 KB page.
 */

#define NVME_PCIE_VENDOR_ID     0x1D79  /* example vendor */
#define NVME_PCIE_DEVICE_ID     0x0201

/* Controller registers (BAR0, MMIO 4 KB). */
#define NVME_REG_CAP            0x0000
#define NVME_REG_VS             0x0008
#define NVME_REG_CC             0x0014
#define NVME_REG_CSTS           0x001C
#define NVME_REG_AQA            0x0024
#define NVME_REG_ASQ            0x0028
#define NVME_REG_ACQ            0x0030
#define NVME_REG_DBS            0x1000  /* doorbell stride */

/* CC bits */
#define NVME_CC_EN              (1 << 0)
#define NVME_CC_CSS_NVM         (0 << 4)
#define NVME_CC_PAGE_4KB        (0 << 7)
#define NVME_CC_ARB_RR          (0 << 11)
#define NVME_CC_IOSQES_64       (6 << 16)
#define NVME_CC_IOCQES_16       (4 << 20)

/* CSTS bits */
#define NVME_CSTS_RDY           (1 << 0)
#define NVME_CSTS_CFS           (1 << 1)

/* NVMe opcodes */
#define NVME_OP_DELETE_SQ       0x00
#define NVME_OP_CREATE_SQ       0x01
#define NVME_OP_DELETE_CQ       0x04
#define NVME_OP_CREATE_CQ       0x05
#define NVME_OP_IDENTIFY        0x06
#define NVME_OP_READ            0x02
#define NVME_OP_WRITE           0x01
#define NVME_OP_FLUSH           0x0C

/* Submission queue entry */
typedef struct __attribute__((packed)) {
    uint8_t  opcode;
    uint8_t  flags;
    uint16_t cid;
    uint32_t nsid;
    uint64_t reserved;
    uint64_t metadata;
    uint64_t prp[2];
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} nvme_sqe_t;

/* Completion queue entry */
typedef struct __attribute__((packed)) {
    uint32_t dw0;
    uint32_t dw1;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t cid;
    uint16_t status;
} nvme_cqe_t;

#define NVME_ADMIN_QUEUE_DEPTH  64
#define NVME_IO_QUEUE_DEPTH     64
#define NVME_MAX_NAMESPACES     4
#define NVME_MAX_PRP_LIST_ENTS  16

typedef struct {
    nvme_sqe_t  *sq_virt;
    nvme_cqe_t  *cq_virt;
    uint32_t     sq_paddr;
    uint32_t     cq_paddr;
    uint16_t     sq_tail;
    uint16_t     cq_head;
    uint16_t     cid_counter;
} nvme_queue_t;

typedef struct {
    uint32_t nsid;
    uint64_t block_count;
    uint32_t block_size;
    int      present;
} nvme_namespace_t;

typedef struct {
    volatile uint32_t *regs;
    nvme_queue_t adminq;
    nvme_queue_t ioq;
    nvme_namespace_t namespaces[NVME_MAX_NAMESPACES];
    uint8_t n_namespaces;
    uint32_t page_size;
    uint32_t doorbell_stride;
    int      inited;
    uint64_t ios_done;
} nvme_pcie_t;

int nvme_pcie_init(uint8_t bus, uint8_t dev, uint8_t func);
int nvme_pcie_read(uint32_t nsid, uint64_t lba, uint32_t blocks, void *buf);
int nvme_pcie_write(uint32_t nsid, uint64_t lba, uint32_t blocks, const void *buf);
int nvme_pcie_get_namespace_count(void);
const nvme_namespace_t *nvme_pcie_get_namespace(int idx);

#endif