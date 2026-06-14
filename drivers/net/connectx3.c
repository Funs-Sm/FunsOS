/*
 * connectx3.c - Mellanox ConnectX-3 / ConnectX-3 Pro 10GbE 网卡驱动
 *
 * 简化实现: 初始化 + 发送 + 接收。
 * 使用 MMIO 方式 (BAR0) 访问设备寄存器。
 * 基于 Mellanox ConnectX-3 PRM (Programmer's Reference Manual) 简化。
 */

#include "connectx3.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "kheap.h"
#include "string.h"
#include "irq.h"

/* ---- 私有状态 ---- */
static volatile uint32_t *mmio_base;

static cx3_rx_desc_t *rx_descs;
static cx3_tx_desc_t *tx_descs;

static void *rx_buffers[CX3_NUM_RX_DESC];
static void *tx_buffers[CX3_NUM_TX_DESC];

static cx3_cqe_t *cq_entries;    /* 完成队列 */
static uint32_t cq_size;

static uint32_t rx_tail;
static uint32_t tx_tail;
static uint32_t cq_head;

static net_interface_t cx3_iface;
static int cx3_inited = 0;

/* ---- 寄存器读写辅助函数 ---- */

static inline uint32_t cx3_read_reg(uint32_t offset) {
    return mmio_base[offset / 4];
}

static inline void cx3_write_reg(uint32_t offset, uint32_t val) {
    mmio_base[offset / 4] = val;
}

/* ---- 命令接口 (简化) ---- */
static int cx3_cmd_wait(void) {
    /* 等待命令完成: 轮询 HCR 直到 GO 位清零 */
    uint32_t timeout = 1000000;
    while ((cx3_read_reg(CX3_REG_HCR) & CX3_HCR_GO_BIT) && timeout--)
        ;
    return timeout == 0 ? -1 : 0;
}

static int cx3_post_cmd(uint32_t opcode, uint32_t opmod,
                         uint32_t in_mod, uint32_t token,
                         uint32_t input_ptr, uint32_t input_len,
                         uint32_t output_ptr, uint32_t output_len) {
    /* 构造命令寄存器值并提交 */
    uint32_t hcr = (opcode << CX3_HCR_OPCODE_SHIFT) | CX3_HCR_GO_BIT;
    hcr |= (opmod & 0xF) << 16;
    hcr |= (token & 0xFF);

    cx3_write_reg(0x000090, input_ptr);    /* 命令输入地址 */
    cx3_write_reg(0x000094, input_len);    /* 命令输入长度 */
    cx3_write_reg(0x000098, output_ptr);   /* 命令输出地址 */
    cx3_write_reg(0x00009C, output_len);   /* 命令输出长度 */
    cx3_write_reg(0x00008C, in_mod);       /* 输入修改器 */
    cx3_write_reg(CX3_REG_HCR, hcr);      /* 提交命令 */

    return cx3_cmd_wait();
}

/* ---- 固件初始化 ---- */
static int cx3_init_fw(void) {
    /* 查询固件版本 */
    if (cx3_post_cmd(CX3_CMD_QUERY_FW, 0, 0, 0, 0, 0, 0, 0) != 0) {
        return -1;
    }

    /* 初始化 HCA (Host Channel Adapter) */
    if (cx3_post_cmd(CX3_CMD_INIT_HCA, 0, 0, 0, 0, 0, 0, 0) != 0) {
        return -1;
    }

    return 0;
}

/* ---- MAC 地址读取 ---- */
static void cx3_read_mac(void) {
    /* ConnectX-3 的 MAC 地址存储在 VPD (Vital Product Data) 中
     * 简化实现: 从固定偏移读取 */
    uint32_t mac_low = cx3_read_reg(0x001000);
    uint32_t mac_high = cx3_read_reg(0x001004);

    cx3_iface.mac.bytes[0] = mac_low & 0xFF;
    cx3_iface.mac.bytes[1] = (mac_low >> 8) & 0xFF;
    cx3_iface.mac.bytes[2] = (mac_low >> 16) & 0xFF;
    cx3_iface.mac.bytes[3] = (mac_low >> 24) & 0xFF;
    cx3_iface.mac.bytes[4] = mac_high & 0xFF;
    cx3_iface.mac.bytes[5] = (mac_high >> 8) & 0xFF;
}

