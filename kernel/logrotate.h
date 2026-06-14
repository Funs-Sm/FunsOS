#ifndef LOGROTATE_H
#define LOGROTATE_H

#include "stdint.h"

#define LOGROTATE_MAX_FILES    9     /* .1 through .9 */
#define LOGROTATE_DEFAULT_SIZE (64 * 1024)  /* 64KB default max size */

typedef struct {
    char filepath[128];       /* Log file path */
    uint32_t max_size;        /* Max size before rotation */
    uint32_t max_files;       /* Number of rotated files to keep */
    uint32_t compress;        /* Whether to compress old logs (0/1) */
    uint32_t current_size;    /* Current file size */
} logrotate_config_t;

void logrotate_init(void);
int logrotate_add(const char *filepath, uint32_t max_size, uint32_t max_files);
int logrotate_check(const char *filepath);
int logrotate_force(const char *filepath);
int logrotate_remove(const char *filepath);
uint32_t logrotate_get_config_count(void);
logrotate_config_t *logrotate_get_config(uint32_t index);

#endif
