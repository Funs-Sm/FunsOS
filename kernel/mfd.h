#ifndef MFD_H
#define MFD_H

#include "stdint.h"

#define MFD_NAME_LEN 32
#define MFD_MAX_DEVS 4
#define MFD_MAX_CELLS 16

struct mfd_cell {
    char name[MFD_NAME_LEN];
    uint32_t cell_id;
    uint8_t enabled;
    uint32_t resources_count;
    uint64_t enable_count;
    uint64_t disable_count;
};

typedef struct mfd_cell mfd_cell_t;

struct mfd_device;

typedef int (*mfd_enable_cell_t)(struct mfd_device *mfd, mfd_cell_t *cell);
typedef int (*mfd_disable_cell_t)(struct mfd_device *mfd, mfd_cell_t *cell);

struct mfd_device {
    char name[MFD_NAME_LEN];
    mfd_cell_t cells[MFD_MAX_CELLS];
    uint32_t n_cells;
    mfd_enable_cell_t enable_cell;
    mfd_disable_cell_t disable_cell;
    void *data;
    uint64_t add_count;
    uint64_t enable_count;
    uint64_t disable_count;
    struct mfd_device *next;
};

typedef struct mfd_device mfd_device_t;

int mfd_init(void);
int mfd_add_device(mfd_device_t *mfd);
int mfd_add_devices(mfd_device_t *mfd, mfd_cell_t *cells, uint32_t n_cells);
int mfd_cell_enable(mfd_device_t *mfd, uint32_t cell_id);
int mfd_cell_disable(mfd_device_t *mfd, uint32_t cell_id);
void mfd_print_stats(void);

#endif
