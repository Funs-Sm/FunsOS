#ifndef QUOTA_H
#define QUOTA_H

#include "stdint.h"

typedef struct {
    uint32_t uid;
    uint64_t blocks_used;
    uint64_t blocks_soft;
    uint64_t blocks_hard;
    uint64_t inodes_used;
    uint64_t inodes_soft;
    uint64_t inodes_hard;
    uint32_t btime;  /* Grace time for blocks */
    uint32_t itime;  /* Grace time for inodes */
} quota_entry_t;

void quota_init(void);
int quota_check_block(uint32_t uid, uint64_t blocks);
int quota_check_inode(uint32_t uid, uint64_t inodes);
void quota_add_block(uint32_t uid, uint64_t count);
void quota_add_inode(uint32_t uid, uint64_t count);
int quota_set(uint32_t uid, uint64_t bsoft, uint64_t bhard, uint64_t isoft, uint64_t ihard);
quota_entry_t *quota_get(uint32_t uid);

#endif
