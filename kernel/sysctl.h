#ifndef SYSCTL_H
#define SYSCTL_H

#include "stdint.h"

#define SYSCTL_MAX_NAME 64
#define SYSCTL_MAX_ENTRIES 128
#define SYSCTL_MAX_STR 128

typedef enum {
    SYSCTL_INT = 0,
    SYSCTL_UINT = 1,
    SYSCTL_STRING = 2,
    SYSCTL_BOOL = 3,
    SYSCTL_LONG = 4
} sysctl_type_t;

typedef int (*sysctl_read_func_t)(char *buf, uint32_t buf_size);
typedef int (*sysctl_write_func_t)(const char *value);

struct sysctl_entry {
    char name[SYSCTL_MAX_NAME];
    sysctl_type_t type;
    void *data;
    uint32_t data_size;
    sysctl_read_func_t read;
    sysctl_write_func_t write;
    uint32_t min_val;
    uint32_t max_val;
    const char *description;
};

void sysctl_init(void);
int sysctl_register(const char *name, sysctl_type_t type, void *data, uint32_t size,
                    uint32_t min_val, uint32_t max_val, const char *description);
int sysctl_register_handler(const char *name, sysctl_read_func_t read,
                            sysctl_write_func_t write, const char *description);
int sysctl_set(const char *name, const char *value);
int sysctl_get(const char *name, char *buf, uint32_t buf_size);
int sysctl_set_int(const char *name, int value);
int sysctl_get_int(const char *name, int *value);
void sysctl_dump_all(void);
int sysctl_unregister(const char *name);
struct sysctl_entry *sysctl_find(const char *name);

#endif
