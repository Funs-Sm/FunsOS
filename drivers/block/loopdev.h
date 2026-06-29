#ifndef LOOPDEV_H
#define LOOPDEV_H

#include "stdint.h"
#include "vfs.h"

#define LOOPDEV_MAX_DEVICES 8
#define LOOPDEV_SECTOR_SIZE 512

typedef struct {
    int         id;
    int         in_use;
    char        path[256];
    uint32_t    file_size;
    uint32_t    ref_count;
    file_t     *file;
} loopdev_t;

void loopdev_init(void);
int  loopdev_setup(const char *path);
int  loopdev_detach(int id);
loopdev_t *loopdev_get(int id);
int32_t  loopdev_read(int id, uint32_t offset, uint32_t count, void *buf);
int32_t  loopdev_write(int id, uint32_t offset, uint32_t count, const void *buf);

#endif
