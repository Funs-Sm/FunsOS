#include "rlimit.h"
#include "string.h"
#include "stddef.h"

static const char *rlimit_names[RLIMIT_NLIMITS] = {
    "cpu",
    "fsize",
    "data",
    "stack",
    "core",
    "nofile",
    "nproc",
    "memlock",
    "as",
    "locks",
};

const char *rlimit_name(uint32_t resource) {
    if (resource >= RLIMIT_NLIMITS) return NULL;
    return rlimit_names[resource];
}

void rlimit_init_defaults(rlimit_info_t *info) {
    if (!info) return;
    memset(info, 0, sizeof(rlimit_info_t));

    info->limits[RLIMIT_CPU].rlim_cur     = RLIM_INFINITY;
    info->limits[RLIMIT_CPU].rlim_max     = RLIM_INFINITY;
    info->limits[RLIMIT_FSIZE].rlim_cur   = RLIM_INFINITY;
    info->limits[RLIMIT_FSIZE].rlim_max   = RLIM_INFINITY;
    info->limits[RLIMIT_DATA].rlim_cur    = RLIM_INFINITY;
    info->limits[RLIMIT_DATA].rlim_max    = RLIM_INFINITY;
    info->limits[RLIMIT_STACK].rlim_cur   = 8 * 1024 * 1024;
    info->limits[RLIMIT_STACK].rlim_max   = RLIM_INFINITY;
    info->limits[RLIMIT_CORE].rlim_cur    = 0;
    info->limits[RLIMIT_CORE].rlim_max    = RLIM_INFINITY;
    info->limits[RLIMIT_NOFILE].rlim_cur  = 256;
    info->limits[RLIMIT_NOFILE].rlim_max  = 1024;
    info->limits[RLIMIT_NPROC].rlim_cur   = 256;
    info->limits[RLIMIT_NPROC].rlim_max   = 1024;
    info->limits[RLIMIT_MEMLOCK].rlim_cur = 64 * 1024;
    info->limits[RLIMIT_MEMLOCK].rlim_max = RLIM_INFINITY;
    info->limits[RLIMIT_AS].rlim_cur      = RLIM_INFINITY;
    info->limits[RLIMIT_AS].rlim_max      = RLIM_INFINITY;
    info->limits[RLIMIT_LOCKS].rlim_cur   = RLIM_INFINITY;
    info->limits[RLIMIT_LOCKS].rlim_max   = RLIM_INFINITY;
}

int rlimit_set(rlimit_info_t *info, uint32_t resource, const rlimit_t *value) {
    if (!info || !value) return -1;
    if (resource >= RLIMIT_NLIMITS) return -1;

    if (value->rlim_cur > value->rlim_max) return -1;

    if (value->rlim_max > info->limits[resource].rlim_max &&
        info->limits[resource].rlim_max != RLIM_INFINITY) {
        return -1;
    }

    info->limits[resource].rlim_cur = value->rlim_cur;
    info->limits[resource].rlim_max = value->rlim_max;
    return 0;
}

int rlimit_get(rlimit_info_t *info, uint32_t resource, rlimit_t *value) {
    if (!info || !value) return -1;
    if (resource >= RLIMIT_NLIMITS) return -1;

    value->rlim_cur = info->limits[resource].rlim_cur;
    value->rlim_max = info->limits[resource].rlim_max;
    return 0;
}

int rlimit_check(rlimit_info_t *info, uint32_t resource, uint32_t value) {
    if (!info) return -1;
    if (resource >= RLIMIT_NLIMITS) return -1;

    if (info->limits[resource].rlim_cur == RLIM_INFINITY) return 0;

    return (value <= info->limits[resource].rlim_cur) ? 0 : -1;
}

uint32_t rlimit_get_cur(rlimit_info_t *info, uint32_t resource) {
    if (!info || resource >= RLIMIT_NLIMITS) return 0;
    return info->limits[resource].rlim_cur;
}

uint32_t rlimit_get_max(rlimit_info_t *info, uint32_t resource) {
    if (!info || resource >= RLIMIT_NLIMITS) return 0;
    return info->limits[resource].rlim_max;
}
