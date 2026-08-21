#ifndef FLOPPY_H
#define FLOPPY_H

#include "stdint.h"

/* Floppy Disk Controller (FDC) driver for standard 3.5" 1.44MB floppy drives.
 *
 * The driver is fully poll-based: it programs the FDC, issues commands, and
 * waits for the controller's interrupt status register to indicate completion.
 * DMA channel 2 is used for data transfer.
 *
 * Geometry (default 3.5" HD):
 *   80 cylinders * 2 heads * 18 sectors/track * 512 bytes/sector = 1.44 MB
 */

#define FLOPPY_SECTOR_SIZE   512
#define FLOPPY_SECTORS_PER_TRACK 18
#define FLOPPY_HEADS         2
#define FLOPPY_CYLINDERS     80
#define FLOPPY_SECTORS_TOTAL (FLOPPY_CYLINDERS * FLOPPY_HEADS * FLOPPY_SECTORS_PER_TRACK)

/* FDC register ports (primary controller) */
#define FDC_DOR   0x3F2   /* Digital Output Register */
#define FDC_MSR   0x3F4   /* Main Status Register */
#define FDC_FIFO  0x3F5   /* Data register (FIFO) */
#define FDC_CCR   0x3F7   /* Config Control Register */

/* DOR bits */
#define FDC_DOR_DRIVE0   (0 << 0)
#define FDC_DOR_DRIVE1   (1 << 0)
#define FDC_DOR_DRIVE2   (2 << 0)
#define FDC_DOR_DRIVE3   (3 << 0)
#define FDC_DOR_RESET    (0 << 2)
#define FDC_DOR_NORESET  (1 << 2)
#define FDC_DOR_DMA      (1 << 3)
#define FDC_DOR_MOTOR0   (1 << 4)

/* MSR bits */
#define FDC_MSR_DRIVE0_BUSY   (1 << 0)
#define FDC_MSR_DRIVE1_BUSY   (1 << 1)
#define FDC_MSR_DRIVE2_BUSY   (1 << 2)
#define FDC_MSR_DRIVE3_BUSY   (1 << 3)
#define FDC_MSR_COMMAND_BUSY  (1 << 4)
#define FDC_MSR_NON_DMA       (1 << 5)
#define FDC_MSR_DIO            (1 << 6)
#define FDC_MSR_RQM            (1 << 7)

/* FDC commands (top 3 bits, lower bits are arg count) */
#define FDC_CMD_READ_DATA         0x06
#define FDC_CMD_WRITE_DATA        0x05
#define FDC_CMD_RECALIBRATE       0x07
#define FDC_CMD_SENSE_INT         0x08
#define FDC_CMD_SPECIFY           0x03
#define FDC_CMD_SEEK              0x0F
#define FDC_CMD_VERSION           0x10
#define FDC_CMD_DUMPREG           0x0E

/* Status register 0 bits (returned by SENSE INTERRUPT) */
#define FDC_ST0_NORMAL_TERMINATION 0x00
#define FDC_ST0_ABNORMAL_TERMINATION 0x40
#define FDC_ST0_INVALID_COMMAND   0x80
#define FDC_ST0_DRIVE_NOT_READY   0xC0

/* Status register 1 bits */
#define FDC_ST1_MISSING_ADDRESS_MARK 0x01
#define FDC_ST1_NOT_WRITABLE         0x02
#define FDC_ST1_NO_DATA              0x04
#define FDC_ST1_OVERRUN              0x10
#define FDC_ST1_CRC_ERROR            0x20
#define FDC_ST1_END_OF_CYLINDER      0x80

/* Status register 2 bits */
#define FDC_ST2_NO_DATA              0x01
#define FDC_ST2_BAD_CYLINDER         0x02
#define FDC_ST2_CRC_ERROR            0x20
#define FDC_ST2_DELETED_DATA         0x40

/* Wait loop iterations */
#define FDC_TIMEOUT_LOOPS 1000000

/* Public API */
int floppy_init(void);
int floppy_read(uint8_t drive, uint32_t lba, uint8_t *buf);
int floppy_write(uint8_t drive, uint32_t lba, const uint8_t *buf);
int floppy_detect(void);
void floppy_motor_on(uint8_t drive);
void floppy_motor_off(uint8_t drive);

#endif