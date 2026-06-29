#ifndef SHMDEV_H
#define SHMDEV_H

#include "stdint.h"

#define SHMDEV_MAX_SEGMENTS 64
#define SHMDEV_MAX_NAME     64
#define SHMDEV_MAJOR 10
#define SHMDEV_MINOR 0

typedef struct {
    char        name[SHMDEV_MAX_NAME];
    uint32_t    size;
    uint8_t    *data;
    int         in_use;
    uint32_t    ref_count;
    uint32_t    permissions;
} shmdev_segment_t;

int  shmdev_init(void);
int  shmdev_create(const char *name, uint32_t size);
int  shmdev_open(const char *name);
int  shmdev_read(int id, uint32_t offset, uint32_t count, void *buf);
int  shmdev_write(int id, uint32_t offset, uint32_t count, const void *buf);
int  shmdev_close(int id);
int  shmdev_unlink(const char *name);
shmdev_segment_t *shmdev_get_segment(int id);
int  shmdev_get_count(void);

#endif
