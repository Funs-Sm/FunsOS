#ifndef INITRD_H
#define INITRD_H

#include "stdint.h"

/* Initrd header at the start of the initrd image */
typedef struct {
    uint32_t magic;       /* 0xBFDA1E00 */
    uint32_t file_count;  /* Number of files */
} initrd_header_t;

/* File entry in the initrd */
typedef struct {
    char name[128];
    uint32_t offset;      /* Offset from start of initrd */
    uint32_t length;      /* File size in bytes */
} initrd_file_entry_t;

void initrd_init(uint32_t start_addr, uint32_t size);
int initrd_read_file(const char *name, void *buf, uint32_t *size);
initrd_file_entry_t *initrd_list_files(uint32_t *count);

#endif
