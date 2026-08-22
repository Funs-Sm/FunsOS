/*
 * kernel/pgtable.c - page-table walker and stats.
 *
 * The walker takes a page directory (top-level PDE array of 1024
 * uint32_t entries) and iterates over the inclusive [start, end)
 * virtual range, calling the supplied callback for every leaf page
 * actually mapped.  Unmapped regions are silently skipped (zero PDE,
 * zero PTE).
 *
 * Stats are accumulated per call; reset between runs via
 * pgtable_reset_stats().
 */
#include "pgtable.h"
#include "string.h"
#include "stdio.h"
#include "klog.h"

static pgtable_stats_t g_stats;

static inline uint32_t pd_index(uint32_t v) { return (v >> 22) & 0x3FFu; }
static inline uint32_t pt_index(uint32_t v) { return (v >> 12) & 0x3FFu; }

int pgtable_walk(uint32_t *page_dir, uint32_t start, uint32_t end,
                 pgtable_walk_fn cb, void *ctx)
{
    if (!page_dir || end <= start) return -1;
    if (start & 0xFFFu) start &= ~0xFFFu;
    end = (end + 0xFFFu) & ~0xFFFu;

    g_stats.total_pd_entries = 0;

    int rc = 0;
    for (uint32_t va = start; va < end; va += 0x40000000u) {
        uint32_t pdi = pd_index(va);
        uint32_t pde = page_dir[pdi];
        g_stats.total_pd_entries++;
        if (!(pde & PGT_PRESENT)) continue;
        g_stats.kernel_entries++;
        if (pde & PGT_USER) g_stats.kernel_entries--, g_stats.user_entries++;

        if (pde & PGT_PS_4M) {
            g_stats.huge_pages++;
            g_stats.mapped_pt_entries++;
            uint32_t pf_base = pde & 0xFFC00000u;
            for (uint32_t off = 0; off < 0x400000u; off += 0x1000u) {
                uint32_t leaf = pf_base + off | (pde & 0x3Fu);
                int r = cb(va + off, leaf, ctx);
                if (r) { rc = r; goto out; }
            }
            continue;
        }

        uint32_t *pt = (uint32_t *)(pde & 0xFFFFF000u);
        if (!pt) continue;
        g_stats.total_pt_entries += 1024;
        for (uint32_t pti = 0; pti < 1024u; pti++) {
            uint32_t pte = pt[pti];
            g_stats.total_pt_entries++;
            if (!(pte & PGT_PRESENT)) continue;
            g_stats.mapped_pt_entries++;
            uint32_t page_va = (pdi << 22) | (pti << 12);
            if (page_va < start || page_va >= end) continue;
            int r = cb(page_va, pte, ctx);
            if (r) { rc = r; goto out; }
        }
    }
out:
    return rc;
}

uint32_t pgtable_current_pdbr(void)
{
    /* Single-CPU boot: real CR3 reading requires MM plumbing.  For
     * smoke tests we return 0 so callers fall back to a known-good
     * synthetic page dir. */
    return 0;
}

void pgtable_stats(pgtable_stats_t *out)
{
    if (!out) return;
    *out = g_stats;
}

void pgtable_reset_stats(void)
{
    memset(&g_stats, 0, sizeof(g_stats));
}

int pgtable_format_flags(uint32_t entry, char *out, uint32_t out_size)
{
    if (!out || out_size < 8) return 0;
    out[0] = (entry & PGT_PRESENT) ? 'P' : '.';
    out[1] = (entry & PGT_RW)      ? 'W' : '.';
    out[2] = (entry & PGT_USER)    ? 'U' : 'K';
    out[3] = (entry & PGT_A)       ? 'A' : '.';
    out[4] = (entry & PGT_D)       ? 'D' : '.';
    if (entry & PGT_PS_4M) {
        out[5] = 'H'; out[6] = 0;
    } else if (entry & PGT_GLOBAL) {
        out[5] = 'G'; out[6] = 0;
    } else {
        out[5] = '.'; out[6] = 0;
    }
    return 6;
}
