#include "nvme.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "kheap.h"
#include "sync.h"
#include "stdio.h"
#include "string.h"
#include "stddef.h"

static nvme_ctrl_t nvme_ctrl;
static mutex_t nvme_lock;

static inline uint32_t nvme_read_reg(uint32_t reg)
{
    volatile uint32_t *ptr = (volatile uint32_t *)(nvme_ctrl.mmio + reg);
    return *ptr;
}

static inline void nvme_write_reg(uint32_t reg, uint32_t value)
{
    volatile uint32_t *ptr = (volatile uint32_t *)(nvme_ctrl.mmio + reg);
    *ptr = value;
}

static int nvme_wait_csts(uint32_t mask, uint32_t expected, uint32_t timeout)
{
    while (timeout > 0) {
        uint32_t csts = nvme_read_reg(NVME_REG_CSTS);
        if ((csts & mask) == expected) {
            return 0;
        }
        timeout--;
    }
    return -1;
}

static int nvme_submit_admin_cmd(nvme_command_t *cmd, nvme_completion_t *cpl)
{
    mutex_lock(&nvme_lock);

    uint16_t tail = nvme_ctrl.admin_sq_tail;
    nvme_command_t *sq = (nvme_command_t *)nvme_ctrl.sq[0].entries;
    memcpy(&sq[tail], cmd, sizeof(nvme_command_t));

    nvme_ctrl.admin_sq_tail = (tail + 1) % nvme_ctrl.sq[0].size;
    nvme_write_reg(NVME_REG_SQ0TDBL, nvme_ctrl.admin_sq_tail);

    uint32_t timeout = NVME_CAP_TIMEOUT;
    while (timeout > 0) {
        uint16_t head = nvme_ctrl.admin_cq_head;
        nvme_completion_t *cq = (nvme_completion_t *)nvme_ctrl.cq[0].entries;

        if (cq[head].status != 0xFFFF) {
            if (cpl) {
                memcpy(cpl, &cq[head], sizeof(nvme_completion_t));
            }
            cq[head].status = 0xFFFF;
            nvme_ctrl.admin_cq_head = (head + 1) % nvme_ctrl.cq[0].size;
            nvme_write_reg(NVME_REG_SQ0TDBL + 0x04, nvme_ctrl.admin_cq_head);

            mutex_unlock(&nvme_lock);
            return (cpl ? cpl->status & 0xFF : 0);
        }
        timeout--;
    }

    mutex_unlock(&nvme_lock);
    return -1;
}

static int nvme_identify_controller(void)
{
    void *buf = pmm_alloc_page();
    if (!buf) return -1;
    memset(buf, 0, 4096);

    nvme_command_t cmd = {0};
    cmd.cdw0 = (NVME_CMD_IDENTIFY << 16);
    cmd.nsid = 0;
    cmd.prp1 = (uint64_t)(uintptr_t)vmm_get_physical(vmm_get_current_dir(), (uint32_t)(uintptr_t)buf);

    nvme_completion_t cpl = {0};
    int result = nvme_submit_admin_cmd(&cmd, &cpl);

    pmm_free_page(buf);
    return result;
}

static int nvme_identify_namespace(uint32_t nsid, uint64_t *size)
{
    void *buf = pmm_alloc_page();
    if (!buf) return -1;
    memset(buf, 0, 4096);

    nvme_command_t cmd = {0};
    cmd.cdw0 = (NVME_CMD_IDENTIFY << 16) | 0x01;
    cmd.nsid = nsid;
    cmd.prp1 = (uint64_t)(uintptr_t)vmm_get_physical(vmm_get_current_dir(), (uint32_t)(uintptr_t)buf);

    nvme_completion_t cpl = {0};
    int result = nvme_submit_admin_cmd(&cmd, &cpl);

    if (result == NVME_STATUS_SUCCESS && size) {
        uint64_t *nsze = (uint64_t *)buf;
        *size = nsze[0];
    }

    pmm_free_page(buf);
    return result;
}

static int nvme_create_io_cq(uint16_t qid, uint16_t qsize, uint32_t prp)
{
    nvme_command_t cmd = {0};
    cmd.cdw0 = (NVME_CMD_CREATE_IO_CQ << 16) | qid;
    cmd.cdw10 = (qsize - 1) | (qid << 16);
    cmd.cdw11 = 1;
    cmd.prp1 = prp;

    return nvme_submit_admin_cmd(&cmd, NULL);
}

