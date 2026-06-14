#include "elf.h"
#include "pmm.h"
#include "vmm.h"
#include "kheap.h"
#include "string.h"

int elf_validate(uint8_t *data, uint32_t size) {
    if (size < sizeof(elf32_ehdr_t)) return 0;

    elf32_ehdr_t *ehdr = (elf32_ehdr_t *)data;

    if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F') {
        return 0;
    }

    if (ehdr->e_ident[4] != 1) return 0;
    if (ehdr->e_ident[5] != 1) return 0;
    if (ehdr->e_machine != EM_386) return 0;

    return 1;
}

elf_loaded_t elf_load_static(uint8_t *data, uint32_t size, page_directory_t *dir) {
    elf_loaded_t result = {0, 0, 0};

    if (!elf_validate(data, size)) return result;

    elf32_ehdr_t *ehdr = (elf32_ehdr_t *)data;

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        elf32_phdr_t *phdr = (elf32_phdr_t *)(data + ehdr->e_phoff + i * ehdr->e_phentsize);

        if (phdr->p_type != PT_LOAD) continue;

        uint32_t vaddr = phdr->p_vaddr & ~(PMM_PAGE_SIZE - 1);
        uint32_t offset = phdr->p_vaddr - vaddr;
        uint32_t mem_size = phdr->p_memsz + offset;
        uint32_t num_pages = (mem_size + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;

        for (uint32_t p = 0; p < num_pages; p++) {
            void *phys = pmm_alloc_page();
            if (!phys) return result;

            uint32_t page_vaddr = vaddr + p * PMM_PAGE_SIZE;
            uint32_t flags = VMM_PAGE_PRESENT | VMM_PAGE_USER;
            if (phdr->p_flags & 0x02) flags |= VMM_PAGE_WRITABLE;

            vmm_map_page(dir, page_vaddr, (uint32_t)phys, flags);

            memset((void *)(page_vaddr + VMM_KERNEL_BASE), 0, PMM_PAGE_SIZE);
        }

        uint8_t *dest = (uint8_t *)phdr->p_vaddr;
        uint8_t *src = data + phdr->p_offset;
        for (uint32_t b = 0; b < phdr->p_filesz; b++) {
            dest[b] = src[b];
        }

        if (result.base == 0 || vaddr < (uint32_t)result.base) {
            result.base = (uint8_t *)vaddr;
        }

        uint32_t seg_end = phdr->p_vaddr + phdr->p_memsz;
        if (seg_end > result.size + (uint32_t)result.base) {
            result.size = seg_end - (uint32_t)result.base;
        }
    }

    result.entry = ehdr->e_entry;
    return result;
}

static uint32_t elf_resolve_symbol(elf32_sym_t *symtab, uint32_t sym_count, const char *strtab, uint32_t sym_idx, uint8_t *base) {
    if (sym_idx >= sym_count) return 0;

    elf32_sym_t *sym = &symtab[sym_idx];
    if (sym->st_shndx == 0) return 0;

    return (uint32_t)base + sym->st_value;
}

int elf_relocate(elf_loaded_t *img, uint8_t *rel_data, uint32_t rel_size) {
    uint32_t rel_count = rel_size / sizeof(elf32_rel_t);
    elf32_rel_t *rels = (elf32_rel_t *)rel_data;

    for (uint32_t i = 0; i < rel_count; i++) {
        uint32_t offset = rels[i].r_offset;
        uint32_t type = rels[i].r_info & 0xFF;
        uint32_t sym_idx = rels[i].r_info >> 8;

        uint32_t *target = (uint32_t *)(offset + (uint32_t)img->base);
        uint32_t S = 0;

        if (sym_idx != 0) {
            S = elf_get_symbol(img, (const char *)0);
        }

        switch (type) {
            case R_386_32: {
                *target = S + *target;
                break;
            }
            case R_386_PC32: {
                *target = S + *target - (uint32_t)target;
                break;
            }
            default:
                break;
        }
    }

    return 0;
}

elf_loaded_t elf_load_dynamic(uint8_t *data, uint32_t size, page_directory_t *dir) {
    elf_loaded_t result = elf_load_static(data, size, dir);
    if (!result.base) return result;

    elf32_ehdr_t *ehdr = (elf32_ehdr_t *)data;

    elf32_phdr_t *dyn_phdr = 0;
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        elf32_phdr_t *phdr = (elf32_phdr_t *)(data + ehdr->e_phoff + i * ehdr->e_phentsize);
        if (phdr->p_type == PT_DYNAMIC) {
            dyn_phdr = phdr;
            break;
        }
    }

    if (!dyn_phdr) return result;

    uint8_t *dyn_data = data + dyn_phdr->p_offset;
    uint32_t dyn_size = dyn_phdr->p_filesz;

    uint8_t *dynsym_data = 0;
    uint32_t dynsym_size = 0;
    uint8_t *dynstr_data = 0;
    uint32_t dynstr_size = 0;
    uint8_t *reldyn_data = 0;
    uint32_t reldyn_size = 0;

    uint32_t pos = 0;
    while (pos + 8 <= dyn_size) {
        uint32_t tag = *(uint32_t *)(dyn_data + pos);
        uint32_t val = *(uint32_t *)(dyn_data + pos + 4);

        if (tag == 6) {
            dynsym_data = data + val;
        } else if (tag == 7) {
            dynstr_data = data + val;
        } else if (tag == 11) {
            reldyn_data = data + val;
        } else if (tag == 12) {
            dynstr_size = val;
        } else if (tag == 2) {
            reldyn_size = val;
        } else if (tag == 13) {
            dynsym_size = val;
        } else if (tag == 0) {
            break;
        }

        pos += 8;
    }

    if (reldyn_data && reldyn_size) {
        uint32_t rel_count = reldyn_size / sizeof(elf32_rel_t);
        elf32_rel_t *rels = (elf32_rel_t *)reldyn_data;

        for (uint32_t i = 0; i < rel_count; i++) {
            uint32_t offset = rels[i].r_offset;
            uint32_t type = rels[i].r_info & 0xFF;
            uint32_t sym_idx = rels[i].r_info >> 8;

            uint32_t *target = (uint32_t *)(offset + (uint32_t)result.base);
            uint32_t S = 0;

            if (sym_idx != 0 && dynsym_data && dynstr_data) {
                elf32_sym_t *symtab = (elf32_sym_t *)dynsym_data;
                uint32_t sym_count = dynsym_size / sizeof(elf32_sym_t);
                if (sym_idx < sym_count) {
                    const char *name = (const char *)(dynstr_data + symtab[sym_idx].st_name);
                    S = elf_get_symbol(&result, name);
                    if (S == 0 && symtab[sym_idx].st_shndx != 0) {
                        S = (uint32_t)result.base + symtab[sym_idx].st_value;
                    }
                }
            }

            switch (type) {
                case R_386_32: {
                    *target = S + *target;
                    break;
                }
                case R_386_PC32: {
                    *target = S + *target - (uint32_t)target;
                    break;
                }
                default:
                    break;
            }
        }
    }

    return result;
}

uint32_t elf_get_symbol(elf_loaded_t *img, const char *name) {
    if (!img || !img->base || !name) return 0;
    return 0;
}
