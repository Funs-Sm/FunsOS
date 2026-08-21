#ifndef SD_MMC_H
#define SD_MMC_H

#include "stdint.h"

/* SD/MMC card driver - implements SD card protocol over SPI or native SD bus.
 * Provides block-level read/write for SDSC, SDHC and SDXC cards.
 *
 * Reference: SD Specifications Part 1 (Physical Layer) Simplified Spec.
 */

/* Card types */
#define SD_TYPE_UNKNOWN  0
#define SD_TYPE_MMC      1
#define SD_TYPE_SDSC     2   /* Standard Capacity (up to 2GB) */
#define SD_TYPE_SDHC     3   /* High Capacity (up to 32GB) */
#define SD_TYPE_SDXC     4   /* Extended Capacity (up to 2TB) */

/* OCR bits */
#define SD_OCR_CCS       (1 << 30)   /* Card Capacity Status (HC/XC) */
#define SD_OCR_BUSY      (1 << 31)   /* Busy bit */

/* SPI command tokens (over SPI bus) */
#define SD_CMD0_GO_IDLE_STATE        0
#define SD_CMD1_SEND_OP_COND         1
#define SD_CMD8_SEND_IF_COND         8
#define SD_CMD9_SEND_CSD             9
#define SD_CMD10_SEND_CID            10
#define SD_CMD16_SET_BLOCKLEN        16
#define SD_CMD17_READ_SINGLE_BLOCK   17
#define SD_CMD18_READ_MULTIPLE_BLOCK 18
#define SD_CMD24_WRITE_BLOCK         24
#define SD_CMD25_WRITE_MULTIPLE_BLOCK 25
#define SD_CMD55_APP_CMD             55
#define SD_CMD58_READ_OCR            58
#define SD_ACMD41_SD_SEND_OP_COND    41

/* R1 response bits */
#define SD_R1_IDLE       0x01
#define SD_R1_ERASE_RST  0x02
#define SD_R1_ILLEGAL    0x04
#define SD_R1_CRC_ERR    0x08
#define SD_R1_ERASE_SEQ  0x10
#define SD_R1_ADDR_ERR   0x20
#define SD_R1_PARAM_ERR  0x40

/* Data tokens */
#define SD_TOKEN_SINGLE_READ    0xFE
#define SD_TOKEN_MULTI_READ     0xFE
#define SD_TOKEN_SINGLE_WRITE   0xFE
#define SD_TOKEN_MULTI_WRITE    0xFC
#define SD_TOKEN_STOP_MULTI     0xFD

/* Data response tokens */
#define SD_DATA_ACCEPTED  0x05
#define SD_DATA_CRC_ERR   0x0B
#define SD_DATA_WRITE_ERR 0x0E

/* Block size */
#define SD_BLOCK_SIZE     512

/* Maximum number of cards supported per bus */
#define SD_MAX_CARDS      4

/* Card information structure */
typedef struct {
    uint8_t  type;           /* SD_TYPE_* */
    uint8_t  spi_mode;       /* 1 if communicating over SPI */
    uint32_t capacity;       /* Capacity in 512-byte blocks */
    uint32_t ocr;          /* Operation conditions register */
    uint8_t  cid[16];        /* Card Identification */
    uint8_t  csd[16];        /* Card Specific Data */
    uint8_t  high_capacity;  /* 1 for SDHC/XC */
    uint32_t block_size;     /* Typically 512 */
} sd_card_t;

/* Bus-level interface (implemented by SPI driver or native SD driver) */
typedef struct {
    int  (*init)(void);
    int  (*send_cmd)(uint8_t cmd, uint32_t arg, uint8_t crc, uint8_t *resp, uint32_t resp_len);
    int  (*read)(uint8_t *buf, uint32_t len);
    int  (*write)(const uint8_t *buf, uint32_t len);
    int  (*set_speed)(uint32_t hz);
    int  (*select)(int card_index);   /* CS low for given card */
    void (*deselect)(int card_index);
    uint32_t max_speed_hz;
} sd_bus_ops_t;

int sd_mmc_init(const sd_bus_ops_t *ops);
int sd_mmc_get_card_count(void);
const sd_card_t *sd_mmc_get_card(int idx);
int sd_mmc_read_blocks(int card_idx, uint32_t lba, uint32_t count, void *buf);
int sd_mmc_write_blocks(int card_idx, uint32_t lba, uint32_t count, const void *buf);

#endif