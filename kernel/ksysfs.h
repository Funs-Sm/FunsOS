#ifndef KSYSFS_H
#define KSYSFS_H

#include "stdint.h"

#define KSYSFS_NAME_MAX      64
#define KSYSFS_MAX_DIRENTS   128
#define KSYSFS_MAX_KOBJECTS  64

#define KSYSFS_DIR   1
#define KSYSFS_FILE  2
#define KSYSFS_LINK  3

struct kobject;

struct ksysfs_dirent {
    char name[KSYSFS_NAME_MAX];
    uint32_t type;
    uint32_t used;
    struct kobject *kobj;
    uint32_t parent_id;
    uint64_t read_count;
    uint64_t write_count;
};

typedef struct ksysfs_dirent ksysfs_dirent_t;

struct kobject {
    char name[KSYSFS_NAME_MAX];
    uint32_t ktype;
    uint32_t used;
    uint32_t refcount;
    ksysfs_dirent_t *sd;
    uint64_t add_count;
    uint64_t remove_count;
};

typedef struct kobject kobject_t;

int ksysfs_init(void);
int ksysfs_create_dir(kobject_t *parent, const char *name, kobject_t *kobj);
int ksysfs_create_file(kobject_t *parent, const char *name);
int ksysfs_create_link(kobject_t *parent, const char *name, kobject_t *target);
kobject_t *ksysfs_kobject_get(kobject_t *kobj);
void ksysfs_kobject_put(kobject_t *kobj);
void ksysfs_print_stats(void);

#endif
