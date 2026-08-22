/*
 * kernel/pgtable.h - low-level page-table helpers for the FunsCore
 * bootstrap stage, and the page-table walker used by /proc/pagetable
 * introspection and the `cmd_pt` shell command.
 *
 * The page table layout is the standard 32-bit x86 two-level:
 *
 *   PDE (Page Directory Entry)     PTE (Page Table Entry)
 *   ------------------             ------------------
 *   |  bit 0: P   |                |  bit 0: P   |
 *   |  bit 1: R/W |                |  bit 1: R/W |
 *   |  bit 2: U/S |                |  bit 2: U/S |
 *   |  bit 5: A   |                |  bit 3: PWT |
 *   |  bit 6: D   |                |  bit 4: PCD |
 *   | bit 7: PS   |                |  bit 5: A   |
 *   | bits 12-31: PFN              |  bit 6: D   |
 *                                 |  bit 7: PAT |
 *                                 | bits 12-31: PFN |
 *
 * We use the same notation as the Intel/AMD manuals.  In addition to
 * the layout, the file exposes:
 *   - pgtable_walk():    iterate over a virtual address space and call
 *                        a callback for each mapped leaf page.
 *   - pgtable_dump():    human-readable dump for the shell command.
 *   - pgtable_stats():   counts of PDE/PTE entries, free vs mapped.
 */
#ifndef KERNEL_PGTABLE_H
#define KERNEL_PGTABLE_H

#include "stdint.h"
#include "stdbool.h"
#include "stddef.h"

#define PGT_PRESENT     0x001u
#define PGT_RW          0x002u
#define PGT_USER        0x004u
#define PGT_A           0x020u
#define PGT_D           0x040u
#define PGT_PS_4M       0x080u
#define PGT_GLOBAL      0x100u

typedef struct {
    uint32_t total_pd_entries;
    uint32_t total_pt_entries;
    uint32_t mapped_pt_entries;
    uint32_t huge_pages;       /* 4 MiB pages */
    uint32_t kernel_entries;
    uint32_t user_entries;
} pgtable_stats_t;

/* Page-table walker callback.  Returning non-zero stops the walk and
 * surfaces the value back to pgtable_walk. */
typedef int (*pgtable_walk_fn)(uint32_t vaddr, uint32_t pte, void *ctx);

int pgtable_walk(uint32_t *page_dir, uint32_t start, uint32_t end,
                 pgtable_walk_fn cb, void *ctx);

/* Read the kernel's current page directory's CR3; returns 0 on a
 * single-CPU build, the real value once MM is plumbed in. */
uint32_t pgtable_current_pdbr(void);

void pgtable_stats(pgtable_stats_t *out);
void pgtable_reset_stats(void);

/* Format a single PDE/PTE into a 6-char flag string like "PWU.A."
 * for `cmd_pt` output.  Returns the number of bytes written. */
int pgtable_format_flags(uint32_t entry, char *out, uint32_t out_size);

#endif /* KERNEL_PGTABLE_H */
