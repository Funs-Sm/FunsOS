/*
 * fs/path_hash.h - fast path-component hashing for dcache lookups.
 *
 * Walking a directory's child linked list is O(n).  Even though our
 * typical directories have < 100 entries, hot paths like `cat /etc/...`
 * re-resolve the same path every time.  Adding a per-directory hash
 * table on top of the linked list gives O(1) lookup for the common
 * case while keeping the linked list for crash-safe iteration.
 *
 * The hash function itself is FNV-1a 32-bit (one byte at a time, no
 * table memory) - small enough to inline and cheap enough that the
 * cache-maintenance cost is dominated by the linked-list walk we are
 * already doing.
 */
#ifndef FS_PATH_HASH_H
#define FS_PATH_HASH_H

#include "stdint.h"

#define PHASH_FNV_OFFSET_BASIS  0x811C9DC5u
#define PHASH_FNV_PRIME         0x01000193u

/* Hash a single component.  Length capped to 64 bytes (anything longer
 * is hashed using the first 64 only - this is enough for unique
 * distribution in our dentry namespace). */
uint32_t path_component_hash(const char *name);

/* Hash a full path.  `len` may be 0 (strlen is called) or non-zero to
 * avoid scanning. */
uint32_t path_full_hash(const char *path, uint32_t len);

/* Cache statistics. */
typedef struct path_hash_stats {
    uint64_t hash_calls;
    uint64_t hash_collisions;
} path_hash_stats_t;

void path_hash_get_stats(path_hash_stats_t *out);
void path_hash_reset_stats(void);

#endif /* FS_PATH_HASH_H */
