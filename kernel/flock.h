#ifndef FLOCK_H
#define FLOCK_H

#include "stdint.h"

#define LOCK_SH  1  /* Shared lock */
#define LOCK_EX  2  /* Exclusive lock */
#define LOCK_UN  8  /* Unlock */
#define LOCK_NB  4  /* Non-blocking */

typedef struct file_lock {
    uint32_t pid;          /* Owning process */
    uint32_t inode_num;    /* Locked inode */
    uint32_t type;         /* LOCK_SH or LOCK_EX */
    uint64_t start;        /* Byte offset start */
    uint64_t len;          /* Length (0 = EOF) */
    struct file_lock *next;
} file_lock_t;

void flock_init(void);
int flock_acquire(uint32_t pid, uint32_t inode, uint32_t type, uint64_t start, uint64_t len, uint32_t flags);
int flock_release(uint32_t pid, uint32_t inode);
int flock_check(uint32_t inode, uint64_t offset, uint32_t access_type);
void flock_release_all(uint32_t pid);  /* Release all locks for a process */

#endif
