#ifndef NTFS_WRITE_H
#define NTFS_WRITE_H

/* NTFS write support.
 *
 * Adds the primitives needed to mutate an NTFS volume:
 *   - Allocate / free MFT records
 *   - Append attributes to existing records
 *   - Write file data into $DATA (resident or non-resident)
 *   - Update $STANDARD_INFORMATION (timestamps, mode)
 *   - Update $FILE_NAME (rename, link)
 *   - Update parent directory index
 *
 * The implementation plays nicely with ntfs_logfile for crash
 * recovery by emitting logical operations that the log can
 * understand.
 */

#include "stdint.h"
#include "ntfs.h"

#define NTFS_FILE_TYPE_FILE        1
#define NTFS_FILE_TYPE_DIR         2

int  ntfs_write_alloc_inode(uint64_t *out_inode);

int  ntfs_write_create_file(uint64_t parent_inode,
                              const char *name,
                              uint64_t *out_inode);

int  ntfs_write_create_dir(uint64_t parent_inode,
                            const char *name,
                            uint64_t *out_inode);

int  ntfs_write_data(uint64_t inode, uint64_t offset,
                      const void *buf, uint32_t count);

int  ntfs_write_truncate(uint64_t inode, uint64_t new_size);
int  ntfs_write_delete(uint64_t parent_inode, const char *name);

int  ntfs_write_rename(uint64_t parent_inode, const char *old_name,
                        uint64_t new_parent_inode, const char *new_name);

int  ntfs_write_set_attr(uint64_t inode, uint16_t mode);
int  ntfs_write_set_mtime(uint64_t inode, uint64_t mtime);

#endif