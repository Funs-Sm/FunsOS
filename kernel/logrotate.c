#include "logrotate.h"
#include "vfs.h"
#include "kheap.h"
#include "string.h"
#include "stdio.h"
#include "sync.h"

#define LOGROTATE_MAX_CONFIGS 16

static logrotate_config_t configs[LOGROTATE_MAX_CONFIGS];
static uint32_t config_count = 0;
static spinlock_t logrotate_lock;

void logrotate_init(void) {
    memset(configs, 0, sizeof(configs));
    config_count = 0;
    spinlock_init(&logrotate_lock);

    /* Default configs for common log files */
    logrotate_add("/var/log/kernel.log", 64 * 1024, 9);
    logrotate_add("/var/log/syslog", 64 * 1024, 9);
    logrotate_add("/var/log/auth.log", 32 * 1024, 5);
}

int logrotate_add(const char *filepath, uint32_t max_size, uint32_t max_files) {
    if (!filepath || !filepath[0]) return -1;
    if (max_files > LOGROTATE_MAX_FILES) max_files = LOGROTATE_MAX_FILES;

    spinlock_lock(&logrotate_lock);

    /* Check if already exists */
    uint32_t i;
    for (i = 0; i < config_count; i++) {
        if (strcmp(configs[i].filepath, filepath) == 0) {
            /* Update existing config */
            configs[i].max_size = max_size;
            configs[i].max_files = max_files;
            spinlock_unlock(&logrotate_lock);
            return 0;
        }
    }

    if (config_count >= LOGROTATE_MAX_CONFIGS) {
        spinlock_unlock(&logrotate_lock);
        return -1;
    }

    logrotate_config_t *cfg = &configs[config_count];
    strncpy(cfg->filepath, filepath, sizeof(cfg->filepath) - 1);
    cfg->filepath[sizeof(cfg->filepath) - 1] = '\0';
    cfg->max_size = max_size;
    cfg->max_files = max_files;
    cfg->compress = 0;
    cfg->current_size = 0;

    /* Try to get current file size */
    inode_t stat;
    memset(&stat, 0, sizeof(inode_t));
    if (vfs_stat(filepath, &stat) == 0) {
        cfg->current_size = stat.size;
    }

    config_count++;
    spinlock_unlock(&logrotate_lock);
    return 0;
}

/* Perform the actual rotation for a config entry */
static int do_rotate(logrotate_config_t *cfg) {
    char old_path[160];
    char new_path[160];
    uint32_t i;

    /* Delete the oldest file (filepath.N) */
    snprintf(old_path, sizeof(old_path), "%s.%d", cfg->filepath, cfg->max_files);
    vfs_unlink(old_path);

    /* Shift rotated files: filepath.(N-1) -> filepath.N, ... filepath.1 -> filepath.2 */
    for (i = cfg->max_files; i > 1; i--) {
        snprintf(old_path, sizeof(old_path), "%s.%d", cfg->filepath, i - 1);
        snprintf(new_path, sizeof(new_path), "%s.%d", cfg->filepath, i);
        /* Rename old_path to new_path using VFS unlink + recreate approach.
           Since we don't have a vfs_rename, we simulate by reading and writing. */
        /* Try to read the source file */
        file_t *src_file = NULL;
        if (vfs_open(old_path, FILE_MODE_READ, &src_file) == 0 && src_file) {
            /* Read the file content */
            char *buf = (char *)kmalloc(cfg->max_size);
            if (buf) {
                int32_t bytes_read = vfs_read(src_file, buf, cfg->max_size);
                vfs_close(src_file);

                if (bytes_read > 0) {
                    /* Delete old destination if exists */
                    vfs_unlink(new_path);

                    /* Write to new location */
                    file_t *dst_file = NULL;
                    if (vfs_open(new_path, FILE_MODE_WRITE | FILE_MODE_READ, &dst_file) == 0 && dst_file) {
                        vfs_write(dst_file, buf, bytes_read);
                        vfs_close(dst_file);
                    }
                }
                kfree(buf);
            } else {
                vfs_close(src_file);
            }
        }
        /* Remove the source file */
        vfs_unlink(old_path);
    }

    /* Rename current file to filepath.1 */
    {
        snprintf(new_path, sizeof(new_path), "%s.1", cfg->filepath);

        file_t *src_file = NULL;
        if (vfs_open(cfg->filepath, FILE_MODE_READ, &src_file) == 0 && src_file) {
            char *buf = (char *)kmalloc(cfg->max_size + 1);
            if (buf) {
                int32_t bytes_read = vfs_read(src_file, buf, cfg->max_size);
                vfs_close(src_file);

                if (bytes_read > 0) {
                    vfs_unlink(new_path);
                    file_t *dst_file = NULL;
                    if (vfs_open(new_path, FILE_MODE_WRITE | FILE_MODE_READ, &dst_file) == 0 && dst_file) {
                        vfs_write(dst_file, buf, bytes_read);
                        vfs_close(dst_file);
                    }
                }
                kfree(buf);
            } else {
                vfs_close(src_file);
            }
        }
        /* Remove the original file */
        vfs_unlink(cfg->filepath);
    }

    /* Create new empty log file */
    {
        file_t *new_file = NULL;
        if (vfs_open(cfg->filepath, FILE_MODE_WRITE | FILE_MODE_READ, &new_file) == 0 && new_file) {
            vfs_close(new_file);
        }
    }

    cfg->current_size = 0;
    return 0;
}

int logrotate_check(const char *filepath) {
    if (!filepath) return -1;

    spinlock_lock(&logrotate_lock);

    uint32_t i;
    for (i = 0; i < config_count; i++) {
        if (strcmp(configs[i].filepath, filepath) == 0) {
            /* Update current size from file system */
            inode_t stat;
            memset(&stat, 0, sizeof(inode_t));
            if (vfs_stat(filepath, &stat) == 0) {
                configs[i].current_size = stat.size;
            }

            if (configs[i].current_size >= configs[i].max_size) {
                int ret = do_rotate(&configs[i]);
                spinlock_unlock(&logrotate_lock);
                return ret;
            }

            spinlock_unlock(&logrotate_lock);
            return 0;  /* No rotation needed */
        }
    }

    spinlock_unlock(&logrotate_lock);
    return -1;  /* Config not found */
}

int logrotate_force(const char *filepath) {
    if (!filepath) return -1;

    spinlock_lock(&logrotate_lock);

    uint32_t i;
    for (i = 0; i < config_count; i++) {
        if (strcmp(configs[i].filepath, filepath) == 0) {
            int ret = do_rotate(&configs[i]);
            spinlock_unlock(&logrotate_lock);
            return ret;
        }
    }

    spinlock_unlock(&logrotate_lock);
    return -1;  /* Config not found */
}

int logrotate_remove(const char *filepath) {
    if (!filepath) return -1;

    spinlock_lock(&logrotate_lock);

    uint32_t i;
    for (i = 0; i < config_count; i++) {
        if (strcmp(configs[i].filepath, filepath) == 0) {
            /* Shift remaining configs down */
            uint32_t j;
            for (j = i; j < config_count - 1; j++) {
                configs[j] = configs[j + 1];
            }
            config_count--;
            memset(&configs[config_count], 0, sizeof(logrotate_config_t));
            spinlock_unlock(&logrotate_lock);
            return 0;
        }
    }

    spinlock_unlock(&logrotate_lock);
    return -1;  /* Not found */
}

uint32_t logrotate_get_config_count(void) {
    return config_count;
}

logrotate_config_t *logrotate_get_config(uint32_t index) {
    if (index >= config_count) return NULL;
    return &configs[index];
}
