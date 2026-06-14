#include "swap.h"
#include "kheap.h"
#include "string.h"
#include "panic.h"

#define BITS_PER_UINT32 32
#define SECTORS_PER_PAGE 8  /* 4096 / 512 = 8 sectors per page */

static swap_space_t swap_space;

void swap_init(void) {
    swap_space.bitmap = NULL;
    swap_space.total_slots = 0;
    swap_space.used_slots = 0;
    swap_space.slot_size = 4096;
    swap_space.disk = NULL;
    swap_space.start_lba = 0;
}

int swap_create(disk_info_t *disk, uint64_t start_lba, uint32_t num_slots) {
    if (!disk || num_slots == 0) return -1;

    uint32_t bitmap_size = (num_slots + BITS_PER_UINT32 - 1) / BITS_PER_UINT32;
    uint32_t *bitmap = (uint32_t *)kcalloc(bitmap_size, sizeof(uint32_t));
    if (!bitmap) return -1;

    /* Mark all slots as free (0) - kcalloc already zeros */
    swap_space.bitmap = bitmap;
    swap_space.total_slots = num_slots;
    swap_space.used_slots = 0;
    swap_space.slot_size = 4096;
    swap_space.disk = disk;
    swap_space.start_lba = start_lba;

    return 0;
}

int swap_read_page(uint32_t slot, void *buf) {
    if (slot >= swap_space.total_slots) return -1;
    if (!swap_space.disk) return -1;
    if (!swap_space.disk->read_sectors) return -1;

    uint64_t lba = swap_space.start_lba + (uint64_t)slot * SECTORS_PER_PAGE;
    return swap_space.disk->read_sectors(swap_space.disk, lba, SECTORS_PER_PAGE, buf);
}

int swap_write_page(uint32_t slot, const void *buf) {
    if (slot >= swap_space.total_slots) return -1;
    if (!swap_space.disk) return -1;
    if (!swap_space.disk->write_sectors) return -1;

    uint64_t lba = swap_space.start_lba + (uint64_t)slot * SECTORS_PER_PAGE;
    return swap_space.disk->write_sectors(swap_space.disk, lba, SECTORS_PER_PAGE, buf);
}

uint32_t swap_alloc_slot(void) {
    if (!swap_space.bitmap) return (uint32_t)-1;

    uint32_t bitmap_size = (swap_space.total_slots + BITS_PER_UINT32 - 1) / BITS_PER_UINT32;
    for (uint32_t i = 0; i < bitmap_size; i++) {
        if (swap_space.bitmap[i] != 0xFFFFFFFF) {
            for (uint32_t j = 0; j < BITS_PER_UINT32; j++) {
                uint32_t index = i * BITS_PER_UINT32 + j;
                if (index >= swap_space.total_slots) return (uint32_t)-1;
                if (!(swap_space.bitmap[i] & (1 << j))) {
                    swap_space.bitmap[i] |= (1 << j);
                    swap_space.used_slots++;
                    return index;
                }
            }
        }
    }
    return (uint32_t)-1;
}

void swap_free_slot(uint32_t slot) {
    if (slot >= swap_space.total_slots) return;
    if (!swap_space.bitmap) return;

    uint32_t i = slot / BITS_PER_UINT32;
    uint32_t j = slot % BITS_PER_UINT32;

    if (swap_space.bitmap[i] & (1 << j)) {
        swap_space.bitmap[i] &= ~(1 << j);
        if (swap_space.used_slots > 0) {
            swap_space.used_slots--;
        }
    }
}

uint32_t swap_get_free_count(void) {
    return swap_space.total_slots - swap_space.used_slots;
}

uint32_t swap_get_total_count(void) {
    return swap_space.total_slots;
}
