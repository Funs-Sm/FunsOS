#ifndef TTM_H
#define TTM_H

#include "stdint.h"

/* TTM (Translation Table Maps) - GPU memory manager.
 *
 * TTM is a Linux kernel framework that manages different types of
 * memory backing for GPUs: VRAM, GART (Graphics Address Remapping
 * Table), and system memory. It provides:
 *   - Multiple memory "domains" (TT_PL_SYSTEM, TT_PL_TT, TT_PL_VRAM)
 *   - Object handles with refcounting
 *   - LRU eviction of buffer objects
 *   - Fence-based synchronization
 *
 * This implementation provides the core allocator:
 *   - BO (buffer object) handle allocation / free
 *   - Domain assignment (TTM_PL_SYSTEM / TTM_PL_VRAM)
 *   - First-fit allocation in VRAM
 *   - BO validation / pinning for command submission
 */

#define TTM_MAX_BOS          1024
#define TTM_VRAM_PAGES       16384        /* 64 MB in 4 KB pages */
#define TTM_GART_PAGES       1024         /* 4 MB GART aperture */
#define TTM_PAGE_SIZE        4096

/* Placement domains */
typedef enum {
    TTM_PL_SYSTEM = 0,    /* System memory (CPU accessible) */
    TTM_PL_TT      = 1,   /* GART / aperture mapped */
    TTM_PL_VRAM    = 2,   /* On-board video RAM */
    TTM_PL_MAX     = 3,
} ttm_domain_t;

/* Fence flags */
#define TTM_FENCE_NO_WAIT    0
#define TTM_FENCE_WAIT       1

/* Buffer object flags */
#define TTM_BO_FLAG_FIXED    (1 << 0)  /* cannot be evicted */
#define TTM_BO_FLAG_MAPPED   (1 << 1)  /* has CPU mapping */

typedef struct {
    uint32_t id;
    uint32_t page_count;
    uint32_t vram_offset;       /* offset in VRAM if TTM_PL_VRAM */
    uint32_t gart_offset;       /* offset in GART if TTM_PL_TT */
    uint32_t flags;
    ttm_domain_t domain;
    uint64_t refcount;
    /* Eviction metadata. */
    uint64_t last_used_seq;
    /* CPU mapping. */
    void    *cpu_virt;
    /* Stats. */
    uint64_t evict_count;
    uint64_t validate_count;
} ttm_bo_t;

typedef struct {
    uint8_t  vram[TTM_VRAM_PAGES * TTM_PAGE_SIZE / 16]; /* compact bitmap */
    /* Use a flat bitmap: 1 page = 1 bit. For 16384 pages = 2048 bytes.
     * For demonstration we use a smaller bitmap allocation pattern. */
    uint8_t *vram_bitmap;
    uint32_t vram_total_pages;
    uint32_t vram_free_pages;
    uint32_t vram_alloc_alignment;
    uint8_t *gart_bitmap;
    uint32_t gart_total_pages;
    uint32_t gart_free_pages;
    /* Fence sequence counter for LRU eviction. */
    uint64_t fence_seq;
    /* BOs. */
    ttm_bo_t bos[TTM_MAX_BOS];
    int      n_bos;
    int      inited;
    /* Stats. */
    uint64_t total_allocs;
    uint64_t total_evictions;
    uint64_t total_validations;
} ttm_man_t;

int  ttm_init(ttm_man_t *m);
void ttm_destroy(ttm_man_t *m);
int  ttm_bo_create(ttm_man_t *m, uint32_t size, ttm_domain_t domain,
                   uint32_t *out_id);
int  ttm_bo_free(ttm_man_t *m, uint32_t id);
int  ttm_bo_pin(ttm_man_t *m, uint32_t id);
int  ttm_bo_unpin(ttm_man_t *m, uint32_t id);
int  ttm_bo_validate(ttm_man_t *m, uint32_t id);
int  ttm_bo_move(ttm_man_t *m, uint32_t id, ttm_domain_t new_domain);
int  ttm_evict_lru(ttm_man_t *m);
const ttm_bo_t *ttm_bo_get(ttm_man_t *m, uint32_t id);

#endif