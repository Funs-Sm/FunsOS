/* sd_mmc.c - SD/MMC card driver (SPI mode)
 *
 * Implements the SD protocol over an SPI bus, supporting SDSC, SDHC and SDXC
 * cards. The driver is responsible for card identification, capacity detection,
 * and single/multi-block read/write operations.
 *
 * The driver uses a pluggable bus interface (sd_bus_ops_t) so it can run over
 * any SPI controller that exposes the appropriate send/read primitives.
 */

#include "sd_mmc.h"
#include "string.h"

#define SD_DEBUG 0

#if SD_DEBUG
#define sd_dbg(...) /* enable for diagnostic prints */
#else
#define sd_dbg(...)
#endif

static const sd_bus_ops_t *g_bus;
static sd_card_t g_cards[SD_MAX_CARDS];
static int g_card_count;
static int g_inited;

/* ---- Low-level helpers ---- */

static int sd_transmit(uint8_t tok) {
    if (g_bus->write(&tok, 1) != 0) return -1;
    return 0;
}

/* Wait until the card returns a non-0xFF byte (indicating response ready).
 * Returns the received byte or -1 on timeout. */
static int sd_wait_response(uint32_t timeout_iters) {
    uint8_t b = 0xFF;
    while (timeout_iters--) {
        if (g_bus->read(&b, 1) != 0) return -1;
        if (b != 0xFF) return (int)b;
    }
    return -1;
}

/* Skip junk bytes after a CMD0 / ACMD41 retry sequence.
 * Some cards insert 1-2 0xFF bytes after going idle. */
static void sd_skip_junk(void) {
    uint8_t b;
    for (int i = 0; i < 8; i++) {
        if (g_bus->read(&b, 1) != 0) return;
        if (b != 0xFF) {
            /* Put it back by re-reading it from a dummy variable... we can't
             * un-read, so just continue. Caller must use 8-cycle tolerance. */
            return;
        }
    }
}

/* ---- SPI command transport ---- */

static int sd_send_cmd_spi(uint8_t cmd, uint32_t arg, uint8_t crc,
                            uint8_t *resp, uint32_t resp_len)
{
    /* Packet format: 0x40|cmd (1B), arg (4B BE), crc (1B) */
    uint8_t pkt[6];
    pkt[0] = (uint8_t)(0x40 | cmd);
    pkt[1] = (uint8_t)(arg >> 24);
    pkt[2] = (uint8_t)(arg >> 16);
    pkt[3] = (uint8_t)(arg >> 8);
    pkt[4] = (uint8_t)(arg);
    pkt[5] = crc;

    if (g_bus->write(pkt, 6) != 0) return -1;

    /* Wait for response byte (R1) - up to 8 cycles for slow cards. */
    int r = sd_wait_response(8);
    if (r < 0) return -1;

    if (resp && resp_len > 0) {
        resp[0] = (uint8_t)r;
        for (uint32_t i = 1; i < resp_len; i++) {
            if (g_bus->read(&resp[i], 1) != 0) return -1;
        }
    }
    return 0;
}

/* ---- Card identification ---- */

static int sd_read_ocr(uint32_t *ocr_out) {
    uint8_t resp[5];
    if (sd_send_cmd_spi(SD_CMD58_READ_OCR, 0, 0xFF, resp, 5) != 0) return -1;
    if (resp[0] != 0) return -1;
    *ocr_out = ((uint32_t)resp[1] << 24) |
               ((uint32_t)resp[2] << 16) |
               ((uint32_t)resp[3] << 8)  |
               (uint32_t)resp[4];
    return 0;
}

/* Send ACMD<n>: prefix with CMD55. */
static int sd_send_acmd_spi(uint8_t cmd, uint32_t arg, uint8_t crc,
                             uint8_t *resp, uint32_t resp_len)
{
    uint8_t r1[1];
    if (sd_send_cmd_spi(SD_CMD55_APP_CMD, 0, 0xFF, r1, 1) != 0) return -1;
    if (r1[0] != 0 && !(r1[0] & SD_R1_IDLE)) return -1;
    return sd_send_cmd_spi(cmd, arg, crc, resp, resp_len);
}

