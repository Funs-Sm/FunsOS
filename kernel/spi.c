#include "spi.h"
#include "klog.h"
#include "string.h"

struct spi_global {
    uint8_t initialized;
    spi_controller_t controllers[SPI_MAX_CONTROLLERS];
    uint32_t n_controllers;
    spi_controller_t *ctlr_list;
    uint64_t total_xfers;
    uint64_t total_bytes;
    uint64_t total_errors;
};

static struct spi_global spi_data;

static int virt_spi_transfer_one(spi_controller_t *ctlr, spi_device_t *spi,
                                 struct spi_transfer *xfer) {
    (void)ctlr; (void)spi;
    if (!xfer) return -1;
    if (xfer->tx_buf && xfer->rx_buf && xfer->len > 0) {
        memcpy(xfer->rx_buf, xfer->tx_buf, xfer->len > 64 ? 64 : xfer->len);
    }
    return 0;
}

static int virt_spi_setup(spi_device_t *spi) {
    if (!spi) return -1;
    if (spi->max_speed_hz == 0) spi->max_speed_hz = 10000000;
    if (spi->bits_per_word == 0) spi->bits_per_word = 8;
    return 0;
}

static int virt_spi_prepare_message(spi_controller_t *ctlr, spi_message_t *msg) {
    (void)ctlr; (void)msg;
    return 0;
}

static int virt_spi_unprepare_message(spi_controller_t *ctlr, spi_message_t *msg) {
    (void)ctlr; (void)msg;
    return 0;
}

static void spi_init_virtual_controller(spi_controller_t *ctlr, uint32_t bus_num) {
    memset(ctlr, 0, sizeof(*ctlr));
    strncpy(ctlr->name, "virt_spi", SPI_NAME_LEN - 1);
    ctlr->bus_num = bus_num;
    ctlr->num_chipselect = 4;
    ctlr->min_speed_hz = 100000;
    ctlr->max_speed_hz = 50000000;
    ctlr->transfer_one = virt_spi_transfer_one;
    ctlr->setup = virt_spi_setup;
    ctlr->prepare_message = virt_spi_prepare_message;
    ctlr->unprepare_message = virt_spi_unprepare_message;

    static const struct {
        const char *name;
        uint32_t cs;
        uint32_t max_speed;
        uint32_t mode;
    } virt_devs[] = {
        { "spi_flash", 0, 20000000, SPI_MODE_0 },
        { "spi_nor", 1, 50000000, SPI_MODE_0 },
        { "adc", 2, 1000000, SPI_MODE_1 },
        { "display", 3, 40000000, SPI_MODE_3 },
    };

    for (uint32_t i = 0; i < sizeof(virt_devs)/sizeof(virt_devs[0]); i++) {
        spi_device_t *spi = &ctlr->devices[i];
        memset(spi, 0, sizeof(*spi));
        strncpy(spi->name, virt_devs[i].name, SPI_NAME_LEN - 1);
        spi->chip_select = virt_devs[i].cs;
        spi->max_speed_hz = virt_devs[i].max_speed;
        spi->mode = virt_devs[i].mode;
        spi->bits_per_word = 8;
        spi->controller = ctlr;
        spi->registered = 1;
        ctlr->n_devices++;
    }
}

static spi_controller_t *spi_find_controller(uint32_t bus_num) {
    spi_controller_t *ctlr = spi_data.ctlr_list;
    while (ctlr) {
        if (ctlr->bus_num == bus_num) return ctlr;
        ctlr = ctlr->next;
    }
    return NULL;
}

static spi_device_t *spi_find_device(spi_controller_t *ctlr, uint32_t cs) {
    if (!ctlr) return NULL;
    for (uint32_t i = 0; i < ctlr->n_devices; i++) {
        if (ctlr->devices[i].registered && ctlr->devices[i].chip_select == cs) {
            return &ctlr->devices[i];
        }
    }
    return NULL;
}

static const char *spi_mode_str(uint32_t mode) {
    switch (mode & (SPI_CPOL | SPI_CPHA)) {
        case SPI_MODE_0: return "mode-0 (0,0)";
        case SPI_MODE_1: return "mode-1 (0,1)";
        case SPI_MODE_2: return "mode-2 (1,0)";
        case SPI_MODE_3: return "mode-3 (1,1)";
        default: return "unknown";
    }
}

int spi_init(void) {
    if (spi_data.initialized) return 0;
    memset(&spi_data, 0, sizeof(spi_data));

    spi_init_virtual_controller(&spi_data.controllers[0], 0);
    spi_data.controllers[0].next = NULL;
    spi_data.ctlr_list = &spi_data.controllers[0];
    spi_data.n_controllers = 1;

    spi_data.initialized = 1;
    klog_info("SPI: SPI bus subsystem initialized (%u controllers, %u devices)",
              spi_data.n_controllers, spi_data.controllers[0].n_devices);
    return 0;
}

int spi_register_controller(spi_controller_t *ctlr) {
    if (!spi_data.initialized || !ctlr || spi_data.n_controllers >= SPI_MAX_CONTROLLERS) return -1;
    memcpy(&spi_data.controllers[spi_data.n_controllers], ctlr, sizeof(*ctlr));
    spi_controller_t *new_ctlr = &spi_data.controllers[spi_data.n_controllers];
    new_ctlr->bus_num = spi_data.n_controllers;
    new_ctlr->next = spi_data.ctlr_list;
    spi_data.ctlr_list = new_ctlr;
    spi_data.n_controllers++;
    klog_info("SPI: registered controller '%s' (bus=%u, cs=%u)",
              new_ctlr->name, new_ctlr->bus_num, new_ctlr->num_chipselect);
    return 0;
}

