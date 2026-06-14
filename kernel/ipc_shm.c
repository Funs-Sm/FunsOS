#include "ipc_shm.h"
#include "pmm.h"
#include "vmm.h"
#include "kheap.h"
#include "sync.h"
#include "process.h"
#include "string.h"

#define SHM_MAX_REGIONS 256

static shm_region_t *shm_regions[SHM_MAX_REGIONS];
static mutex_t shm_lock;
static int shm_key_counter = 1;

void shm_init(void) {
    mutex_init(&shm_lock);
    for (int i = 0; i < SHM_MAX_REGIONS; i++) {
        shm_regions[i] = 0;
    }
    shm_key_counter = 1;
}

int shm_create(uint32_t size, int flags) {
    mutex_lock(&shm_lock);

    shm_region_t *region = (shm_region_t *)kmalloc(sizeof(shm_region_t));
    if (!region) {
        mutex_unlock(&shm_lock);
        return -1;
    }

    uint32_t page_count = (size + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
    region->phys_pages = (uint32_t *)kmalloc(page_count * sizeof(uint32_t));
    if (!region->phys_pages) {
        kfree(region);
        mutex_unlock(&shm_lock);
        return -1;
    }

    for (uint32_t i = 0; i < page_count; i++) {
        void *phys = pmm_alloc_page();
        if (!phys) {
            for (uint32_t j = 0; j < i; j++) {
                pmm_free_page((void *)region->phys_pages[j]);
            }
            kfree(region->phys_pages);
            kfree(region);
            mutex_unlock(&shm_lock);
            return -1;
        }
        region->phys_pages[i] = (uint32_t)phys;
    }

    region->key = shm_key_counter++;
    region->size = size;
    region->page_count = page_count;
    region->ref_count = 1;
    region->flags = (uint32_t)flags;
    region->owner = 0;

    int slot = -1;
    for (int i = 0; i < SHM_MAX_REGIONS; i++) {
        if (shm_regions[i] == 0) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        for (uint32_t i = 0; i < page_count; i++) {
            pmm_free_page((void *)region->phys_pages[i]);
        }
        kfree(region->phys_pages);
        kfree(region);
        mutex_unlock(&shm_lock);
        return -1;
    }

    shm_regions[slot] = region;
    int key = region->key;

    mutex_unlock(&shm_lock);
    return key;
}

uint32_t shm_attach(int key, page_directory_t *dir) {
    mutex_lock(&shm_lock);

    shm_region_t *region = 0;
    for (int i = 0; i < SHM_MAX_REGIONS; i++) {
        if (shm_regions[i] && shm_regions[i]->key == key) {
            region = shm_regions[i];
            break;
        }
    }

    if (!region) {
        mutex_unlock(&shm_lock);
        return 0;
    }

    uint32_t vaddr = 0x40000000;
    for (int attempt = 0; attempt < 256; attempt++) {
        uint32_t virt = vaddr + attempt * 0x1000000;
        int available = 1;
        for (uint32_t p = 0; p < region->page_count; p++) {
            if (vmm_get_physical(dir, virt + p * PMM_PAGE_SIZE) != 0) {
                available = 0;
                break;
            }
        }
        if (available) {
            vaddr = virt;
            break;
        }
    }

    for (uint32_t i = 0; i < region->page_count; i++) {
        uint32_t flags = VMM_PAGE_PRESENT | VMM_PAGE_USER;
        if (!(region->flags & SHM_RDONLY)) {
            flags |= VMM_PAGE_WRITABLE;
        }
        vmm_map_page(dir, vaddr + i * PMM_PAGE_SIZE, region->phys_pages[i], flags);
    }

    region->ref_count++;

    mutex_unlock(&shm_lock);
    return vaddr;
}

void shm_detach(int key, uint32_t vaddr, page_directory_t *dir) {
    mutex_lock(&shm_lock);

    shm_region_t *region = 0;
    int slot = -1;
    for (int i = 0; i < SHM_MAX_REGIONS; i++) {
        if (shm_regions[i] && shm_regions[i]->key == key) {
            region = shm_regions[i];
            slot = i;
            break;
        }
    }

    if (!region) {
        mutex_unlock(&shm_lock);
        return;
    }

    for (uint32_t i = 0; i < region->page_count; i++) {
        vmm_unmap_page(dir, vaddr + i * PMM_PAGE_SIZE);
    }

    region->ref_count--;

    if (region->ref_count <= 0) {
        for (uint32_t i = 0; i < region->page_count; i++) {
            pmm_free_page((void *)region->phys_pages[i]);
        }
        kfree(region->phys_pages);
        kfree(region);
        shm_regions[slot] = 0;
    }

    mutex_unlock(&shm_lock);
}

shm_region_t *shm_get(int key) {
    for (int i = 0; i < SHM_MAX_REGIONS; i++) {
        if (shm_regions[i] && shm_regions[i]->key == key) {
            return shm_regions[i];
        }
    }
    return 0;
}
