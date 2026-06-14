#include "ne2000.h"
#include "pci.h"
#include "io.h"
#include "kheap.h"
#include "string.h"
#include "stddef.h"
/* ------------------------------------------------------------------ */
/* Register offsets                                                   */
/* ------------------------------------------------------------------ */
#define NE2K_REG_CR      0x00   /* Command register                */
#define NE2K_REG_PSTART  0x01   /* Page start (page 0)             */
#define NE2K_REG_PSTOP   0x02   /* Page stop  (page 0)             */
#define NE2K_REG_BNRY    0x03   /* Boundary pointer (page 0)       */
#define NE2K_REG_TSR     0x04
#define NE2K_REG_ISR     0x07   /* Interrupt status                */
#define NE2K_REG_CURR    0x07   /* Current page (page 1)           */
#define NE2K_REG_RCR     0x0C
#define NE2K_REG_TCR     0x0D
#define NE2K_REG_DCR     0x0E
#define NE2K_REG_IMR     0x0F
#define NE2K_REG_DATA    0x10
#define NE2K_REG_RESET   0x1F

/* CR bits */
#define NE2K_CR_STP      0x01
#define NE2K_CR_STA      0x02
#define NE2K_CR_TXP      0x04
#define NE2K_CR_RD0      0x08
#define NE2K_CR_RD1      0x10
#define NE2K_CR_RD2      0x20
#define NE2K_CR_PS0      0x40
#define NE2K_CR_PS1      0x80

/* RD0+RD1 commands */
#define NE2K_CR_DMA_READ   (0x01 << 3)
#define NE2K_CR_DMA_WRITE  (0x02 << 3)
#define NE2K_CR_DMA_SEND   (0x03 << 3)
#define NE2K_CR_DMA_ABORT  (0x04 << 3)

/* ISR bits */
#define NE2K_ISR_RST      0x80
#define NE2K_ISR_PRX      0x01
#define NE2K_ISR_PTX      0x02
#define NE2K_ISR_RXE      0x04
#define NE2K_ISR_TXE      0x08
#define NE2K_ISR_OVW      0x10

/* DCR */
#define NE2K_DCR_BYTE_WIDE   0x00
#define NE2K_DCR_WORD_WIDE   0x01
#define NE2K_DCR_FIFO_8      0x40
#define NE2K_DCR_AUTOINIT    0x10

/* RCR */
#define NE2K_RCR_APROM       0x00
#define NE2K_RCR_MONITOR     0x20
#define NE2K_RCR_ALLPKT      0x10
#define NE2K_RCR_BROADCAST   0x04
#define NE2K_RCR_MULTICAST   0x08
#define NE2K_RCR_PHYSICAL    0x00

const uint16_t ne2k_isa_bases[NE2K_ISA_BASES_NUM] = {
    0x300, 0x280, 0x320, 0x340, 0x360
};

/* ------------------------------------------------------------------ */
/* Per-device state                                                   */
/* ------------------------------------------------------------------ */
static uint16_t  ne2k_base;
static uint8_t   ne2k_mac_addr[6];
static uint8_t   ne2k_up;
static uint8_t   ne2k_next;      /* next page in RX ring        */
static uint8_t   ne2k_curr;      /* CURR -- the chip's read ptr */

static net_interface_t ne2k_iface;

/* ------------------------------------------------------------------ */
/* Low-level helpers                                                  */
/* ------------------------------------------------------------------ */

static inline void ne2k_set_page(uint8_t page) {
    /* Always select page 0/1 by toggling PS0/PS1, keep STA set */
    uint8_t cr = NE2K_CR_STA | (page ? NE2K_CR_PS1 : 0);
    outb(ne2k_base + NE2K_REG_CR, cr);
}

static inline void ne2k_write_reg(uint8_t page, uint8_t reg, uint8_t val) {
    ne2k_set_page(page);
    outb(ne2k_base + reg, val);
}

static inline uint8_t ne2k_read_reg(uint8_t page, uint8_t reg) {
    ne2k_set_page(page);
    return inb(ne2k_base + reg);
}

static inline void ne2k_cmd(uint8_t cmd) {
    outb(ne2k_base + NE2K_REG_CR, cmd);
}

/* Read `len` bytes from the on-chip buffer starting at `addr` using
 * remote-DMA read.  PIO is in 16-bit words. */
