#include "quota.h"
#include "kheap.h"
#include "string.h"
#include "sync.h"

#define QUOTA_MAX_USERS 64

static quota_entry_t quota_table[QUOTA_MAX_USERS];
static uint32_t quota_count = 0;
static spinlock_t quota_lock;

void quota_init(void) {
    memset(quota_table, 0, sizeof(quota_table));
    quota_count = 0;
    spinlock_init(&quota_lock);
}

/* Find or create a quota entry for the given uid */
static quota_entry_t *quota_find(uint32_t uid) {
    uint32_t i;
    for (i = 0; i < quota_count; i++) {
        if (quota_table[i].uid == uid) {
            return &quota_table[i];
        }
    }
    /* Not found, create a new entry if space available */
    if (quota_count < QUOTA_MAX_USERS) {
        quota_entry_t *entry = &quota_table[quota_count];
        memset(entry, 0, sizeof(quota_entry_t));
        entry->uid = uid;
        quota_count++;
        return entry;
    }
    return NULL;
}

/* Check block quota: returns 0 if under limit, 1 if over soft, 2 if over hard */
int quota_check_block(uint32_t uid, uint64_t blocks) {
    spinlock_lock(&quota_lock);

    quota_entry_t *entry = quota_find(uid);
    if (!entry) {
        spinlock_unlock(&quota_lock);
        return 0;  /* No quota set, allow */
    }

    /* If no hard limit set, allow */
    if (entry->blocks_hard == 0) {
        spinlock_unlock(&quota_lock);
        return 0;
    }

    uint64_t new_total = entry->blocks_used + blocks;

    if (new_total > entry->blocks_hard) {
        spinlock_unlock(&quota_lock);
        return 2;  /* Over hard limit */
    }

    if (new_total > entry->blocks_soft) {
        spinlock_unlock(&quota_lock);
        return 1;  /* Over soft limit */
    }

    spinlock_unlock(&quota_lock);
    return 0;
}

/* Check inode quota: returns 0 if under limit, 1 if over soft, 2 if over hard */
int quota_check_inode(uint32_t uid, uint64_t inodes) {
    spinlock_lock(&quota_lock);

    quota_entry_t *entry = quota_find(uid);
    if (!entry) {
        spinlock_unlock(&quota_lock);
        return 0;  /* No quota set, allow */
    }

    /* If no hard limit set, allow */
    if (entry->inodes_hard == 0) {
        spinlock_unlock(&quota_lock);
        return 0;
    }

    uint64_t new_total = entry->inodes_used + inodes;

    if (new_total > entry->inodes_hard) {
        spinlock_unlock(&quota_lock);
        return 2;  /* Over hard limit */
    }

    if (new_total > entry->inodes_soft) {
        spinlock_unlock(&quota_lock);
        return 1;  /* Over soft limit */
    }

    spinlock_unlock(&quota_lock);
    return 0;
}

/* Record block usage */
void quota_add_block(uint32_t uid, uint64_t count) {
    spinlock_lock(&quota_lock);

    quota_entry_t *entry = quota_find(uid);
    if (entry) {
        entry->blocks_used += count;
    }

    spinlock_unlock(&quota_lock);
}

/* Record inode usage */
void quota_add_inode(uint32_t uid, uint64_t count) {
    spinlock_lock(&quota_lock);

    quota_entry_t *entry = quota_find(uid);
    if (entry) {
        entry->inodes_used += count;
    }

    spinlock_unlock(&quota_lock);
}

/* Set quota limits for a user */
int quota_set(uint32_t uid, uint64_t bsoft, uint64_t bhard, uint64_t isoft, uint64_t ihard) {
    spinlock_lock(&quota_lock);

    quota_entry_t *entry = quota_find(uid);
    if (!entry) {
        spinlock_unlock(&quota_lock);
        return -1;  /* Table full */
    }

    entry->blocks_soft = bsoft;
    entry->blocks_hard = bhard;
    entry->inodes_soft = isoft;
    entry->inodes_hard = ihard;

    spinlock_unlock(&quota_lock);
    return 0;
}

/* Get quota entry for a user */
quota_entry_t *quota_get(uint32_t uid) {
    spinlock_lock(&quota_lock);

    uint32_t i;
    for (i = 0; i < quota_count; i++) {
        if (quota_table[i].uid == uid) {
            spinlock_unlock(&quota_lock);
            return &quota_table[i];
        }
    }

    spinlock_unlock(&quota_lock);
    return NULL;
}
