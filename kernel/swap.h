#ifndef SWAP_H
#define SWAP_H

#include "stdint.h"
#include "disk_manager.h"

#define SWAP_SLOT_FREE    0
#define SWAP_SLOT_USED    1

typedef struct {
    uint32_t *bitmap;       /* Bitmap of free/used swap slots */
    uint32_t total_slots;   /* Total number of swap slots */
    uint32_t used_slots;    /* Number of used slots */
    uint32_t slot_size;     /* Size of each slot in bytes (4096) */
    disk_info_t *disk;      /* Backing disk device */
    uint64_t start_lba;     /* Starting LBA on disk */
} swap_space_t;

void swap_init(void);
int swap_create(disk_info_t *disk, uint64_t start_lba, uint32_t num_slots);
int swap_read_page(uint32_t slot, void *buf);
int swap_write_page(uint32_t slot, const void *buf);
uint32_t swap_alloc_slot(void);
void swap_free_slot(uint32_t slot);
uint32_t swap_get_free_count(void);
uint32_t swap_get_total_count(void);

#endif