int spi_add_device(spi_controller_t *ctlr, spi_device_t *spi) {
    if (!spi_data.initialized || !ctlr || !spi || ctlr->n_devices >= SPI_MAX_DEVICES) return -1;
    memcpy(&ctlr->devices[ctlr->n_devices], spi, sizeof(*spi));
    spi_device_t *new_spi = &ctlr->devices[ctlr->n_devices];
    new_spi->controller = ctlr;
    new_spi->registered = 1;
    spi_setup(new_spi);
    ctlr->n_devices++;
    klog_info("SPI: added device '%s' (cs=%u, mode=%u)",
              new_spi->name, new_spi->chip_select, new_spi->mode);
    return 0;
}

int spi_setup(spi_device_t *spi) {
    if (!spi || !spi->controller || !spi->controller->setup) return -1;
    return spi->controller->setup(spi);
}

int spi_sync(spi_device_t *spi, spi_message_t *msg) {
    if (!spi_data.initialized || !spi || !spi->controller || !msg) return -1;
    spi_controller_t *ctlr = spi->controller;
    int ret = 0;

    if (ctlr->prepare_message) {
        ret = ctlr->prepare_message(ctlr, msg);
        if (ret) return ret;
    }

    msg->actual_length = 0;
    msg->status = 0;

    for (uint32_t i = 0; i < msg->n_transfers; i++) {
        struct spi_transfer *xfer = &msg->transfers[i];
        if (ctlr->transfer_one) {
            ret = ctlr->transfer_one(ctlr, spi, xfer);
            if (ret) {
                msg->status = ret;
                ctlr->error_count++;
                spi_data.total_errors++;
                break;
            }
            msg->actual_length += xfer->len;
            ctlr->bytes_total += xfer->len;
            spi->bytes_transferred += xfer->len;
            spi_data.total_bytes += xfer->len;
        }
    }

    if (ret == 0) {
        ctlr->xfer_count++;
        spi->xfer_count++;
        spi_data.total_xfers++;
        if (msg->complete) msg->complete(msg->context);
    }

    if (ctlr->unprepare_message) {
        ctlr->unprepare_message(ctlr, msg);
    }

    return ret;
}

void spi_print_stats(void) {
    static uint8_t tx_buf[16] = { 0x9F, 0, 0, 0 };
    static uint8_t rx_buf[16];
    struct spi_transfer xfer = { tx_buf, rx_buf, 4, 10000000, 0, 8, 1 };
    spi_controller_t *ctlr = spi_data.ctlr_list;
    if (ctlr) {
        spi_device_t *flash = spi_find_device(ctlr, 0);
        if (flash) {
            spi_message_t msg;
            msg.transfers = &xfer;
            msg.n_transfers = 1;
            msg.spi = flash;
            msg.complete = NULL;
            msg.context = NULL;
            spi_sync(flash, &msg);
        }
    }

    klog_info("=== SPI Bus Subsystem Statistics ===");
    klog_info("Initialized: %s", spi_data.initialized ? "yes" : "no");
    klog_info("SPI controllers: %u", spi_data.n_controllers);
    klog_info("Total transfers: %llu", (unsigned long long)spi_data.total_xfers);
    klog_info("Total bytes transferred: %llu", (unsigned long long)spi_data.total_bytes);
    klog_info("Total errors: %llu", (unsigned long long)spi_data.total_errors);
    klog_info("");

    klog_info("SPI controllers:");
    ctlr = spi_data.ctlr_list;
    uint32_t cidx = 0;
    while (ctlr && cidx < SPI_MAX_CONTROLLERS) {
        klog_info("  [%u] %s (bus %u, %u chip selects)",
                  cidx, ctlr->name, ctlr->bus_num, ctlr->num_chipselect);
        klog_info("    speed range: %u - %u Hz", ctlr->min_speed_hz, ctlr->max_speed_hz);
        klog_info("    ops: xfer=%llu bytes=%llu err=%llu",
                  (unsigned long long)ctlr->xfer_count,
                  (unsigned long long)ctlr->bytes_total,
                  (unsigned long long)ctlr->error_count);
        klog_info("    devices (%u):", ctlr->n_devices);
        for (uint32_t i = 0; i < ctlr->n_devices; i++) {
            spi_device_t *spi = &ctlr->devices[i];
            if (spi->registered) {
                klog_info("      cs%u '%s': %s, speed=%uHz, bpw=%u, xfer=%llu, bytes=%llu",
                          spi->chip_select, spi->name,
                          spi_mode_str(spi->mode),
                          spi->max_speed_hz, spi->bits_per_word,
                          (unsigned long long)spi->xfer_count,
                          (unsigned long long)spi->bytes_transferred);
                if (spi->mode & SPI_CS_HIGH) klog_info("        CS_HIGH");
                if (spi->mode & SPI_LSB_FIRST) klog_info("        LSB_FIRST");
                if (spi->mode & SPI_3WIRE) klog_info("        3WIRE");
                if (spi->mode & SPI_LOOP) klog_info("        LOOP");
            }
        }
        klog_info("");
        ctlr = ctlr->next;
        cidx++;
    }
}