static int sd_read_cid(sd_card_t *card) {
    uint8_t resp[17];
    if (sd_send_cmd_spi(SD_CMD10_SEND_CID, 0, 0xFF, resp, 17) != 0) return -1;
    if (resp[0] != 0) return -1;
    if (resp[1] != SD_TOKEN_SINGLE_READ) return -1;
    memcpy(card->cid, &resp[2], 16);
    return 0;
}

static int sd_read_csd(sd_card_t *card) {
    uint8_t resp[17];
    if (sd_send_cmd_spi(SD_CMD9_SEND_CSD, 0, 0xFF, resp, 17) != 0) return -1;
    if (resp[0] != 0) return -1;
    if (resp[1] != SD_TOKEN_SINGLE_READ) return -1;
    memcpy(card->csd, &resp[2], 16);

    /* CSD version 1.0 (SDSC) parses C_SIZE differently from v2.0 (SDHC).
     * Bit 126 of CSD selects version. */
    uint8_t csd_structure = (card->csd[0] >> 6) & 0x03;
    if (csd_structure == 0) {
        /* CSD v1.0 - SDSC card */
        uint32_t c_size = ((uint32_t)(card->csd[6] & 0x03) << 10) |
                          ((uint32_t)card->csd[7] << 2) |
                          ((uint32_t)(card->csd[8] >> 6) & 0x03);
        uint8_t c_size_mult = ((card->csd[9] & 0x03) << 1) |
                              ((card->csd[10] >> 7) & 0x01);
        uint8_t read_bl_len = card->csd[5] & 0x0F;
        uint32_t block_len = 1u << read_bl_len;
        uint32_t mult = 1u << (c_size_mult + 2);
        card->capacity = (c_size + 1) * mult * block_len / SD_BLOCK_SIZE;
        card->high_capacity = 0;
    } else if (csd_structure == 1) {
        /* CSD v2.0 - SDHC/SDXC card */
        uint32_t c_size = ((uint32_t)(card->csd[7] & 0x3F) << 16) |
                          ((uint32_t)card->csd[8] << 8) |
                          (uint32_t)card->csd[9];
        card->capacity = (c_size + 1) * 1024;  /* already in 512B blocks */
        card->high_capacity = 1;
    } else {
        return -1;
    }
    if (card->capacity == 0) return -1;
    return 0;
}

/* Run the initialization sequence for one card. Returns 0 on success. */
static int sd_init_card(int card_index) {
    sd_card_t *card = &g_cards[g_card_count];
    memset(card, 0, sizeof(*card));
    card->block_size = SD_BLOCK_SIZE;

    /* Toggle CS high/low several times to force the card into SPI mode. */
    g_bus->deselect(card_index);
    for (int i = 0; i < 5; i++) {
        if (sd_transmit(0xFF) != 0) return -1;
    }

    g_bus->select(card_index);

    /* CMD0: GO_IDLE_STATE - resets card and enters SPI mode. */
    uint8_t r1;
    if (sd_send_cmd_spi(SD_CMD0_GO_IDLE_STATE, 0, 0x95, &r1, 1) != 0) return -1;
    if (r1 != SD_R1_IDLE) return -1;

    /* CMD8: SEND_IF_COND - check voltage range and pattern.
     * Required for SD v2.0+ cards; older cards will fail this and
     * fall back to MMC initialization. */
    uint8_t resp[5];
    if (sd_send_cmd_spi(SD_CMD8_SEND_IF_COND, 0x000001AA, 0x87, resp, 5) == 0) {
        if (resp[0] != SD_R1_IDLE) return -1;
        uint32_t r7 = ((uint32_t)resp[1] << 24) | ((uint32_t)resp[2] << 16) |
                     ((uint32_t)resp[3] << 8) | (uint32_t)resp[4];
        if ((r7 & 0xFFFF) != 0x01AA) return -1;  /* voltage mismatch */

        /* SD v2.0+ card: initialize with ACMD41 (HCS=1 to indicate HC support). */
        int retries = 1000;
        while (retries--) {
            if (sd_send_acmd_spi(SD_ACMD41_SD_SEND_OP_COND, 0x40000000, 0xFF,
                                  resp, 1) != 0)
                return -1;
            if (resp[0] == 0) break;
            if (retries == 0) return -1;
        }

        uint32_t ocr;
        if (sd_read_ocr(&ocr) != 0) return -1;
        card->ocr = ocr;
        card->high_capacity = (ocr & SD_OCR_CCS) ? 1 : 0;
        card->type = card->high_capacity ? SD_TYPE_SDHC : SD_TYPE_SDSC;
    } else {
        /* MMC card (or SD v1.0). Initialize with CMD1. */
        int retries = 1000;
        while (retries--) {
            if (sd_send_cmd_spi(SD_CMD1_SEND_OP_COND, 0, 0xFF, resp, 1) != 0)
                return -1;
            if (resp[0] == 0) break;
            if (retries == 0) return -1;
        }
        card->type = SD_TYPE_MMC;
        card->high_capacity = 0;
    }

    /* CMD16: SET_BLOCKLEN - set block size to 512 bytes.
     * SDHC/SDXC cards are fixed at 512B, but issuing this is harmless. */
    if (sd_send_cmd_spi(SD_CMD16_SET_BLOCKLEN, SD_BLOCK_SIZE, 0xFF, &r1, 1) != 0)
        return -1;
    if (r1 != 0) return -1;

    /* Read CID & CSD to populate capacity and identification. */
    if (sd_read_cid(card) != 0) return -1;
    if (sd_read_csd(card) != 0) return -1;

    g_bus->deselect(card_index);
    g_card_count++;
    return 0;
}

