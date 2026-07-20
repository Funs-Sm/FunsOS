#ifndef APPARMOR_H
#define APPARMOR_H

#include "stdint.h"

#define AA_NAME_MAX         64
#define AA_MAX_PROFILES     32
#define AA_MAX_RULES        128
#define AA_MAX_PROCS        64

#define AA_MODE_ENFORCE     0
#define AA_MODE_COMPLAIN    1
#define AA_MODE_DISABLED    2

#define AA_PERM_READ    0x01
#define AA_PERM_WRITE   0x02
#define AA_PERM_EXEC    0x04
#define AA_PERM_APPEND  0x08
#define AA_PERM_ALL     0xFF

#define AA_CLASS_FILE       1
#define AA_CLASS_CAPABILITY 2
#define AA_CLASS_NETWORK    3

#define AA_ALLOW 0
#define AA_DENY  1
#define AA_AUDIT 2

struct aa_rule {
    uint32_t class;
    uint32_t perm;
    uint32_t action;
    char pattern[AA_NAME_MAX];
    uint32_t used;
    uint64_t hits;
};

struct aa_profile {
    char name[AA_NAME_MAX];
    uint32_t mode;
    uint32_t used;
    uint32_t attached;
    struct aa_rule rules[AA_MAX_RULES];
    uint32_t rule_count;
    uint64_t allow_count;
    uint64_t deny_count;
    uint64_t complain_count;
    uint32_t refcount;
};

typedef struct aa_profile aa_profile_t;

struct aa_task_ctx {
    uint32_t pid;
    aa_profile_t *profile;
    uint32_t used;
};

int apparmor_init(void);
aa_profile_t *aa_profile_new(const char *name, uint32_t mode);
int aa_replace_profile(aa_profile_t *old, aa_profile_t *new);
int aa_profile_add_rule(aa_profile_t *profile, uint32_t class, uint32_t perm,
                        uint32_t action, const char *pattern);
int aa_check_permission(uint32_t pid, uint32_t class, uint32_t perm, const char *name);
aa_profile_t *aa_find_profile(const char *name);
void apparmor_print_stats(void);

#endif