/* ---- 初始化 ---- */
void cx3_init(uint8_t bus, uint8_t dev, uint8_t func) {
    /* 读取 BAR0 (MMIO 基地址) */
    uint32_t bar0 = pci_read_config(bus, dev, func, 0x10);
    uint32_t phys_addr = bar0 & 0xFFFFFFF0;
    mmio_base = (volatile uint32_t *)vmm_map_physical(phys_addr, 0x100000);

    /* 启用 PCI 总线主控和内存空间 */
    uint32_t cmd = pci_read_config(bus, dev, func, 0x04) & 0xFFFF;
    pci_write_config(bus, dev, func, 0x04, cmd | 0x07);

    /* 初始化固件 */
    if (cx3_init_fw() != 0) {
        return;  /* 固件初始化失败 */
    }

    /* 读取 MAC 地址 */
    cx3_read_mac();

    /* 分配 TX 描述符环 (WQE) */
    tx_descs = (cx3_tx_desc_t *)kmalloc(CX3_NUM_TX_DESC * sizeof(cx3_tx_desc_t));
    memset(tx_descs, 0, CX3_NUM_TX_DESC * sizeof(cx3_tx_desc_t));

    /* 分配 RX 描述符环 (WQE) */
    rx_descs = (cx3_rx_desc_t *)kmalloc(CX3_NUM_RX_DESC * sizeof(cx3_rx_desc_t));
    memset(rx_descs, 0, CX3_NUM_RX_DESC * sizeof(cx3_rx_desc_t));

    /* 分配完成队列 */
    cq_size = CX3_NUM_TX_DESC + CX3_NUM_RX_DESC;
    cq_entries = (cx3_cqe_t *)kmalloc(cq_size * sizeof(cx3_cqe_t));
    memset(cq_entries, 0, cq_size * sizeof(cx3_cqe_t));

    /* 分配 TX/RX 缓冲区并初始化描述符 */
    uint32_t i;
    for (i = 0; i < CX3_NUM_TX_DESC; i++) {
        tx_buffers[i] = kmalloc(CX3_TX_BUF_SIZE);
        memset(tx_descs[i].ctrl, 0, sizeof(tx_descs[i].ctrl));
        tx_descs[i].addr = (uint64_t)(uint32_t)tx_buffers[i];
        tx_descs[i].lkey = 0;  /* 简化: 不使用内存保护 */
        tx_descs[i].byte_count = 0;
    }

    for (i = 0; i < CX3_NUM_RX_DESC; i++) {
        rx_buffers[i] = kmalloc(CX3_RX_BUF_SIZE);
        memset(rx_buffers[i], 0, CX3_RX_BUF_SIZE);
        memset(rx_descs[i].ctrl, 0, sizeof(rx_descs[i].ctrl));
        rx_descs[i].addr = (uint64_t)(uint32_t)rx_buffers[i];
        rx_descs[i].lkey = 0;
        rx_descs[i].byte_count = CX3_RX_BUF_SIZE;
    }

    tx_tail = 0;
    rx_tail = 0;
    cq_head = 0;

    /* 配置事件队列 (简化: 仅使用轮询模式) */

    /* 注册中断处理程序 (备用) */
    uint8_t irq_line = pci_read_config(bus, dev, func, 0x3C) & 0xFF;
    irq_register_handler(0x20 + irq_line, cx3_irq_handler);

    /* 注册网络接口 */
    strcpy(cx3_iface.name, "cx3");
    cx3_iface.up = 1;
    cx3_iface.mtu = 1500;
    cx3_iface.send = cx3_send;
    cx3_iface.driver_data = (void *)0;
    net_register_interface(&cx3_iface);

    cx3_inited = 1;
}