/* ---- Public API ---- */

int sd_mmc_init(const sd_bus_ops_t *ops) {
    if (!ops) return -1;
    g_bus = ops;
    g_card_count = 0;
    g_inited = 1;
    memset(g_cards, 0, sizeof(g_cards));

    if (ops->init && ops->init() != 0) return -1;
    if (ops->set_speed) ops->set_speed(100000);  /* init at 100kHz */

    /* Try up to SD_MAX_CARDS card indices. */
    for (int i = 0; i < SD_MAX_CARDS; i++) {
        if (sd_init_card(i) == 0) {
            /* Switch to a faster SPI clock after successful init. */
            if (ops->set_speed) ops->set_speed(ops->max_speed_hz);
        }
    }

    if (g_card_count == 0) return -1;
    return 0;
}

int sd_mmc_get_card_count(void) {
    return g_inited ? g_card_count : 0;
}

const sd_card_t *sd_mmc_get_card(int idx) {
    if (!g_inited || idx < 0 || idx >= g_card_count) return (const sd_card_t *)0;
    return &g_cards[idx];
}

int sd_mmc_read_blocks(int card_idx, uint32_t lba, uint32_t count, void *buf) {
    if (!g_inited) return -1;
    if (card_idx < 0 || card_idx >= g_card_count) return -1;
    if (!buf || count == 0) return -1;

    const sd_card_t *card = &g_cards[card_idx];
    uint8_t *p = (uint8_t *)buf;
    uint32_t arg;

    if (card->high_capacity) {
        arg = lba;  /* SDHC uses block address directly */
    } else {
        arg = lba * SD_BLOCK_SIZE;  /* SDSC uses byte address */
    }

    if (count == 1) {
        /* Single block read. */
        g_bus->select(card_idx);
        uint8_t r1;
        if (sd_send_cmd_spi(SD_CMD17_READ_SINGLE_BLOCK, arg, 0xFF, &r1, 1) != 0
            || r1 != 0) {
            g_bus->deselect(card_idx);
            return -1;
        }
        int tok = sd_wait_response(100000);
        if (tok != SD_TOKEN_SINGLE_READ) {
            g_bus->deselect(card_idx);
            return -1;
        }
        if (g_bus->read(p, SD_BLOCK_SIZE) != 0) {
            g_bus->deselect(card_idx);
            return -1;
        }
        /* Discard 2-byte CRC. */
        uint8_t crc[2];
        g_bus->read(crc, 2);
        g_bus->deselect(card_idx);
        return 0;
    }

    /* Multi-block read. */
    g_bus->select(card_idx);
    uint8_t r1;
    if (sd_send_cmd_spi(SD_CMD18_READ_MULTIPLE_BLOCK, arg, 0xFF, &r1, 1) != 0
        || r1 != 0) {
        g_bus->deselect(card_idx);
        return -1;
    }
    for (uint32_t i = 0; i < count; i++) {
        int tok = sd_wait_response(100000);
        if (tok != SD_TOKEN_SINGLE_READ) {
            g_bus->deselect(card_idx);
            return -1;
        }
        if (g_bus->read(p + i * SD_BLOCK_SIZE, SD_BLOCK_SIZE) != 0) {
            g_bus->deselect(card_idx);
            return -1;
        }
        uint8_t crc[2];
        g_bus->read(crc, 2);
    }
    /* CMD12: STOP_TRANSMISSION. */
    sd_send_cmd_spi(0x0C, 0, 0xFF, &r1, 1);
    /* Card needs 1 byte clock after CMD12. */
    uint8_t d;
    g_bus->read(&d, 1);
    g_bus->deselect(card_idx);
    return 0;
}