static void ne2k_dma_read(uint16_t addr, void *dst, uint16_t len) {
    /* Set RBCR0/RBCR1 (0x14/0x15) and RSAR0/RSAR1 (0x08/0x09) */
    ne2k_set_page(0);
    outb(ne2k_base + 0x08, (uint8_t)(addr & 0xFF));
    outb(ne2k_base + 0x09, (uint8_t)(addr >> 8));
    outb(ne2k_base + 0x0A, (uint8_t)(len & 0xFF));
    outb(ne2k_base + 0x0B, (uint8_t)(len >> 8));
    /* Issue remote read */
    outb(ne2k_base + NE2K_REG_CR, NE2K_CR_STA | NE2K_CR_DMA_READ);

    uint8_t *p = (uint8_t *)dst;
    for (uint16_t i = 0; i < len; i++) {
        p[i] = inb(ne2k_base + NE2K_REG_DATA);
    }
}

/* Write `len` bytes to the on-chip buffer at `addr` using remote-DMA
 * write. */
static void ne2k_dma_write(uint16_t addr, const void *src, uint16_t len) {
    ne2k_set_page(0);
    outb(ne2k_base + 0x08, (uint8_t)(addr & 0xFF));
    outb(ne2k_base + 0x09, (uint8_t)(addr >> 8));
    outb(ne2k_base + 0x0A, (uint8_t)(len & 0xFF));
    outb(ne2k_base + 0x0B, (uint8_t)(len >> 8));
    outb(ne2k_base + NE2K_REG_CR, NE2K_CR_STA | NE2K_CR_DMA_WRITE);

    const uint8_t *p = (const uint8_t *)src;
    for (uint16_t i = 0; i < len; i++) {
        outb(ne2k_base + NE2K_REG_DATA, p[i]);
    }
}

/* ------------------------------------------------------------------ */
/* Detection                                                          */
/* ------------------------------------------------------------------ */