/* ---- 发送数据包 ---- */
int cx3_send(net_interface_t *iface, const void *data, uint32_t len) {
    if (!cx3_inited || len > CX3_TX_BUF_SIZE) return -1;

    /* 复制数据到 TX 缓冲区 */
    memcpy(tx_buffers[tx_tail], data, len);

    /* 填写 TX 描述符 (Send WQE) */
    tx_descs[tx_tail].ctrl[0] = (1 << 28);  /* OpCode: Send */
    tx_descs[tx_tail].byte_count = len;
    tx_descs[tx_tail].addr = (uint64_t)(uint32_t)tx_buffers[tx_tail];

    /* 通知硬件有新的发送描述符 (Doorbell) */
    cx3_write_reg(CX3_REG_SQ_DOORBELL, tx_tail);

    uint32_t old_tail = tx_tail;
    tx_tail = (tx_tail + 1) % CX3_NUM_TX_DESC;

    (void)iface;
    return (int)old_tail;
}

/* ---- 轮询接收数据包 ---- */
void cx3_poll(void) {
    if (!cx3_inited) return;

    /* 检查完成队列 */
    uint32_t i;
    for (i = 0; i < cq_size; i++) {
        cx3_cqe_t *cqe = &cq_entries[i];

        /* 检查 CQE 是否有效 (owner 位) */
        if (cqe->opcode & 0x80) {
            uint32_t qpn = cqe->qpn & 0xFFFFFF;

            /* 判断是 TX 完成还是 RX 完成 */
            if (qpn < CX3_NUM_RX_DESC) {
                /* RX 完成: 提取数据包 */
                net_buffer_t *buf = net_alloc_buffer();
                if (buf) {
                    uint32_t pkt_len = cqe->byte_cnt;
                    if (pkt_len > 1518) pkt_len = 1518;
                    memcpy(buf->data, rx_buffers[qpn], pkt_len);
                    buf->len = pkt_len;
                    buf->offset = 0;
                    buf->iface = &cx3_iface;
                    net_receive(buf);
                }

                /* 重新投递 RX 描述符 */
                memset(rx_buffers[qpn], 0, CX3_RX_BUF_SIZE);
                rx_descs[qpn].byte_count = CX3_RX_BUF_SIZE;
                rx_descs[qpn].addr = (uint64_t)(uint32_t)rx_buffers[qpn];

                /* RX Doorbell */
                cx3_write_reg(CX3_REG_RQ_DOORBELL, rx_tail);
                rx_tail = (rx_tail + 1) % CX3_NUM_RX_DESC;
            }

            /* 清除 CQE */
            cqe->opcode = 0;
            cqe->owner ^= 0x80;
        }
    }
}

/* ---- 中断处理 ---- */
void cx3_irq_handler(regs_t *regs) {
    if (!cx3_inited) return;

    /* ConnectX-3 中断处理: 轮询完成队列 */
    cx3_poll();

    (void)regs;
}

/* ---- PCI 探测入口 ---- */
int connectx3_probe(void) {
    /* 扫描 PCI 总线查找 Mellanox ConnectX-3 系列设备 */
    uint16_t bus;
    uint8_t dev, func;
    int found = 0;

    for (bus = 0; bus < PCI_MAX_BUSES && !found; bus++) {
        for (dev = 0; dev < PCI_MAX_DEVICES && !found; dev++) {
            for (func = 0; func < PCI_MAX_FUNCTIONS && !found; func++) {
                uint32_t vd = pci_read_config((uint8_t)bus, dev, func, 0x00);
                if (vd == 0xFFFFFFFF || vd == 0) continue;

                uint16_t vendor_id = vd & 0xFFFF;
                uint16_t device_id = (vd >> 16) & 0xFFFF;

                if (vendor_id == CX3_VENDOR_ID &&
                    (device_id == CX3_DEVICE_ID ||
                     device_id == CX3_PRO_DEVICE_ID)) {
                    found = 1;
                    cx3_init((uint8_t)bus, dev, func);
                }
            }
        }
    }

    return found ? 0 : -1;
}
