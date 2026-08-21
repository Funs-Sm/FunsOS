#ifndef EXT2_WRITE_H
#define EXT2_WRITE_H

#include "stdint.h"
#include "ext2.h"

/* ext2 write support.
 *
 * Adds the missing primitives to support creating new files and
 * directories, writing to existing files, truncating, deleting, and
 * updating directory entries. All metadata changes are journaled
 * through the JBD2 interface (when available) to provide atomicity
 * across crashes.
 *
 * Two block / inode allocators are provided:
 *   - bitmap-based for small filesystems
 *   - chained buddy-block allocator option
 *
 * The dir_entry_emit routine handles entry splitting and unbounded
 * Tail-mode conversion that may appear when extending a directory.
 */

#define EXT2_ROOT_INO        2
#define EXT2_BLOCKS_PER_DIR  1
#define EXT2_PAD_TO_4(n)    ((n + 3) & ~3u)

int ext2_write_init(uint32_t drive, uint32_t partition_start);

int ext2_alloc_block(void);
int ext2_free_block(uint32_t block);
int ext2_alloc_blocks(uint32_t *blocks, uint32_t count);
void ext2_free_blocks(const uint32_t *blocks, uint32_t count);

int ext2_alloc_inode(uint8_t file_type);
int ext2_free_inode(uint32_t ino);

int ext2_create(uint32_t parent_ino, const char *name,
                 uint16_t mode, uint32_t *out_ino);
int ext2_mkdir(uint32_t parent_ino, const char *name,
                uint16_t mode, uint32_t *out_ino);
int ext2_unlink(uint32_t parent_ino, const char *name);
int ext2_rmdir(uint32_t parent_ino, const char *name);
int ext2_rename(uint32_t parent_ino, const char *old_name,
                const char *new_name);

int ext2_write_data(uint32_t ino, uint64_t offset, uint32_t count,
                     const void *buf);

int ext2_dir_add(uint32_t dir_ino, const char *name, uint32_t child_ino,
                 uint8_t file_type);
int ext2_dir_remove(uint32_t dir_ino, const char *name);
int ext2_dir_lookup(uint32_t dir_ino, const char *name, uint32_t *out_ino);

int ext2_sync_group_bitmaps(void);
int ext2_sync_superblock_counts(void);

int ext2_write_journal_init(void);

#endif