static int ne2k_detect_at(uint16_t base) {
    /* Sanity probe: read a benign register, write another, verify
     * the write sticks by reading it back. */
    ne2k_base = base;

    /* Reset the chip */
    inb(base + NE2K_REG_RESET);
    for (volatile int i = 0; i < 1000; i++) { io_wait(); }

    /* Read+write ISR -- reset clears it (bit 7 RST set), reading
     * should show 0x80.  Writing 0xFF then reading should yield the
     * low bits cleared (write-1-to-clear semantics). */
    uint8_t r = inb(base + NE2K_REG_ISR);
    if ((r & NE2K_ISR_RST) == 0) {
        return 0;
    }
    outb(base + NE2K_REG_ISR, 0xFF);
    r = inb(base + NE2K_REG_ISR);
    /* We expect the low bits to clear and RST to stay (read-only). */
    if (r & (NE2K_ISR_PRX | NE2K_ISR_PTX)) {
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Bring-up                                                           */
/* ------------------------------------------------------------------ */

static void ne2k_read_mac(void) {
    /* Read the 6-byte factory MAC out of the PROM region (first
     * 16 bytes of the on-chip buffer).  For ISA PIO the PROM is
     * mapped at offsets 0x00..0x0F of the data port when RCR is in
     * APROM mode.  We do it the simpler way: DMA-read 16 bytes from
     * address 0 and trust the layout. */
    uint8_t prom[16];
    ne2k_dma_read(0, prom, 16);
    for (int i = 0; i < 6; i++) {
        ne2k_mac_addr[i] = prom[i];
    }
}

static int ne2k_init_at(uint16_t base) {
    if (!ne2k_detect_at(base)) {
        return 0;
    }
    ne2k_base = base;

    /* Full software reset */
    inb(base + NE2K_REG_RESET);
    for (volatile int i = 0; i < 5000; i++) { io_wait(); }

    /* Select page 0 and stop the chip */
    ne2k_cmd(NE2K_CR_STP);
    for (volatile int i = 0; i < 1000; i++) { io_wait(); }

    /* DCR: 8-bit bus, 8-byte FIFO, auto-init remote DMA */
    ne2k_write_reg(0, NE2K_REG_DCR, NE2K_DCR_BYTE_WIDE | NE2K_DCR_FIFO_8 | NE2K_DCR_AUTOINIT);

    /* Clear ISR by writing 0xFF (W1C) */
    ne2k_write_reg(0, NE2K_REG_ISR, 0xFF);

    /* Mask all interrupts (polled mode) */
    ne2k_write_reg(0, NE2K_REG_IMR, 0x00);

    /* RX ring: 0x42 .. 0x80 (wrap).  PSTART/PSTOP/BNRY. */
    ne2k_write_reg(0, NE2K_REG_PSTART, NE2K_RX_BUF_START);
    ne2k_write_reg(0, NE2K_REG_PSTOP,  NE2K_RX_BUF_END);
    ne2k_write_reg(0, NE2K_REG_BNRY,   NE2K_RX_BUF_START);

    /* TCR: normal transmit */
    ne2k_write_reg(0, NE2K_REG_TCR, 0x00);
    /* RCR: accept broadcast + matching physical + all-packets for
     *       promiscuous sniff.  Use broadcast + physical by default. */
    ne2k_write_reg(0, NE2K_REG_RCR,
                   NE2K_RCR_BROADCAST | NE2K_RCR_PHYSICAL);

    /* Read MAC and program it into PAR0..5 on page 1 */
    ne2k_read_mac();
    for (int i = 0; i < 6; i++) {
        ne2k_write_reg(1, 0x01 + i, ne2k_mac_addr[i]);
    }
    /* Clear multicast table */
    for (int i = 0; i < 8; i++) {
        ne2k_write_reg(1, 0x08 + i, 0x00);
    }

    /* Place CURR one page after the boundary, just past PSTART. */
    ne2k_curr = NE2K_RX_BUF_START + 1;
    ne2k_write_reg(1, NE2K_REG_CURR, ne2k_curr);
    ne2k_next = NE2K_RX_BUF_START;

    /* Start the chip (page 0) */
    ne2k_cmd(NE2K_CR_STA);
    for (volatile int i = 0; i < 1000; i++) { io_wait(); }

    /* Register the net interface.  We pick a fixed name ("ne0") and
     * a placeholder IPv4 configuration -- the user can re-configure
     * it with the `ifconfig` command at runtime. */
    memset(&ne2k_iface, 0, sizeof(ne2k_iface));
    strncpy(ne2k_iface.name, "ne0", sizeof(ne2k_iface.name) - 1);
    for (int i = 0; i < 6; i++) {
        ne2k_iface.mac.bytes[i] = ne2k_mac_addr[i];
    }
    ne2k_iface.up   = 1;
    ne2k_iface.mtu  = 1500;
    ne2k_iface.flags = IFF_UP | IFF_RUNNING | IFF_BROADCAST;
    ne2k_iface.send  = ne2k_send;
    ne2k_iface.driver_data = NULL;
    net_register_interface(&ne2k_iface);

    ne2k_up = 1;
    return 1;
}

/* PCI path: map the BAR, then init. */
static int ne2k_init_pci(uint8_t bus, uint8_t dev, uint8_t func) {
    /* BAR0 must be an I/O BAR (bit 0 set). */
    uint32_t bar0 = pci_read_config(bus, dev, func, 0x10);
    if ((bar0 & 0x01) == 0) {
        return 0;
    }
    uint16_t base = (uint16_t)(bar0 & 0xFFFFFFFC);
    if (base == 0) return 0;

    /* Enable bus mastering and I/O space. */
    uint32_t cmd = pci_read_config(bus, dev, func, 0x04);
    cmd |= (1u << 2) | (1u << 0);
    pci_write_config(bus, dev, func, 0x04, cmd);

    return ne2k_init_at(base);
}

/* ISA fallback: try a small list of classic bases. */
static int ne2k_init_isa(void) {
    for (int i = 0; i < NE2K_ISA_BASES_NUM; i++) {
        if (ne2k_init_at(ne2k_isa_bases[i])) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

int ne2k_probe(void) {
    /* Try PCI first. */
    for (uint16_t bus = 0; bus < PCI_MAX_BUSES; bus++) {
        for (uint8_t dev = 0; dev < PCI_MAX_DEVICES; dev++) {
            for (uint8_t func = 0; func < PCI_MAX_FUNCTIONS; func++) {
                uint32_t reg0 = pci_read_config(bus, dev, func, 0x00);
                if (reg0 == 0xFFFFFFFF) continue;
                if ((reg0 & 0xFFFF) != NE2K_PCI_VENDOR) continue;
                if (((reg0 >> 16) & 0xFFFF) != NE2K_PCI_DEVICE) continue;
                if (ne2k_init_pci((uint8_t)bus, (uint8_t)dev, func)) {
                    return 1;
                }
            }
        }
    }
    /* ISA fallback. */
    return ne2k_init_isa();
}

int ne2k_is_up(void) { return ne2k_up; }

const uint8_t *ne2k_mac(void) { return ne2k_mac_addr; }

/* ------------------------------------------------------------------ */
/* TX / RX                                                            */
/* ------------------------------------------------------------------ */

int ne2k_send(net_interface_t *iface, const void *buf, uint32_t len) {
    (void)iface;
    if (!ne2k_up || len == 0 || len > 1514) return -1;

    /* Copy the frame into the on-chip TX buffer.  We use a single
     * DMA write followed by a "send packet" command. */
    ne2k_dma_write(NE2K_TX_BUF_START * NE2K_PAGE_SIZE, buf, (uint16_t)len);

    /* Program TBCR0/1 (0x13/0x14) with the length. */
    ne2k_set_page(0);
    outb(ne2k_base + 0x05, 0x00);   /* NCR clear */
    outb(ne2k_base + 0x06, 0x00);   /* FIFO clear */
    outb(ne2k_base + 0x04, 0x00);   /* TSR clear */
    outb(ne2k_base + 0x03, 0x00);   /* BNRY -- not strictly required */
    outb(ne2k_base + 0x0D, 0x00);   /* TCR */
    outb(ne2k_base + 0x07, 0xFF);   /* ISR ack */
    outb(ne2k_base + 0x10, (uint8_t)(len & 0xFF));
    outb(ne2k_base + 0x11, (uint8_t)((len >> 8) & 0xFF));
    outb(ne2k_base + 0x12, NE2K_TX_BUF_START);

    /* Send packet command. */
    outb(ne2k_base + NE2K_REG_CR,
         NE2K_CR_STA | NE2K_CR_TXP | NE2K_CR_DMA_SEND);

    /* Poll ISR for TX completion. */
    uint32_t spin = 0;
    for (; spin < 100000; spin++) {
        uint8_t isr = inb(ne2k_base + NE2K_REG_ISR);
        if (isr & (NE2K_ISR_PTX | NE2K_ISR_TXE)) {
            outb(ne2k_base + NE2K_REG_ISR, isr);
            break;
        }
    }
    if (spin >= 100000) return -2;

    uint8_t tsr = inb(ne2k_base + NE2K_REG_TSR);
    if (!(tsr & 0x01)) {  /* PTX bit */
        return -3;
    }
    if (iface) {
        iface->tx_packets++;
        iface->tx_bytes += len;
    }
    return (int)len;
}

int ne2k_poll(void) {
    if (!ne2k_up) return 0;

    int frames = 0;
    while (1) {
        /* Stop when CURR == BNRY+1 (modulo ring): ring is empty. */
        ne2k_set_page(0);
        uint8_t bnd = inb(ne2k_base + NE2K_REG_BNRY);
        ne2k_set_page(1);
        uint8_t cur = inb(ne2k_base + NE2K_REG_CURR);
        if (cur == bnd) break;
        if (cur == ne2k_curr) {
            /* No progress: break to avoid spinning forever. */
            break;
        }
        ne2k_curr = cur;

        uint8_t hdr[4];
        ne2k_dma_read(ne2k_curr * NE2K_PAGE_SIZE, hdr, 4);
        uint8_t  status = hdr[0];
        uint8_t  nextpg = hdr[1];
        uint16_t flen   = ((uint16_t)hdr[3] << 8) | hdr[2];
        (void)status;
        if (flen == 0 || flen > 1518) {
            ne2k_write_reg(0, NE2K_REG_BNRY, ne2k_curr);
            continue;
        }
        net_buffer_t *buf = net_alloc_buffer();
        if (!buf) {
            ne2k_write_reg(0, NE2K_REG_BNRY,
                           (nextpg == NE2K_RX_BUF_START) ? (NE2K_RX_BUF_END - 1) : (nextpg - 1));
            ne2k_write_reg(1, NE2K_REG_CURR, nextpg);
            return frames;
        }
        if (flen > sizeof(buf->data)) flen = sizeof(buf->data);
        ne2k_dma_read(ne2k_curr * NE2K_PAGE_SIZE + 4, buf->data, flen);
        buf->len     = flen;
        buf->offset  = 0;
        buf->iface   = &ne2k_iface;
        buf->next    = NULL;
        ne2k_iface.rx_packets++;
        ne2k_iface.rx_bytes += flen;
        net_receive(buf);
        frames++;

        ne2k_write_reg(0, NE2K_REG_BNRY,
                       (nextpg == NE2K_RX_BUF_START) ? (NE2K_RX_BUF_END - 1) : (nextpg - 1));
        ne2k_write_reg(1, NE2K_REG_CURR, nextpg);
    }
    /* Acknowledge RX-related ISR bits. */
    ne2k_set_page(0);
    outb(ne2k_base + NE2K_REG_ISR, NE2K_ISR_PRX | NE2K_ISR_RXE | NE2K_ISR_OVW);
    return frames;
}
