/* nvme_pcie.c - NVMe PCIe storage driver (subset).
 *
 * Probes a generic NVMe-compliant PCIe device and provides simple
 * LBA-based read / write. Includes admin + I/O queue allocation,
 * PRP-1 (single page) / PRP-2 (list) construction, and synchronous
 * completion polling.
 */

#include "nvme_pcie.h"
#include "pci.h"
#include "kheap.h"
#include "string.h"

static nvme_pcie_t g_nvme;
static uint8_t g_bus, g_dev, g_func;

static uint32_t reg_read32(uint32_t off) {
    return g_nvme.regs[off / 4];
}
static void reg_write32(uint32_t off, uint32_t v) {
    g_nvme.regs[off / 4] = v;
}

static int probe_pci(uint8_t bus, uint8_t dev, uint8_t func) {
    uint32_t id = pci_read_config(bus, dev, func, 0x00);
    uint16_t v = (uint16_t)(id & 0xFFFF);
    uint16_t d = (uint16_t)((id >> 16) & 0xFFFF);
    return (v == NVME_PCIE_VENDOR_ID && d == NVME_PCIE_DEVICE_ID) ? 0 : -1;
}

/* Allocate one queue pair with depth D. */
static int alloc_queue(nvme_queue_t *q, uint32_t depth) {
    q->sq_virt = (nvme_sqe_t *)kmalloc(depth * sizeof(nvme_sqe_t));
    q->cq_virt = (nvme_cqe_t *)kmalloc(depth * sizeof(nvme_cqe_t));
    if (!q->sq_virt || !q->cq_virt) return -1;
    memset(q->sq_virt, 0, depth * sizeof(nvme_sqe_t));
    memset(q->cq_virt, 0, depth * sizeof(nvme_cqe_t));
    q->sq_paddr = (uint32_t)q->sq_virt;
    q->cq_paddr = (uint32_t)q->cq_virt;
    q->sq_tail = 0;
    q->cq_head = 0;
    q->cid_counter = 1;
    return 0;
}

/* Build PRP list for buffer > 1 page. Allocates a separate list of
 * page-frame physical addresses. */
static int build_prp_list(void *buf, uint32_t len, uint32_t page_size,
                          uint64_t *prp1, uint64_t *prp2,
                          uint32_t *prp2_count)
{
    uint32_t paddr = (uint32_t)buf;
    uint32_t off_in_page = paddr % page_size;
    uint32_t first_page_bytes = page_size - off_in_page;

    if (len <= first_page_bytes) {
        *prp1 = paddr;
        *prp2 = 0;
        *prp2_count = 0;
        return 0;
    }
    /* Use PRP2 as a list pointer. */
    uint32_t remain = len - first_page_bytes;
    uint32_t list_entries = (remain + page_size - 1) / page_size;
    if (list_entries > NVME_MAX_PRP_LIST_ENTS) return -1;

    /* Allocate contiguous PRP list memory. */
    static uint64_t prp_list_storage[NVME_MAX_PRP_LIST_ENTS];
    memset(prp_list_storage, 0, sizeof(prp_list_storage));
    uint32_t cur = paddr + first_page_bytes;
    for (uint32_t i = 0; i < list_entries; i++) {
        prp_list_storage[i] = cur;
        cur += page_size;
    }
    *prp1 = paddr;
    *prp2 = (uint64_t)(uint32_t)prp_list_storage;
    *prp2_count = list_entries;
    return 0;
}

/* Submit a command on the I/O queue and wait for completion. */
static int io_submit(nvme_sqe_t *cmd) {
    cmd->cid = ++g_nvme.ioq.cid_counter;
    if (g_nvme.ioq.cid_counter == 0) g_nvme.ioq.cid_counter = 1;
    g_nvme.ioq.sq_virt[g_nvme.ioq.sq_tail] = *cmd;
    /* Ring doorbell. */
    volatile uint32_t *doorbell = g_nvme.regs
                                + (NVME_REG_DBS / 4)
                                + (g_nvme.doorbell_stride / 4) * 0;
    *doorbell = ++g_nvme.ioq.sq_tail;
    /* Poll completion. */
    for (volatile int t = 0; t < 100000; t++) {
        nvme_cqe_t *cqe = &g_nvme.ioq.cq_virt[g_nvme.ioq.cq_head];
        if (cqe->status == 0 && cqe->cid == cmd->cid) {
            /* Ring completion doorbell. */
            volatile uint32_t *cq_db = g_nvme.regs
                                     + (NVME_REG_DBS / 4)
                                     + (g_nvme.doorbell_stride / 4) * 1;
            *cq_db = ++g_nvme.ioq.cq_head;
            g_nvme.ios_done++;
            return 0;
        }
    }
    return -1;
}

