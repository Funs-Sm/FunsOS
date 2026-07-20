#ifndef RLIMIT_H
#define RLIMIT_H

#include "stdint.h"

#define RLIMIT_CPU        0
#define RLIMIT_FSIZE      1
#define RLIMIT_DATA       2
#define RLIMIT_STACK      3
#define RLIMIT_CORE       4
#define RLIMIT_NOFILE     5
#define RLIMIT_NPROC      6
#define RLIMIT_MEMLOCK    7
#define RLIMIT_AS         8
#define RLIMIT_LOCKS      9
#define RLIMIT_NLIMITS    10

#define RLIM_INFINITY     0xFFFFFFFF

typedef struct rlimit {
    uint32_t rlim_cur;
    uint32_t rlim_max;
} rlimit_t;

typedef struct rlimit_info {
    rlimit_t limits[RLIMIT_NLIMITS];
} rlimit_info_t;

const char *rlimit_name(uint32_t resource);
void     rlimit_init_defaults(rlimit_info_t *info);
int      rlimit_set(rlimit_info_t *info, uint32_t resource, const rlimit_t *value);
int      rlimit_get(rlimit_info_t *info, uint32_t resource, rlimit_t *value);
int      rlimit_check(rlimit_info_t *info, uint32_t resource, uint32_t value);
uint32_t rlimit_get_cur(rlimit_info_t *info, uint32_t resource);
uint32_t rlimit_get_max(rlimit_info_t *info, uint32_t resource);

#endif
