#ifndef MMAP_H
#define MMAP_H

#include "stdint.h"

#define PROT_READ   0x01
#define PROT_WRITE  0x02
#define PROT_EXEC   0x04
#define PROT_NONE   0x00

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20
#define MAP_FAILED    ((void *)0)

typedef struct mmap_region {
    uint32_t start_vaddr;
    uint32_t size;
    uint32_t prot;
    uint32_t flags;
    uint32_t inode_num;
    uint64_t file_offset;
    uint32_t pid;
    struct mmap_region *next;
} mmap_region_t;

void mmap_init(void);
void *mmap(void *addr, uint32_t length, uint32_t prot, uint32_t flags, uint32_t fd, uint32_t offset);
int munmap(void *addr, uint32_t length);
int mprotect(void *addr, uint32_t length, uint32_t prot);
int msync(void *addr, uint32_t length, uint32_t flags);
void mmap_handle_page_fault(uint32_t vaddr, uint32_t pid);
void mmap_release_all(uint32_t pid);

#endif