static int nvme_create_io_sq(uint16_t qid, uint16_t qsize, uint32_t prp)
{
    nvme_command_t cmd = {0};
    cmd.cdw0 = (NVME_CMD_CREATE_IO_SQ << 16) | qid;
    cmd.cdw10 = (qsize - 1) | (qid << 16);
    cmd.cdw11 = qid | (1 << 1);
    cmd.prp1 = prp;

    return nvme_submit_admin_cmd(&cmd, NULL);
}

static int nvme_io_rw(uint32_t nsid, uint64_t lba, uint32_t count, void *buf, int write)
{
    if (nsid == 0 || nsid > nvme_ctrl.ns_count) return -1;

    void *prp_page = pmm_alloc_page();
    if (!prp_page) return -1;

    nvme_command_t *sq = (nvme_command_t *)nvme_ctrl.sq[1].entries;
    nvme_completion_t *cq = (nvme_completion_t *)nvme_ctrl.cq[1].entries;
    uint16_t qsize = nvme_ctrl.sq[1].size;

    mutex_lock(&nvme_lock);

    uint16_t tail = nvme_ctrl.sq[1].tail;
    uint16_t next_tail = (tail + 1) % qsize;

    if (next_tail == nvme_ctrl.sq[1].head) {
        mutex_unlock(&nvme_lock);
        pmm_free_page(prp_page);
        return -1;
    }

    sq[tail].cdw0 = (write ? NVME_CMD_WRITE : NVME_CMD_READ) << 16;
    sq[tail].nsid = nsid;
    sq[tail].prp1 = (uint64_t)(uintptr_t)vmm_get_physical(vmm_get_current_dir(), (uint32_t)(uintptr_t)buf);
    sq[tail].cdw10 = (uint32_t)(lba & 0xFFFFFFFF);
    sq[tail].cdw11 = (uint32_t)(lba >> 32);
    sq[tail].cdw12 = count - 1;

    nvme_ctrl.sq[1].tail = next_tail;
    nvme_write_reg(NVME_REG_SQ0TDBL + 0x08 + 1 * 0x08, nvme_ctrl.sq[1].tail);

    uint32_t timeout = NVME_CAP_TIMEOUT;
    while (timeout > 0) {
        uint16_t head = nvme_ctrl.cq[1].head;
        if (cq[head].status != 0xFFFF) {
            int status = (cq[head].status & 0xFF);
            cq[head].status = 0xFFFF;
            nvme_ctrl.cq[1].head = (head + 1) % qsize;
            nvme_write_reg(NVME_REG_SQ0TDBL + 0x0C + 1 * 0x08, nvme_ctrl.cq[1].head);

            mutex_unlock(&nvme_lock);
            pmm_free_page(prp_page);
            return (status == NVME_STATUS_SUCCESS) ? 0 : -1;
        }
        timeout--;
    }

    mutex_unlock(&nvme_lock);
    pmm_free_page(prp_page);
    return -1;
}

int32_t nvme_read(uint32_t nsid, uint64_t lba, uint32_t count, void *buf)
{
    return nvme_io_rw(nsid, lba, count, buf, 0);
}

int32_t nvme_write(uint32_t nsid, uint64_t lba, uint32_t count, const void *buf)
{
    return nvme_io_rw(nsid, lba, count, (void *)buf, 1);
}

void nvme_identify(uint32_t nsid)
{
    uint64_t size = 0;
    if (nvme_identify_namespace(nsid, &size) == NVME_STATUS_SUCCESS) {
        printf("NVMe Namespace %u: %llu blocks\n", nsid, size);
    }
}

int32_t nvme_reset(void)
{
    uint32_t cc = nvme_read_reg(NVME_REG_CC);
    cc &= ~0x01;
    nvme_write_reg(NVME_REG_CC, cc);

    if (nvme_wait_csts(0x01, 0x00, NVME_CAP_TIMEOUT) != 0) {
        return -1;
    }
    return 0;
}

