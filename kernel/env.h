#ifndef ENV_H
#define ENV_H

#include "stdint.h"

#define SYSENV_MAX_VARS      128
#define SYSENV_MAX_NAME      128
#define SYSENV_MAX_VALUE     512
#define SYSENV_MAX_TOTAL     (SYSENV_MAX_VARS * (SYSENV_MAX_NAME + SYSENV_MAX_VALUE))

typedef struct sysenv_entry {
    char name[SYSENV_MAX_NAME];
    char value[SYSENV_MAX_VALUE];
    int  used;
} sysenv_entry_t;

typedef struct sysenv_table {
    sysenv_entry_t entries[SYSENV_MAX_VARS];
    uint32_t count;
} sysenv_table_t;

void     sysenv_init(void);
int      sysenv_set(const char *name, const char *value, int overwrite);
int      sysenv_unset(const char *name);
const char *sysenv_get(const char *name);
int      sysenv_get_all(char *buf, uint32_t buf_size, uint32_t *out_count);
uint32_t sysenv_count(void);
void     sysenv_reset(void);
int      sysenv_set_sys(const char *name, const char *value);

#endif
