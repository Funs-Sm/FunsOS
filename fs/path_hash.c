/*
 * fs/path_hash.c - FNV-1a path hashing.
 *
 * Hash policy: the underlying dcache keeps a child linked list for
 * crash-safe iteration; this module computes 32-bit FNV-1a hashes for
 * path components which can be used as a quick bucket index when we
 * extend the dcache with per-directory hash tables later.  Standalone
 * use today is `path_component_hash()` for caching resolver results.
 */
#include "path_hash.h"
#include "string.h"

static path_hash_stats_t g_stats;

uint32_t path_component_hash(const char *name)
{
    if (!name) return PHASH_FNV_OFFSET_BASIS;
    g_stats.hash_calls++;
    uint32_t h = PHASH_FNV_OFFSET_BASIS;
    for (uint32_t i = 0; name[i] && i < 64; i++) {
        h ^= (uint8_t)name[i];
        h *= PHASH_FNV_PRIME;
    }
    return h;
}

uint32_t path_full_hash(const char *path, uint32_t len)
{
    if (!path) return PHASH_FNV_OFFSET_BASIS;
    if (len == 0) {
        while (path[len]) len++;
    }
    g_stats.hash_calls++;
    uint32_t h = PHASH_FNV_OFFSET_BASIS;
    for (uint32_t i = 0; i < len; i++) {
        h ^= (uint8_t)path[i];
        h *= PHASH_FNV_PRIME;
    }
    return h;
}

void path_hash_get_stats(path_hash_stats_t *out)
{
    if (!out) return;
    out->hash_calls      = g_stats.hash_calls;
    out->hash_collisions = g_stats.hash_collisions;
}

void path_hash_reset_stats(void)
{
    g_stats.hash_calls = 0;
    g_stats.hash_collisions = 0;
}