void nvme_init(void)
{
    memset(&nvme_ctrl, 0, sizeof(nvme_ctrl));
    mutex_init(&nvme_lock);

    pci_device_t *pci_dev = pci_find_device(0x01, 0x08);
    if (pci_dev == NULL) {
        printf("NVMe controller not found\n");
        return;
    }

    uint32_t bar5 = pci_read_config(pci_dev->bus, pci_dev->device, pci_dev->function, 0x24);
    bar5 &= 0xFFFFFFF0;

    nvme_ctrl.mmio = (uint8_t *)vmm_map_physical(bar5, 0x10000);
    if (!nvme_ctrl.mmio) {
        printf("Failed to map NVMe BAR\n");
        return;
    }

    uint32_t cap = nvme_read_reg(NVME_REG_CAP);
    uint32_t timeout = (cap >> 24) & 0xFF;

    if (nvme_reset() != 0) {
        printf("NVMe reset failed\n");
        return;
    }

    void *asq = pmm_alloc_page();
    void *acq = pmm_alloc_page();
    if (!asq || !acq) {
        printf("Failed to allocate admin queues\n");
        pmm_free_page(asq);
        pmm_free_page(acq);
        return;
    }
    memset(asq, 0, 4096);
    memset(acq, 0xFF, 4096);

    nvme_ctrl.sq[0].entries = (uint32_t *)asq;
    nvme_ctrl.cq[0].entries = (uint32_t *)acq;
    nvme_ctrl.sq[0].size = 256;
    nvme_ctrl.cq[0].size = 256;

    nvme_write_reg(NVME_REG_AQA, (255 << 16) | 255);
    nvme_write_reg(NVME_REG_ASQ, (uint32_t)(uintptr_t)vmm_get_physical(vmm_get_current_dir(), (uint32_t)(uintptr_t)asq));
    nvme_write_reg(NVME_REG_ACQ, (uint32_t)(uintptr_t)vmm_get_physical(vmm_get_current_dir(), (uint32_t)(uintptr_t)acq));

    uint32_t cc = nvme_read_reg(NVME_REG_CC);
    cc = 0x01;
    nvme_write_reg(NVME_REG_CC, cc);

    if (nvme_wait_csts(0x01, 0x01, timeout * 500000) != 0) {
        printf("NVMe enable failed\n");
        return;
    }

    if (nvme_identify_controller() != NVME_STATUS_SUCCESS) {
        printf("NVMe identify controller failed\n");
        return;
    }

    void *io_sq = pmm_alloc_page();
    void *io_cq = pmm_alloc_page();
    if (!io_sq || !io_cq) {
        printf("Failed to allocate IO queues\n");
        pmm_free_page(io_sq);
        pmm_free_page(io_cq);
        return;
    }
    memset(io_sq, 0, 4096);
    memset(io_cq, 0xFF, 4096);

    if (nvme_create_io_cq(1, 256, (uintptr_t)vmm_get_physical(vmm_get_current_dir(), (uint32_t)(uintptr_t)io_cq)) != NVME_STATUS_SUCCESS) {
        printf("Failed to create IO CQ\n");
        pmm_free_page(io_sq);
        pmm_free_page(io_cq);
        return;
    }

    if (nvme_create_io_sq(1, 256, (uintptr_t)vmm_get_physical(vmm_get_current_dir(), (uint32_t)(uintptr_t)io_sq)) != NVME_STATUS_SUCCESS) {
        printf("Failed to create IO SQ\n");
        pmm_free_page(io_sq);
        pmm_free_page(io_cq);
        return;
    }

    nvme_ctrl.sq[1].entries = (uint32_t *)io_sq;
    nvme_ctrl.cq[1].entries = (uint32_t *)io_cq;
    nvme_ctrl.sq[1].size = 256;
    nvme_ctrl.cq[1].size = 256;
    nvme_ctrl.cq[1].phase = 1;

    for (uint32_t i = 1; i < NVME_MAX_NAMESPACES; i++) {
        uint64_t size = 0;
        if (nvme_identify_namespace(i, &size) == NVME_STATUS_SUCCESS && size > 0) {
            nvme_ctrl.ns_sizes[i] = size;
            nvme_ctrl.ns_count = i;
            printf("NVMe: Found namespace %u with %llu blocks\n", i, size);
        }
    }

    nvme_ctrl.initialized = 1;
    printf("NVMe controller initialized successfully\n");
}
