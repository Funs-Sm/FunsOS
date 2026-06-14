#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include "stdint.h"

#define MULTIBOOT_HEADER_MAGIC      0x1BADB002
#define MULTIBOOT_HEADER_FLAGS      0x00000003
#define MULTIBOOT_HEADER_CHECKSUM   -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS)

#define MULTIBOOT_INFO_MEMORY       0x00000001
#define MULTIBOOT_INFO_BOOTDEV      0x00000002
#define MULTIBOOT_INFO_CMDLINE      0x00000004
#define MULTIBOOT_INFO_MODS         0x00000008
#define MULTIBOOT_INFO_SYMS         0x00000010
#define MULTIBOOT_INFO_MMAP         0x00000040
#define MULTIBOOT_INFO_VBE          0x00000200

typedef struct {
    uint32_t size;
    uint32_t base_addr_low;
    uint32_t base_addr_high;
    uint32_t length_low;
    uint32_t length_high;
    uint32_t type;
} __attribute__((packed)) mmap_entry_t;

typedef struct {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
} __attribute__((packed)) multiboot_info_t;

#endif
