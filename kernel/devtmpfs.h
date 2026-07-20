#ifndef DEVTMPFS_H
#define DEVTMPFS_H

#include "stdint.h"

#define DEVTMPFS_NAME_MAX    64
#define DEVTMPFS_MAX_NODES   64

#define DEV_TYPE_CHAR    1
#define DEV_TYPE_BLOCK   2
#define DEV_TYPE_FIFO    3
#define DEV_TYPE_SOCK    4

#define DEV_MODE_READ    0x4
#define DEV_MODE_WRITE   0x2
#define DEV_MODE_EXEC    0x1

typedef uint32_t dev_t;

struct devtmpfs_node {
    char name[DEVTMPFS_NAME_MAX];
    uint32_t mode;
    dev_t dev;
    uint32_t uid;
    uint32_t gid;
    uint32_t type;
    uint32_t used;
    uint64_t open_count;
    uint64_t read_count;
    uint64_t write_count;
};

typedef struct devtmpfs_node devtmpfs_node_t;

int devtmpfs_init(void);
int devtmpfs_create_node(const char *name, uint32_t type, dev_t dev, uint32_t mode, uint32_t uid, uint32_t gid);
int devtmpfs_delete_node(const char *name);
devtmpfs_node_t *devtmpfs_find_node(const char *name);
void devtmpfs_print_stats(void);

#endif
