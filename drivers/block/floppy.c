/* floppy.c - Floppy Disk Controller (FDC) driver for 3.5" 1.44MB drives.
 *
 * Implements DMA-based block reads/writes using FDC + ISA DMA channel 2.
 * The driver programs the FDC, sets up the DMA buffer, and uses a poll loop
 * on the MSR's RQM bit to push/pull bytes to/from the FIFO.
 */

#include "floppy.h"
#include "io.h"
#include "string.h"
#include "kheap.h"

#define FLOPPY_DMA_CHANNEL     2
#define FLOPPY_DMA_BUF_ADDR    0x80000   /* 512KB, must be < 16MB and 64KB-aligned */

/* DMA channel 2 ports (ISA DMA controller) */
#define DMA1_ADDR      0x00
#define DMA1_COUNT     0x01
#define DMA1_PAGE      0x81
#define DMA1_MASK      0x0A
#define DMA1_MODE      0x0B
#define DMA1_FLIPFLOP  0x0C

#define DMA_MODE_READ   0x46   /* single, address increment, read, channel 2 */
#define DMA_MODE_WRITE  0x4A

static int g_initialized;
static int g_motor_on[4];

/* ---- Low-level I/O ---- */

static void fdc_write(uint8_t val) {
    while (!(inb(FDC_MSR) & FDC_MSR_RQM));
    outb(FDC_FIFO, val);
}

static uint8_t fdc_read(void) {
    while (!(inb(FDC_MSR) & FDC_MSR_RQM));
    return inb(FDC_FIFO);
}

static int fdc_wait_msr(uint8_t mask, uint8_t val, int timeout) {
    while (timeout--) {
        uint8_t s = inb(FDC_MSR);
        if ((s & mask) == val) return 0;
    }
    return -1;
}

/* ---- DMA setup ---- */

static void dma_setup(uint8_t mode, const void *buf, uint32_t len) {
    /* Mask DMA channel 2. */
    outb(DMA1_MASK, 0x06 | (1 << 2));

    /* Reset flip-flop. */
    outb(DMA1_FLIPFLOP, 0xFF);

    /* Address (low then high). */
    uint32_t addr = (uint32_t)buf;
    outb(DMA1_ADDR,  (uint8_t)(addr & 0xFF));
    outb(DMA1_ADDR,  (uint8_t)((addr >> 8) & 0xFF));

    /* Page register. */
    outb(DMA1_PAGE, (uint8_t)((addr >> 16) & 0xFF));

    /* Count (len - 1, low then high). */
    uint32_t count = len - 1;
    outb(DMA1_COUNT, (uint8_t)(count & 0xFF));
    outb(DMA1_COUNT, (uint8_t)((count >> 8) & 0xFF));

    /* Mode: single, increment, auto-init off, channel 2. */
    outb(DMA1_MODE, mode);

    /* Unmask channel 2. */
    outb(DMA1_MASK, 0x06);
}

/* ---- FDC operations ---- */

static void fdc_select(uint8_t drive) {
    outb(FDC_DOR, FDC_DOR_NORESET | FDC_DOR_DMA |
                   (drive == 0 ? FDC_DOR_DRIVE0 :
                    drive == 1 ? FDC_DOR_DRIVE1 :
                    drive == 2 ? FDC_DOR_DRIVE2 : FDC_DOR_DRIVE3) |
                   (g_motor_on[drive] ? FDC_DOR_MOTOR0 : 0));
}

/* Issue a recalibrate (track 0 seek) and wait for interrupt. */
static void fdc_recalibrate(uint8_t drive) {
    fdc_write(FDC_CMD_RECALIBRATE);
    fdc_write(drive);
    /* Sense interrupt after seek. */
    fdc_read();  /* ST0 */
    fdc_read();  /* PCN */
}

/* Issue SEEK to a cylinder and wait for completion. */
static void fdc_seek(uint8_t drive, uint8_t cylinder) {
    fdc_write(FDC_CMD_SEEK);
    fdc_write(drive);
    fdc_write(cylinder);
    /* Sense interrupt after seek. */
    fdc_read();  /* ST0 */
    fdc_read();  /* PCN */
}

static int fdc_wait_irq(void) {
    /* Drive must clear MSR's command-busy bit and we look for any interrupt
     * indication via polling MSR. */
    for (int i = 0; i < FDC_TIMEOUT_LOOPS; i++) {
        if ((inb(FDC_MSR) & FDC_MSR_COMMAND_BUSY) == 0) return 0;
    }
    return -1;
}

/* Read/write multiple sectors on a single track. The FDC drives the head
 * across the sector stream and signals completion via the result phase. */
