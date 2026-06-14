#ifndef CHAR_H
#define CHAR_H

#include "stdint.h"

typedef struct {
    uint32_t major;
    uint32_t minor;
    char name[32];
    int32_t (*open)(void);
    int32_t (*close)(void);
    int32_t (*read)(void *buf, uint32_t count);
    int32_t (*write)(const void *buf, uint32_t count);
} char_device_t;

void char_init(void);
int32_t char_register(uint32_t major, char_device_t *dev);
int32_t char_unregister(uint32_t major);

#endif