static int admin_submit(nvme_sqe_t *cmd) {
    cmd->cid = ++g_nvme.adminq.cid_counter;
    if (g_nvme.adminq.cid_counter == 0) g_nvme.adminq.cid_counter = 1;
    g_nvme.adminq.sq_virt[g_nvme.adminq.sq_tail] = *cmd;
    volatile uint32_t *doorbell = g_nvme.regs
                                + (NVME_REG_DBS / 4)
                                + (g_nvme.doorbell_stride / 4) * 0;
    *doorbell = ++g_nvme.adminq.sq_tail;
    for (volatile int t = 0; t < 100000; t++) {
        nvme_cqe_t *cqe = &g_nvme.adminq.cq_virt[g_nvme.adminq.cq_head];
        if (cqe->status == 0 && cqe->cid == cmd->cid) {
            volatile uint32_t *cq_db = g_nvme.regs
                                     + (NVME_REG_DBS / 4)
                                     + (g_nvme.doorbell_stride / 4) * 1;
            *cq_db = ++g_nvme.adminq.cq_head;
            return 0;
        }
    }
    return -1;
}

static int admin_identify(uint32_t nsid, void *data) {
    nvme_sqe_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_OP_IDENTIFY;
    cmd.nsid = nsid;
    cmd.prp[0] = (uint64_t)(uint32_t)data;
    cmd.cdw10 = 0;  /* CNS = 0 = identify controller */
    if (nsid) cmd.cdw10 = 0x01;  /* CNS = 1 = identify namespace */
    return admin_submit(&cmd);
}

/* Identify all namespaces. */
static int probe_namespaces(void) {
    void *buf = kmalloc(4096);
    if (!buf) return -1;
    memset(buf, 0, 4096);
    if (admin_identify(0, buf) == 0) {
        /* Parse the controller's namespace list (NN field at offset 516). */
        uint8_t *p = (uint8_t *)buf;
        uint32_t nn = (uint32_t)p[516] | ((uint32_t)p[517] << 8)
                    | ((uint32_t)p[518] << 16) | ((uint32_t)p[519] << 24);
        if (nn > NVME_MAX_NAMESPACES) nn = NVME_MAX_NAMESPACES;
        g_nvme.n_namespaces = 0;
        for (uint32_t i = 1; i <= nn; i++) {
            void *nsbuf = kmalloc(4096);
            if (!nsbuf) break;
            memset(nsbuf, 0, 4096);
            if (admin_identify(i, nsbuf) != 0) {
                kfree(nsbuf);
                continue;
            }
            uint8_t *q = (uint8_t *)nsbuf;
            uint64_t nsze = (uint64_t)q[0] | ((uint64_t)q[1] << 8)
                          | ((uint64_t)q[2] << 16) | ((uint64_t)q[3] << 24)
                          | ((uint64_t)q[4] << 32) | ((uint64_t)q[5] << 40)
                          | ((uint64_t)q[6] << 48) | ((uint64_t)q[7] << 56);
            uint32_t flbs = (uint32_t)q[26] | ((uint32_t)q[27] << 8);
            uint32_t lba_shift = (flbs & 0xFF);
            uint32_t block_size = 1u << lba_shift;
            if (block_size < 512) block_size = 512;
            g_nvme.namespaces[i - 1].nsid = i;
            g_nvme.namespaces[i - 1].block_count = nsze;
            g_nvme.namespaces[i - 1].block_size = block_size;
            g_nvme.namespaces[i - 1].present = 1;
            g_nvme.n_namespaces = i;
            kfree(nsbuf);
        }
    }
    kfree(buf);
    return 0;
}