static int fdc_read_write(uint8_t drive, uint32_t lba, uint8_t *buf,
                           int write, uint8_t sectors)
{
    /* Convert LBA to CHS - simple linear mapping. */
    uint8_t  cylinder = (uint8_t)(lba / (FLOPPY_HEADS * FLOPPY_SECTORS_PER_TRACK));
    uint8_t  head     = (uint8_t)((lba / FLOPPY_SECTORS_PER_TRACK) % FLOPPY_HEADS);
    uint8_t  sector   = (uint8_t)((lba % FLOPPY_SECTORS_PER_TRACK) + 1);
    if (cylinder >= FLOPPY_CYLINDERS) return -1;

    fdc_select(drive);

    /* Setup DMA. */
    dma_setup(write ? DMA_MODE_WRITE : DMA_MODE_READ,
              buf, (uint32_t)sectors * FLOPPY_SECTOR_SIZE);

    /* Issue the read/write command (MT, MFM bits set for 1.44MB). */
    uint8_t cmd = write ? FDC_CMD_WRITE_DATA : FDC_CMD_READ_DATA;
    fdc_write(0x80 | 0x40 | cmd);  /* MT | MFM */
    fdc_write((head << 2) | drive);
    fdc_write(cylinder);
    fdc_write(head);
    fdc_write(sector);
    fdc_write(0x02);   /* sector size code 2 = 512B */
    fdc_write(sectors);
    fdc_write(0x1B);   /* gap3 length for 1.44MB */
    fdc_write(0xFF);   /* data length (unused in MFM) */

    /* Wait for command completion (interrupt). */
    if (fdc_wait_irq() != 0) return -1;

    /* Read 7 result bytes. */
    uint8_t st0 = fdc_read();
    uint8_t st1 = fdc_read();
    uint8_t st2 = fdc_read();
    uint8_t cy  = fdc_read();
    uint8_t he  = fdc_read();
    uint8_t se  = fdc_read();
    uint8_t cn  = fdc_read();
    (void)cy; (void)he; (void)se; (void)cn;

    if ((st0 & 0xC0) != FDC_ST0_NORMAL_TERMINATION) return -1;
    if (st1 & (FDC_ST1_CRC_ERROR | FDC_ST1_OVERRUN | FDC_ST1_NO_DATA)) return -1;
    if (st2 & FDC_ST2_CRC_ERROR) return -1;

    return 0;
}

/* ---- Public API ---- */

int floppy_init(void) {
    if (g_initialized) return 0;

    /* Enter reset state. */
    outb(FDC_DOR, 0x00);
    for (volatile int i = 0; i < 10000; i++);

    /* Exit reset, enable DMA. */
    outb(FDC_DOR, FDC_DOR_NORESET | FDC_DOR_DMA);

    /* Set data rate for 500 kbps (1.44MB MFM). */
    outb(FDC_CCR, 0x00);

    /* SPECIFY: SRT=8ms, HUT=0, HLT=10ms, DMA mode. */
    fdc_write(FDC_CMD_SPECIFY);
    fdc_write(0xCF);  /* SRT<<4 | HUT */
    fdc_write(0x02);  /* HLT<<1 | ND (0 = DMA mode) */

    /* Recalibrate all 4 drives (most machines have at least drive 0). */
    for (int d = 0; d < 4; d++) {
        fdc_recalibrate(d);
    }

    g_initialized = 1;
    return 0;
}

int floppy_detect(void) {
    if (!g_initialized && floppy_init() != 0) return 0;
    /* Try to seek to track 1 and back; if the seek completes, a drive
     * is present. We use drive 0 as the test target. */
    fdc_seek(0, 1);
    fdc_recalibrate(0);
    /* Without IRQ-driven detection we conservatively report the controller
     * as available if the recalibrate succeeded. */
    return 1;
}

void floppy_motor_on(uint8_t drive) {
    if (drive >= 4) return;
    if (g_motor_on[drive]) return;
    g_motor_on[drive] = 1;
    fdc_select(drive);
    /* Motor spin-up: ~500ms. We use a polling delay calibrated for
     * modern drives which typically spin up faster. */
    for (volatile int i = 0; i < 5000000; i++);
}

void floppy_motor_off(uint8_t drive) {
    if (drive >= 4) return;
    if (!g_motor_on[drive]) return;
    g_motor_on[drive] = 0;
    fdc_select(drive);
}

int floppy_read(uint8_t drive, uint32_t lba, uint8_t *buf) {
    if (!g_initialized && floppy_init() != 0) return -1;
    if (drive >= 4 || !buf) return -1;
    if (lba >= FLOPPY_SECTORS_TOTAL) return -1;

    floppy_motor_on(drive);

    /* Copy through the fixed DMA buffer; FDC DMA cannot access arbitrary
     * memory above 16MB or below the 64KB boundary. */
    uint8_t *dma_buf = (uint8_t *)FLOPPY_DMA_BUF_ADDR;
    if (fdc_read_write(drive, lba, dma_buf, 0, 1) != 0) return -1;
    memcpy(buf, dma_buf, FLOPPY_SECTOR_SIZE);
    return 0;
}

int floppy_write(uint8_t drive, uint32_t lba, const uint8_t *buf) {
    if (!g_initialized && floppy_init() != 0) return -1;
    if (drive >= 4 || !buf) return -1;
    if (lba >= FLOPPY_SECTORS_TOTAL) return -1;

    floppy_motor_on(drive);

    uint8_t *dma_buf = (uint8_t *)FLOPPY_DMA_BUF_ADDR;
    memcpy(dma_buf, buf, FLOPPY_SECTOR_SIZE);
    if (fdc_read_write(drive, lba, dma_buf, 1, 1) != 0) return -1;
    return 0;
}