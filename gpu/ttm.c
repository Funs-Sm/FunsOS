/* ttm.c - GPU memory manager (TTM-style).
 *
 * Provides buffer-object allocation across multiple GPU memory domains:
 *   - System memory (CPU-only backing)
 *   - VRAM (on-board video RAM, fast GPU access)
 *   - GART (Graphics Address Remapping Table, slower than VRAM)
 *
 * The manager supports:
 *   - First-fit allocation in VRAM and GART
 *   - Reference counting
 *   - Pinning (for buffers that cannot be evicted)
 *   - LRU eviction of unpinned BOs under memory pressure
 *   - Cross-domain BO moves
 */

#include "ttm.h"
#include "kheap.h"
#include "string.h"

/* Bitmap helpers. */
static int bitmap_test(const uint8_t *bm, uint32_t idx) {
    return (bm[idx >> 3] >> (idx & 7)) & 1;
}
static void bitmap_set(uint8_t *bm, uint32_t idx) {
    bm[idx >> 3] |= (uint8_t)(1u << (idx & 7));
}
static void bitmap_clear(uint8_t *bm, uint32_t idx) {
    bm[idx >> 3] &= (uint8_t)~(1u << (idx & 7));
}
static int bitmap_find_first_clear(const uint8_t *bm, uint32_t size) {
    for (uint32_t i = 0; i < size; i++) {
        if (!bitmap_test(bm, i)) return (int)i;
    }
    return -1;
}
static int bitmap_find_contiguous(const uint8_t *bm, uint32_t size,
                                   uint32_t count)
{
    uint32_t run = 0;
    uint32_t start = 0;
    for (uint32_t i = 0; i < size; i++) {
        if (!bitmap_test(bm, i)) {
            if (run == 0) start = i;
            run++;
            if (run == count) return (int)start;
        } else {
            run = 0;
        }
    }
    return -1;
}
static void bitmap_set_range(uint8_t *bm, uint32_t start, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) bitmap_set(bm, start + i);
}
static void bitmap_clear_range(uint8_t *bm, uint32_t start, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) bitmap_clear(bm, start + i);
}

/* Allocate pages in a domain. Returns the page offset (start of the
 * contiguous run), or -1 on failure. */
static int alloc_pages_in_domain(ttm_man_t *m, ttm_domain_t d, uint32_t count) {
    if (d == TTM_PL_VRAM) {
        int off = bitmap_find_contiguous(m->vram_bitmap, m->vram_total_pages, count);
        if (off < 0) return -1;
        bitmap_set_range(m->vram_bitmap, (uint32_t)off, count);
        m->vram_free_pages -= count;
        return off;
    } else if (d == TTM_PL_TT) {
        int off = bitmap_find_contiguous(m->gart_bitmap, m->gart_total_pages, count);
        if (off < 0) return -1;
        bitmap_set_range(m->gart_bitmap, (uint32_t)off, count);
        m->gart_free_pages -= count;
        return off;
    } else {
        return 0;  /* system memory - nothing to reserve */
    }
}

static void free_pages_in_domain(ttm_man_t *m, ttm_domain_t d, uint32_t off,
                                  uint32_t count)
{
    if (d == TTM_PL_VRAM) {
        bitmap_clear_range(m->vram_bitmap, off, count);
        m->vram_free_pages += count;
    } else if (d == TTM_PL_TT) {
        bitmap_clear_range(m->gart_bitmap, off, count);
        m->gart_free_pages += count;
    }
}

int ttm_init(ttm_man_t *m) {
    if (!m) return -1;
    memset(m, 0, sizeof(*m));
    m->vram_total_pages = TTM_VRAM_PAGES;
    m->vram_free_pages  = TTM_VRAM_PAGES;
    m->vram_alloc_alignment = 1;
    m->gart_total_pages = TTM_GART_PAGES;
    m->gart_free_pages  = TTM_GART_PAGES;

    uint32_t vram_bm_size = (TTM_VRAM_PAGES + 7) / 8;
    uint32_t gart_bm_size = (TTM_GART_PAGES + 7) / 8;
    m->vram_bitmap = (uint8_t *)kmalloc(vram_bm_size);
    m->gart_bitmap = (uint8_t *)kmalloc(gart_bm_size);
    if (!m->vram_bitmap || !m->gart_bitmap) return -1;
    memset(m->vram_bitmap, 0, vram_bm_size);
    memset(m->gart_bitmap, 0, gart_bm_size);

    m->n_bos = 0;
    m->fence_seq = 1;
    m->inited = 1;
    return 0;
}

void ttm_destroy(ttm_man_t *m) {
    if (!m || !m->inited) return;
    if (m->vram_bitmap) kfree(m->vram_bitmap);
    if (m->gart_bitmap) kfree(m->gart_bitmap);
    /* Free BO CPU mappings. */
    for (int i = 0; i < m->n_bos; i++) {
        if (m->bos[i].cpu_virt) kfree(m->bos[i].cpu_virt);
    }
    m->inited = 0;
}