int nvme_pcie_init(uint8_t bus, uint8_t dev, uint8_t func) {
    if (g_nvme.inited) return 0;
    if (probe_pci(bus, dev, func) != 0) return -1;
    g_bus = bus; g_dev = dev; g_func = func;

    uint32_t bar = pci_read_config(bus, dev, func, 0x10);
    if (bar & 0x01) return -1;
    g_nvme.regs = (volatile uint32_t *)(bar & 0xFFFFFFF0);
    g_nvme.page_size = 4096;

    /* Enable PCI device (bus master + memory). */
    uint32_t cmd = pci_read_config(bus, dev, func, 0x04);
    cmd |= 0x06;
    pci_write_config(bus, dev, func, 0x04, cmd);

    /* Allocate admin and I/O queues. */
    if (alloc_queue(&g_nvme.adminq, NVME_ADMIN_QUEUE_DEPTH) != 0) return -1;
    if (alloc_queue(&g_nvme.ioq, NVME_IO_QUEUE_DEPTH) != 0) return -1;

    /* Configure admin queue via AQA / ASQ / ACQ. */
    reg_write32(NVME_REG_AQA, ((NVME_ADMIN_QUEUE_DEPTH - 1) << 0)
                          | ((NVME_ADMIN_QUEUE_DEPTH - 1) << 16));
    reg_write32(NVME_REG_ASQ, g_nvme.adminq.sq_paddr);
    reg_write32(NVME_REG_ACQ, g_nvme.adminq.cq_paddr);

    /* Set CC.EN to enable controller. */
    uint32_t cap = reg_read32(NVME_REG_CAP);
    uint32_t dstrd = (uint32_t)((((uint64_t)cap) >> 32) & 0xF);
    g_nvme.doorbell_stride = 4u << dstrd;
    reg_write32(NVME_REG_CC, NVME_CC_EN | NVME_CC_CSS_NVM
                              | NVME_CC_PAGE_4KB | NVME_CC_ARB_RR
                              | NVME_CC_IOSQES_64 | NVME_CC_IOCQES_16);
    /* Wait for RDY. */
    for (volatile int t = 0; t < 100000; t++) {
        if (reg_read32(NVME_REG_CSTS) & NVME_CSTS_RDY) break;
    }

    /* Probe namespaces. */
    probe_namespaces();
    g_nvme.inited = 1;
    return 0;
}

int nvme_pcie_read(uint32_t nsid, uint64_t lba, uint32_t blocks, void *buf) {
    if (!g_nvme.inited) return -1;
    uint32_t page_size = g_nvme.page_size;
    nvme_namespace_t *ns = (nsid <= g_nvme.n_namespaces) ?
                            &g_nvme.namespaces[nsid - 1] : 0;
    if (!ns || !ns->present) return -1;
    uint32_t bytes = blocks * ns->block_size;
    if (!buf || bytes == 0) return -1;

    uint64_t prp1, prp2; uint32_t prp2_count;
    if (build_prp_list(buf, bytes, page_size, &prp1, &prp2, &prp2_count) != 0)
        return -1;

    nvme_sqe_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_OP_READ;
    cmd.nsid = nsid;
    cmd.prp[0] = prp1;
    cmd.prp[1] = prp2;
    cmd.cdw10 = (uint32_t)(lba & 0xFFFFFFFF);
    cmd.cdw11 = (uint32_t)(lba >> 32);
    cmd.cdw12 = (uint32_t)(blocks - 1);
    return io_submit(&cmd);
}

int nvme_pcie_write(uint32_t nsid, uint64_t lba, uint32_t blocks, const void *buf) {
    if (!g_nvme.inited) return -1;
    uint32_t page_size = g_nvme.page_size;
    nvme_namespace_t *ns = (nsid <= g_nvme.n_namespaces) ?
                            &g_nvme.namespaces[nsid - 1] : 0;
    if (!ns || !ns->present) return -1;
    uint32_t bytes = blocks * ns->block_size;
    if (!buf || bytes == 0) return -1;

    uint64_t prp1, prp2; uint32_t prp2_count;
    if (build_prp_list((void *)buf, bytes, page_size, &prp1, &prp2, &prp2_count) != 0)
        return -1;

    nvme_sqe_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_OP_WRITE;
    cmd.nsid = nsid;
    cmd.prp[0] = prp1;
    cmd.prp[1] = prp2;
    cmd.cdw10 = (uint32_t)(lba & 0xFFFFFFFF);
    cmd.cdw11 = (uint32_t)(lba >> 32);
    cmd.cdw12 = (uint32_t)(blocks - 1);
    return io_submit(&cmd);
}

int nvme_pcie_get_namespace_count(void) {
    return g_nvme.inited ? g_nvme.n_namespaces : 0;
}

const nvme_namespace_t *nvme_pcie_get_namespace(int idx) {
    if (!g_nvme.inited || idx < 0 || idx >= g_nvme.n_namespaces)
        return (const nvme_namespace_t *)0;
    return &g_nvme.namespaces[idx];
}