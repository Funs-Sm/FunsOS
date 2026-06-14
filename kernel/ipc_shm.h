#ifndef IPC_SHM_H
#define IPC_SHM_H

#include "stdint.h"
#include "kernel_types.h"

#define SHM_RDONLY 0x01
#define SHM_RND 0x02

typedef struct {
    int key;
    uint32_t size;
    uint32_t *phys_pages;
    uint32_t page_count;
    int ref_count;
    uint32_t flags;
    pid_t owner;
} shm_region_t;

void shm_init(void);
int shm_create(uint32_t size, int flags);
uint32_t shm_attach(int key, page_directory_t *dir);
void shm_detach(int key, uint32_t vaddr, page_directory_t *dir);
shm_region_t *shm_get(int key);

#endif