int sd_mmc_write_blocks(int card_idx, uint32_t lba, uint32_t count, const void *buf) {
    if (!g_inited) return -1;
    if (card_idx < 0 || card_idx >= g_card_count) return -1;
    if (!buf || count == 0) return -1;

    const sd_card_t *card = &g_cards[card_idx];
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t arg;

    if (card->high_capacity) {
        arg = lba;
    } else {
        arg = lba * SD_BLOCK_SIZE;
    }

    if (count == 1) {
        g_bus->select(card_idx);
        uint8_t r1;
        if (sd_send_cmd_spi(SD_CMD24_WRITE_BLOCK, arg, 0xFF, &r1, 1) != 0
            || r1 != 0) {
            g_bus->deselect(card_idx);
            return -1;
        }
        /* Send start token. */
        uint8_t tok = SD_TOKEN_SINGLE_WRITE;
        if (g_bus->write(&tok, 1) != 0) {
            g_bus->deselect(card_idx);
            return -1;
        }
        if (g_bus->write(p, SD_BLOCK_SIZE) != 0) {
            g_bus->deselect(card_idx);
            return -1;
        }
        /* Dummy CRC. */
        uint8_t crc[2] = {0xFF, 0xFF};
        g_bus->write(crc, 2);
        /* Read data response token. */
        int dr = sd_wait_response(100000);
        if (dr != SD_DATA_ACCEPTED) {
            g_bus->deselect(card_idx);
            return -1;
        }
        /* Wait for card to finish programming (busy signaling 0x00). */
        uint8_t busy;
        do {
            if (g_bus->read(&busy, 1) != 0) break;
        } while (busy != 0xFF);
        g_bus->deselect(card_idx);
        return 0;
    }

    /* Multi-block write. */
    g_bus->select(card_idx);
    uint8_t r1;
    if (sd_send_cmd_spi(SD_CMD25_WRITE_MULTIPLE_BLOCK, arg, 0xFF, &r1, 1) != 0
        || r1 != 0) {
        g_bus->deselect(card_idx);
        return -1;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint8_t tok = SD_TOKEN_MULTI_WRITE;
        if (g_bus->write(&tok, 1) != 0) {
            g_bus->deselect(card_idx);
            return -1;
        }
        if (g_bus->write(p + i * SD_BLOCK_SIZE, SD_BLOCK_SIZE) != 0) {
            g_bus->deselect(card_idx);
            return -1;
        }
        uint8_t crc[2] = {0xFF, 0xFF};
        g_bus->write(crc, 2);
        int dr = sd_wait_response(100000);
        if (dr != SD_DATA_ACCEPTED) {
            g_bus->deselect(card_idx);
            return -1;
        }
        uint8_t busy;
        do {
            if (g_bus->read(&busy, 1) != 0) break;
        } while (busy != 0xFF);
    }
    /* Send stop token. */
    uint8_t stop = SD_TOKEN_STOP_MULTI;
    g_bus->write(&stop, 1);
    uint8_t busy;
    do {
        if (g_bus->read(&busy, 1) != 0) break;
    } while (busy != 0xFF);
    g_bus->deselect(card_idx);
    return 0;
}