int ttm_bo_create(ttm_man_t *m, uint32_t size, ttm_domain_t domain,
                   uint32_t *out_id)
{
    if (!m || !m->inited) return -1;
    if (m->n_bos >= TTM_MAX_BOS) return -1;
    if (size == 0) return -1;

    uint32_t pages = (size + TTM_PAGE_SIZE - 1) / TTM_PAGE_SIZE;
    if (pages == 0) pages = 1;

    /* VRAM placement may need eviction. */
    int off = -1;
    while (off < 0 && domain == TTM_PL_VRAM) {
        off = alloc_pages_in_domain(m, domain, pages);
        if (off < 0) {
            /* Try to evict an unpinned BO. */
            if (ttm_evict_lru(m) != 0) break;
        }
    }
    if (off < 0 && domain != TTM_PL_SYSTEM) {
        /* Fallback to system memory. */
        domain = TTM_PL_SYSTEM;
    }
    if (domain == TTM_PL_VRAM || domain == TTM_PL_TT) {
        if (off < 0) return -1;
    }

    uint32_t id = (uint32_t)m->n_bos;
    ttm_bo_t *bo = &m->bos[id];
    memset(bo, 0, sizeof(*bo));
    bo->id = id;
    bo->page_count = pages;
    bo->domain = domain;
    bo->refcount = 1;
    bo->last_used_seq = m->fence_seq++;
    bo->flags = 0;
    bo->vram_offset = (domain == TTM_PL_VRAM) ? (uint32_t)off * TTM_PAGE_SIZE : 0;
    bo->gart_offset = (domain == TTM_PL_TT)   ? (uint32_t)off * TTM_PAGE_SIZE : 0;
    if (domain == TTM_PL_SYSTEM) {
        bo->cpu_virt = kmalloc(pages * TTM_PAGE_SIZE);
        if (!bo->cpu_virt) return -1;
    }
    m->n_bos++;
    m->total_allocs++;
    if (out_id) *out_id = id;
    return 0;
}

int ttm_bo_free(ttm_man_t *m, uint32_t id) {
    if (!m || id >= (uint32_t)m->n_bos) return -1;
    ttm_bo_t *bo = &m->bos[id];
    if (bo->refcount > 1) {
        bo->refcount--;
        return 0;
    }
    /* Free pages in original domain. */
    if (bo->domain == TTM_PL_VRAM) {
        free_pages_in_domain(m, TTM_PL_VRAM,
                              bo->vram_offset / TTM_PAGE_SIZE, bo->page_count);
    } else if (bo->domain == TTM_PL_TT) {
        free_pages_in_domain(m, TTM_PL_TT,
                              bo->gart_offset / TTM_PAGE_SIZE, bo->page_count);
    }
    if (bo->cpu_virt) {
        kfree(bo->cpu_virt);
        bo->cpu_virt = (void *)0;
    }
    memset(bo, 0, sizeof(*bo));
    return 0;
}

int ttm_bo_pin(ttm_man_t *m, uint32_t id) {
    if (!m || id >= (uint32_t)m->n_bos) return -1;
    m->bos[id].flags |= TTM_BO_FLAG_FIXED;
    return 0;
}

int ttm_bo_unpin(ttm_man_t *m, uint32_t id) {
    if (!m || id >= (uint32_t)m->n_bos) return -1;
    m->bos[id].flags &= ~TTM_BO_FLAG_FIXED;
    return 0;
}

int ttm_bo_validate(ttm_man_t *m, uint32_t id) {
    if (!m || id >= (uint32_t)m->n_bos) return -1;
    m->bos[id].last_used_seq = m->fence_seq++;
    m->bos[id].validate_count++;
    m->total_validations++;
    return 0;
}

int ttm_bo_move(ttm_man_t *m, uint32_t id, ttm_domain_t new_domain) {
    if (!m || id >= (uint32_t)m->n_bos) return -1;
    ttm_bo_t *bo = &m->bos[id];
    if (bo->domain == new_domain) return 0;
    /* Free pages in current domain. */
    if (bo->domain == TTM_PL_VRAM) {
        free_pages_in_domain(m, TTM_PL_VRAM,
                              bo->vram_offset / TTM_PAGE_SIZE, bo->page_count);
    } else if (bo->domain == TTM_PL_TT) {
        free_pages_in_domain(m, TTM_PL_TT,
                              bo->gart_offset / TTM_PAGE_SIZE, bo->page_count);
    }
    /* Allocate pages in new domain. */
    if (new_domain != TTM_PL_SYSTEM) {
        int off = alloc_pages_in_domain(m, new_domain, bo->page_count);
        if (off < 0) return -1;
        if (new_domain == TTM_PL_VRAM)
            bo->vram_offset = (uint32_t)off * TTM_PAGE_SIZE;
        else
            bo->gart_offset = (uint32_t)off * TTM_PAGE_SIZE;
    } else {
        if (!bo->cpu_virt) {
            bo->cpu_virt = kmalloc(bo->page_count * TTM_PAGE_SIZE);
            if (!bo->cpu_virt) return -1;
        }
    }
    bo->domain = new_domain;
    bo->flags &= ~TTM_BO_FLAG_FIXED;
    return 0;
}

int ttm_evict_lru(ttm_man_t *m) {
    if (!m || !m->inited) return -1;
    /* Find the least-recently-used, unpinned BO. */
    uint64_t min_seq = ~(uint64_t)0;
    int victim = -1;
    for (int i = 0; i < m->n_bos; i++) {
        ttm_bo_t *bo = &m->bos[i];
        if (!(bo->flags & TTM_BO_FLAG_FIXED) && bo->refcount == 1
            && bo->last_used_seq < min_seq && bo->domain != TTM_PL_SYSTEM) {
            min_seq = bo->last_used_seq;
            victim = i;
        }
    }
    if (victim < 0) return -1;
    /* Move victim to system memory. */
    if (ttm_bo_move(m, (uint32_t)victim, TTM_PL_SYSTEM) != 0) return -1;
    m->bos[victim].evict_count++;
    m->total_evictions++;
    return 0;
}

const ttm_bo_t *ttm_bo_get(ttm_man_t *m, uint32_t id) {
    if (!m || id >= (uint32_t)m->n_bos) return (const ttm_bo_t *)0;
    return &m->bos[id];
}