#include "mfd.h"
#include "klog.h"
#include "string.h"

struct mfd_global {
    uint8_t initialized;
    mfd_device_t devices[MFD_MAX_DEVS];
    uint32_t n_devices;
    mfd_device_t *dev_list;
    uint64_t total_adds;
    uint64_t total_enables;
    uint64_t total_disables;
};

static struct mfd_global mfd_data;

static int virt_mfd_enable_cell(mfd_device_t *mfd, mfd_cell_t *cell) {
    (void)mfd;
    if (!cell) return -1;
    if (cell->enabled) return 0;
    cell->enabled = 1;
    cell->enable_count++;
    return 0;
}

static int virt_mfd_disable_cell(mfd_device_t *mfd, mfd_cell_t *cell) {
    (void)mfd;
    if (!cell) return -1;
    if (!cell->enabled) return 0;
    cell->enabled = 0;
    cell->disable_count++;
    return 0;
}

static void mfd_init_pmic(mfd_device_t *mfd) {
    memset(mfd, 0, sizeof(*mfd));
    strncpy(mfd->name, "pmic_mfd", MFD_NAME_LEN - 1);
    mfd->enable_cell = virt_mfd_enable_cell;
    mfd->disable_cell = virt_mfd_disable_cell;

    static const struct {
        const char *name;
        uint32_t id;
    } pmic_cells[] = {
        { "regulator_core", 0 },
        { "regulator_io", 1 },
        { "regulator_mem", 2 },
        { "gpio_pmic", 3 },
        { "rtc_pmic", 4 },
        { "charger", 5 },
        { "fuel_gauge", 6 },
        { "led_pmic", 7 },
    };

    for (uint32_t i = 0; i < sizeof(pmic_cells)/sizeof(pmic_cells[0]); i++) {
        mfd_cell_t *cell = &mfd->cells[i];
        memset(cell, 0, sizeof(*cell));
        strncpy(cell->name, pmic_cells[i].name, MFD_NAME_LEN - 1);
        cell->cell_id = pmic_cells[i].id;
        cell->enabled = 0;
        cell->resources_count = 0;
        mfd->n_cells++;
    }
}

int mfd_init(void) {
    if (mfd_data.initialized) return 0;
    memset(&mfd_data, 0, sizeof(mfd_data));

    mfd_init_pmic(&mfd_data.devices[0]);
    mfd_data.devices[0].next = NULL;
    mfd_data.dev_list = &mfd_data.devices[0];
    mfd_data.n_devices = 1;

    mfd_cell_enable(&mfd_data.devices[0], 0);
    mfd_cell_enable(&mfd_data.devices[0], 1);
    mfd_cell_enable(&mfd_data.devices[0], 2);
    mfd_cell_enable(&mfd_data.devices[0], 3);

    mfd_data.initialized = 1;
    klog_info("MFD: Multi-function device subsystem initialized (%u devices, %u cells)",
              mfd_data.n_devices, mfd_data.devices[0].n_cells);
    return 0;
}

int mfd_add_device(mfd_device_t *mfd) {
    if (!mfd_data.initialized || !mfd || mfd_data.n_devices >= MFD_MAX_DEVS) return -1;
    memcpy(&mfd_data.devices[mfd_data.n_devices], mfd, sizeof(*mfd));
    mfd_device_t *new_mfd = &mfd_data.devices[mfd_data.n_devices];
    new_mfd->next = mfd_data.dev_list;
    mfd_data.dev_list = new_mfd;
    mfd_data.n_devices++;
    klog_info("MFD: added device '%s' (%u cells)", new_mfd->name, new_mfd->n_cells);
    return 0;
}

int mfd_add_devices(mfd_device_t *mfd, mfd_cell_t *cells, uint32_t n_cells) {
    if (!mfd_data.initialized || !mfd || !cells || n_cells == 0) return -1;
    uint32_t added = 0;
    for (uint32_t i = 0; i < n_cells && mfd->n_cells < MFD_MAX_CELLS; i++) {
        memcpy(&mfd->cells[mfd->n_cells], &cells[i], sizeof(mfd_cell_t));
        mfd->cells[mfd->n_cells].enabled = 0;
        mfd->n_cells++;
        added++;
        mfd->add_count++;
        mfd_data.total_adds++;
    }
    klog_info("MFD: added %u cells to '%s'", added, mfd->name);
    return added;
}

int mfd_cell_enable(mfd_device_t *mfd, uint32_t cell_id) {
    if (!mfd) return -1;
    for (uint32_t i = 0; i < mfd->n_cells; i++) {
        if (mfd->cells[i].cell_id == cell_id) {
            if (mfd->enable_cell) {
                int ret = mfd->enable_cell(mfd, &mfd->cells[i]);
                if (ret == 0) {
                    mfd->enable_count++;
                    mfd_data.total_enables++;
                }
                return ret;
            }
            mfd->cells[i].enabled = 1;
            mfd->cells[i].enable_count++;
            mfd->enable_count++;
            mfd_data.total_enables++;
            return 0;
        }
    }
    return -1;
}

int mfd_cell_disable(mfd_device_t *mfd, uint32_t cell_id) {
    if (!mfd) return -1;
    for (uint32_t i = 0; i < mfd->n_cells; i++) {
        if (mfd->cells[i].cell_id == cell_id) {
            if (mfd->disable_cell) {
                int ret = mfd->disable_cell(mfd, &mfd->cells[i]);
                if (ret == 0) {
                    mfd->disable_count++;
                    mfd_data.total_disables++;
                }
                return ret;
            }
            mfd->cells[i].enabled = 0;
            mfd->cells[i].disable_count++;
            mfd->disable_count++;
            mfd_data.total_disables++;
            return 0;
        }
    }
    return -1;
}

void mfd_print_stats(void) {
    mfd_cell_enable(&mfd_data.devices[0], 4);
    mfd_cell_disable(&mfd_data.devices[0], 4);

    klog_info("=== MFD Subsystem Statistics ===");
    klog_info("Initialized: %s", mfd_data.initialized ? "yes" : "no");
    klog_info("MFD devices: %u", mfd_data.n_devices);
    klog_info("Total cells added: %llu", (unsigned long long)mfd_data.total_adds);
    klog_info("Total cell enables: %llu", (unsigned long long)mfd_data.total_enables);
    klog_info("Total cell disables: %llu", (unsigned long long)mfd_data.total_disables);
    klog_info("");

    klog_info("MFD devices:");
    mfd_device_t *mfd = mfd_data.dev_list;
    uint32_t didx = 0;
    while (mfd && didx < MFD_MAX_DEVS) {
        klog_info("  [%u] %s (%u cells)", didx, mfd->name, mfd->n_cells);
        klog_info("    ops: add=%llu en=%llu dis=%llu",
                  (unsigned long long)mfd->add_count,
                  (unsigned long long)mfd->enable_count,
                  (unsigned long long)mfd->disable_count);
        klog_info("    cells:");
        for (uint32_t i = 0; i < mfd->n_cells; i++) {
            mfd_cell_t *cell = &mfd->cells[i];
            klog_info("      [%u] '%s': %s (en=%llu dis=%llu, resources=%u)",
                      cell->cell_id, cell->name,
                      cell->enabled ? "enabled" : "disabled",
                      (unsigned long long)cell->enable_count,
                      (unsigned long long)cell->disable_count,
                      cell->resources_count);
        }
        klog_info("");
        mfd = mfd->next;
        didx++;
    }
}
