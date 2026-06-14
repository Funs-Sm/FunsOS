#include "initrd.h"
#include "string.h"

#define INITRD_MAGIC 0xBFDA1E00

static uint8_t *initrd_start = NULL;
static uint32_t initrd_size = 0;
static initrd_header_t *initrd_header = NULL;
static initrd_file_entry_t *initrd_entries = NULL;

void initrd_init(uint32_t start_addr, uint32_t size) {
    initrd_start = (uint8_t *)start_addr;
    initrd_size = size;

    if (initrd_start == NULL || initrd_size == 0) {
        initrd_header = NULL;
        initrd_entries = NULL;
        return;
    }

    initrd_header = (initrd_header_t *)initrd_start;

    if (initrd_header->magic != INITRD_MAGIC) {
        initrd_header = NULL;
        initrd_entries = NULL;
        return;
    }

    initrd_entries = (initrd_file_entry_t *)(initrd_start + sizeof(initrd_header_t));
}

int initrd_read_file(const char *name, void *buf, uint32_t *size) {
    if (!initrd_header || !initrd_entries || !name || !buf || !size) {
        return -1;
    }

    uint32_t i;
    for (i = 0; i < initrd_header->file_count; i++) {
        if (strcmp(initrd_entries[i].name, name) == 0) {
            uint8_t *src = initrd_start + initrd_entries[i].offset;
            memcpy(buf, src, initrd_entries[i].length);
            *size = initrd_entries[i].length;
            return 0;
        }
    }

    return -1;
}

initrd_file_entry_t *initrd_list_files(uint32_t *count) {
    if (!initrd_header || !initrd_entries || !count) {
        if (count) *count = 0;
        return NULL;
    }

    *count = initrd_header->file_count;
    return initrd_entries;
}
