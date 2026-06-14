#include "fun_format.h"
#include "../kernel/kheap.h"
#include "../kernel/ksym.h"
#include "../kernel/process.h"
#include "../kernel/sched.h"
#include "../kernel/vmm.h"
#include "../kernel/pmm.h"
#include "../kernel/klog.h"
#include "../lib/string.h"
#include "../lib/stdio.h"
#include "../fs/vfs.h"

/* ================================================================ */
/*  加载的程序描述符                                                    */
/* ================================================================ */

#define FUN_MAX_LOADED  128

typedef struct {
    uint32_t process_id;
    char name[64];
    uint8_t *base;
    uint32_t size;
    uint32_t entry_point;
    int used;
    fun_header_t header;
} fun_loaded_t;

static fun_loaded_t g_loaded[FUN_MAX_LOADED];
static int g_loaded_count = 0;
static char g_error_msg[128] = "";

/* ================================================================ */
/*  初始化                                                            */
/* ================================================================ */

void fun_loader_init(void) {
    int i;
    for (i = 0; i < FUN_MAX_LOADED; i++) {
        g_loaded[i].used = 0;
        g_loaded[i].process_id = 0;
        g_loaded[i].name[0] = '\0';
    }
    g_loaded_count = 0;
    g_error_msg[0] = '\0';
    klog_info("FUN loader initialized (max %d processes)", FUN_MAX_LOADED);
}

/* ================================================================ */
/*  错误处理                                                          */
/* ================================================================ */

static void set_error(const char *msg) {
    int i = 0;
    while (msg[i] && i < 127) {
        g_error_msg[i] = msg[i];
        i++;
    }
    g_error_msg[i] = '\0';
}

const char *fun_get_error(void) {
    return g_error_msg;
}

int fun_get_entry_point(const uint8_t *data, uint32_t *entry) {
    if (!data || !entry) {
        set_error("null parameter");
        return FUN_ERR_ENTRY;
    }
    const fun_header_t *hdr = (const fun_header_t *)data;
    *entry = hdr->entry_point;
    return FUN_ERR_NONE;
}

/* ================================================================ */
/*  校验和                                                            */
/* ================================================================ */

static uint32_t fun_compute_checksum(const uint8_t *data, uint32_t size) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < size; i++) {
        sum = (sum + data[i]) & 0xFFFFFFFF;
    }
    return sum;
}

/* ================================================================ */
/*  格式验证                                                          */
/* ================================================================ */

int fun_validate(const uint8_t *data, uint32_t size) {
    if (!data) {
        set_error("null data");
        return FUN_ERR_MAGIC;
    }
    if (size < sizeof(fun_header_t)) {
        set_error("file too small for header");
        return FUN_ERR_HEADER_SIZE;
    }

    const fun_header_t *hdr = (const fun_header_t *)data;

    /* 检查魔数 */
    if (hdr->magic != FUN_MAGIC) {
        set_error("invalid magic number");
        return FUN_ERR_MAGIC;
    }

    /* 检查版本 */
    if (hdr->version != FUN_VERSION) {
        set_error("unsupported format version");
        return FUN_ERR_VERSION;
    }

    /* 检查文件头大小 */
    if (hdr->header_size < sizeof(fun_header_t)) {
        set_error("invalid header size");
        return FUN_ERR_HEADER_SIZE;
    }

    /* 检查节区数量 */
    if (hdr->section_count == 0 || hdr->section_count > 256) {
        set_error("invalid section count");
        return FUN_ERR_SECTION_COUNT;
    }

    /* 检查节区表是否在范围内 */
    uint32_t section_table_start = hdr->header_size;
    uint32_t section_table_size = (uint32_t)hdr->section_count * sizeof(fun_section_t);
    if (section_table_start + section_table_size > size) {
        set_error("section table exceeds file size");
        return FUN_ERR_SECTION_TABLE;
    }

    /* 验证每个节区 */
    const fun_section_t *sections = (const fun_section_t *)(data + section_table_start);
    for (uint16_t i = 0; i < hdr->section_count; i++) {
        const fun_section_t *sec = &sections[i];

        /* 确保节区名已 NUL 终止 */
        int name_ok = 0;
        for (int j = 0; j < 16; j++) {
            if (sec->name[j] == '\0') { name_ok = 1; break; }
        }
        if (!name_ok) {
            set_error("section name not null-terminated");
            return FUN_ERR_SECTION_TABLE;
        }

        /* 检查文件偏移 + 大小是否在范围内 */
        if (sec->type == FUN_SECTION_BSS) {
            /* BSS 节区没有文件数据 */
            if (sec->raw_size != 0) {
                set_error("BSS section has raw data");
                return FUN_ERR_SECTION_TABLE;
            }
        } else if (sec->raw_size > 0) {
            if (sec->raw_offset < sizeof(fun_header_t) ||
                (uint64_t)sec->raw_offset + sec->raw_size > size) {
                set_error("section data exceeds file size");
                return FUN_ERR_SECTION_TABLE;
            }
        }

        /* 检查虚拟大小 */
        if (sec->virtual_size < sec->raw_size) {
            set_error("virtual size less than raw size");
            return FUN_ERR_SECTION_TABLE;
        }
    }

    /* 检查校验和 */
    /* 保存原始校验和，清零后计算 */
    uint8_t *mutable_data = (uint8_t *)data;
    fun_header_t *mutable_hdr = (fun_header_t *)mutable_data;
    uint32_t saved_checksum = mutable_hdr->checksum;
    mutable_hdr->checksum = 0;
    uint32_t computed = fun_compute_checksum(data, size);
    mutable_hdr->checksum = saved_checksum;

    if (saved_checksum != 0 && computed != saved_checksum) {
        set_error("checksum mismatch");
        return FUN_ERR_CHECKSUM;
    }

    set_error("success");
    return FUN_ERR_NONE;
}

/* ================================================================ */
/*  节区加载                                                          */
/* ================================================================ */

static int fun_load_sections(const uint8_t *data, const fun_header_t *hdr,
                              page_directory_t *page_dir, uint8_t **out_base,
                              uint32_t *out_size) {
    const fun_section_t *sections =
        (const fun_section_t *)(data + hdr->header_size);

    uint32_t min_addr = 0xFFFFFFFF;
    uint32_t max_addr = 0;

    /* 第一遍：确定总地址范围 */
    for (uint16_t i = 0; i < hdr->section_count; i++) {
        const fun_section_t *sec = &sections[i];
        if (sec->virtual_size == 0) continue;
        if (sec->virtual_address < min_addr) {
            min_addr = sec->virtual_address;
        }
        uint32_t end_addr = sec->virtual_address + sec->virtual_size;
        if (end_addr > max_addr) {
            max_addr = end_addr;
        }
    }

    /* 对齐到页边界 */
    min_addr &= ~(PMM_PAGE_SIZE - 1);
    max_addr = (max_addr + PMM_PAGE_SIZE - 1) & ~(PMM_PAGE_SIZE - 1);

    /* 分配物理内存并映射 */
    for (uint32_t addr = min_addr; addr < max_addr; addr += PMM_PAGE_SIZE) {
        void *phys = pmm_alloc_page();
        if (!phys) {
            set_error("failed to allocate physical memory");
            return FUN_ERR_MEMORY;
        }

        uint32_t flags = VMM_PAGE_PRESENT | VMM_PAGE_USER | VMM_PAGE_WRITABLE;
        vmm_map_page(page_dir, addr, (uint32_t)phys, flags);

        /* 零初始化 */
        memset((void *)addr, 0, PMM_PAGE_SIZE);
    }

    /* 第二遍：复制节区数据 */
    for (uint16_t i = 0; i < hdr->section_count; i++) {
        const fun_section_t *sec = &sections[i];

        switch (sec->type) {
        case FUN_SECTION_CODE:
        case FUN_SECTION_DATA:
        case FUN_SECTION_RODATA:
        case FUN_SECTION_RESOURCE:
        case FUN_SECTION_EXPORT:
            /* 复制数据 */
            if (sec->raw_size > 0) {
                uint8_t *dest = (uint8_t *)sec->virtual_address;
                const uint8_t *src = data + sec->raw_offset;
                memcpy(dest, src, sec->raw_size);
            }
            /* 零填充剩余部分 */
            if (sec->virtual_size > sec->raw_size) {
                uint8_t *zero_start = (uint8_t *)sec->virtual_address + sec->raw_size;
                uint32_t zero_len = sec->virtual_size - sec->raw_size;
                memset(zero_start, 0, zero_len);
            }
            break;

        case FUN_SECTION_BSS:
            /* BSS 全部零初始化 */
            memset((void *)sec->virtual_address, 0, sec->virtual_size);
            break;

        default:
            break;
        }

        /* 根据节区标志调整页表保护 */
        if (sec->flags & (FUN_SF_READABLE | FUN_SF_WRITABLE | FUN_SF_EXECUTABLE)) {
            uint32_t sec_start = sec->virtual_address & ~(PMM_PAGE_SIZE - 1);
            uint32_t sec_end = (sec->virtual_address + sec->virtual_size +
                                PMM_PAGE_SIZE - 1) & ~(PMM_PAGE_SIZE - 1);

            for (uint32_t addr = sec_start; addr < sec_end; addr += PMM_PAGE_SIZE) {
                uint32_t flags = VMM_PAGE_PRESENT | VMM_PAGE_USER;
                if (sec->flags & FUN_SF_WRITABLE) flags |= VMM_PAGE_WRITABLE;
                /* CODE 节区通常是只读可执行
                 * 注意：在简单实现中我们保持所有页面可写 */
                uint32_t phys = vmm_get_physical(page_dir, addr);
                if (phys != 0) {
                    vmm_map_page(page_dir, addr, phys, flags);
                }
            }
        }
    }

    *out_base = (uint8_t *)min_addr;
    *out_size = max_addr - min_addr;
    return FUN_ERR_NONE;
}

/* ================================================================ */
/*  重定位                                                           */
/* ================================================================ */

static int fun_apply_relocations(const uint8_t *data, const fun_header_t *hdr,
                                  uint8_t *base) {
    const fun_section_t *sections =
        (const fun_section_t *)(data + hdr->header_size);
    const fun_section_t *reloc_sec = NULL;

    /* 查找重定位节区 */
    for (uint16_t i = 0; i < hdr->section_count; i++) {
        if (sections[i].type == FUN_SECTION_RELOC) {
            reloc_sec = &sections[i];
            break;
        }
    }

    if (!reloc_sec || reloc_sec->raw_size == 0) {
        /* 没有重定位项 - 没问题 */
        return FUN_ERR_NONE;
    }

    uint32_t reloc_count = reloc_sec->raw_size / sizeof(fun_reloc_t);
    if (reloc_count == 0) return FUN_ERR_NONE;

    const fun_reloc_t *relocs = (const fun_reloc_t *)(data + reloc_sec->raw_offset);

    for (uint32_t i = 0; i < reloc_count; i++) {
        const fun_reloc_t *reloc = &relocs[i];
        uint32_t target_addr = reloc->offset;
        uint32_t *target = (uint32_t *)((uint32_t)base + target_addr);

        switch (reloc->type) {
        case FUN_RELOC_ABSOLUTE:
            /* 绝对地址：加上基址 */
            *target = (uint32_t)base + *target;
            break;

        case FUN_RELOC_RELATIVE:
            /* 相对地址：加上基址差 */
            *target = (uint32_t)base + *target - target_addr;
            break;

        case FUN_RELOC_SYMBOL:
            /* 符号重定位：当前未实现 */
            klog_debug("FUN: symbol relocation not yet supported");
            break;

        default:
            klog_warn("FUN: unknown relocation type %d", reloc->type);
            break;
        }
    }

    return FUN_ERR_NONE;
}

/* ================================================================ */
/*  导入解析                                                          */
/* ================================================================ */

static int fun_resolve_imports(const uint8_t *data, const fun_header_t *hdr,
                                uint8_t *base) {
    const fun_section_t *sections =
        (const fun_section_t *)(data + hdr->header_size);
    const fun_section_t *import_sec = NULL;

    /* 查找导入节区 */
    for (uint16_t i = 0; i < hdr->section_count; i++) {
        if (sections[i].type == FUN_SECTION_IMPORT) {
            import_sec = &sections[i];
            break;
        }
    }

    if (!import_sec || import_sec->raw_size == 0) {
        /* 没有导入 - 没问题 */
        return FUN_ERR_NONE;
    }

    uint32_t import_count = import_sec->raw_size / sizeof(fun_import_t);
    if (import_count == 0) return FUN_ERR_NONE;

    const fun_import_t *imports =
        (const fun_import_t *)(data + import_sec->raw_offset);

    int resolved = 0;
    int failed = 0;

    for (uint32_t i = 0; i < import_count; i++) {
        const fun_import_t *imp = &imports[i];

        /* 在符号表中查找函数 */
        uint32_t sym_addr = ksym_lookup(imp->function_name);

        if (sym_addr != 0) {
            /* 写入导入地址 */
            uint32_t *patch = (uint32_t *)((uint32_t)base + imp->import_rva);
            *patch = sym_addr;
            resolved++;
        } else {
            /* 尝试常见的内核符号 */
            klog_warn("FUN: unresolved import '%s' from '%s'",
                      imp->function_name, imp->module_name);
            failed++;
        }
    }

    if (failed > 0 && resolved == 0) {
        set_error("all imports failed to resolve");
        return FUN_ERR_IMPORT;
    }

    klog_debug("FUN: resolved %d/%d imports", resolved, resolved + failed);
    return FUN_ERR_NONE;
}

/* ================================================================ */
/*  从内存加载                                                        */
/* ================================================================ */

int fun_load_from_memory(const uint8_t *data, uint32_t size) {
    int ret = fun_validate(data, size);
    if (ret != FUN_ERR_NONE) return ret;

    const fun_header_t *hdr = (const fun_header_t *)data;

    /* 创建地址空间 */
    page_directory_t *page_dir = vmm_create_address_space();
    if (!page_dir) {
        set_error("failed to create address space");
        return FUN_ERR_MEMORY;
    }

    /* 加载节区 */
    uint8_t *base = NULL;
    uint32_t img_size = 0;
    ret = fun_load_sections(data, hdr, page_dir, &base, &img_size);
    if (ret != FUN_ERR_NONE) {
        vmm_destroy_address_space(page_dir);
        return ret;
    }

    /* 应用重定位 */
    ret = fun_apply_relocations(data, hdr, base);
    if (ret != FUN_ERR_NONE) {
        vmm_destroy_address_space(page_dir);
        return ret;
    }

    /* 解析导入 */
    ret = fun_resolve_imports(data, hdr, base);
    if (ret != FUN_ERR_NONE) {
        vmm_destroy_address_space(page_dir);
        return ret;
    }

    /* 提取名称 */
    char proc_name[32] = "fun_proc";
    const fun_section_t *sections =
        (const fun_section_t *)(data + hdr->header_size);
    for (uint16_t i = 0; i < hdr->section_count; i++) {
        if (sections[i].type == FUN_SECTION_CODE) {
            int j = 0;
            while (j < 15 && j < 16 && sections[i].name[j]) {
                proc_name[j] = sections[i].name[j];
                j++;
            }
            proc_name[j] = '\0';
            break;
        }
    }

    /* 创建进程 */
    /* 注意：process_create 期望 ELF 数据，但这里我们直接操作 PCB
     * 因此使用底层 primitives */
    pcb_t *proc = sched_get_current(); /* 获取当前进程作为模板 */
    if (!proc) {
        set_error("no current process to load from");
        vmm_destroy_address_space(page_dir);
        return FUN_ERR_PROCESS;
    }

    /* 分配用户栈 */
    for (int i = 0; i < 4; i++) {
        uint32_t stack_addr = 0xBFFFF000 - (i + 1) * PMM_PAGE_SIZE;
        void *phys = pmm_alloc_page();
        if (!phys) {
            vmm_destroy_address_space(page_dir);
            set_error("failed to allocate stack");
            return FUN_ERR_MEMORY;
        }
        vmm_map_page(page_dir, stack_addr, (uint32_t)phys,
                     VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE | VMM_PAGE_USER);
    }

    /* 分配内核栈 */
    void *kernel_stack_phys = pmm_alloc_page();
    if (!kernel_stack_phys) {
        vmm_destroy_address_space(page_dir);
        set_error("failed to allocate kernel stack");
        return FUN_ERR_MEMORY;
    }

    /* 使用 process_create 创建进程 */
    /* process_create 需要 ELF 数据，这里用一种简化方式：
     * 直接操作进程表。注意：这必须谨慎使用。 */
    pcb_t *new_proc = process_create(proc_name, (uint8_t *)data, size);
    if (!new_proc) {
        /* process_create 可能因非 ELF 格式失败
         * 备用方案：手动设置 */
        vmm_destroy_address_space(page_dir);
        set_error("process creation failed (not an ELF)");
        return FUN_ERR_PROCESS;
    }

    /* 注册到已加载列表 */
    if (g_loaded_count < FUN_MAX_LOADED) {
        fun_loaded_t *loaded = &g_loaded[g_loaded_count];
        loaded->process_id = new_proc->pid;
        loaded->base = base;
        loaded->size = img_size;
        loaded->entry_point = hdr->entry_point;
        loaded->used = 1;
        loaded->header = *hdr;

        int n = 0;
        while (proc_name[n] && n < 63) {
            loaded->name[n] = proc_name[n];
            n++;
        }
        loaded->name[n] = '\0';

        g_loaded_count++;
    }

    klog_info("FUN: loaded '%s' (pid=%d, entry=0x%x)", proc_name,
              new_proc->pid, hdr->entry_point);
    set_error("success");
    return FUN_ERR_NONE;
}

/* ================================================================ */
/*  从文件加载                                                        */
/* ================================================================ */

int fun_load(const char *path) {
    if (!path) {
        set_error("null path");
        return FUN_ERR_FILE;
    }

    /* 打开文件 */
    file_t *file = NULL;
    if (vfs_open(path, FILE_MODE_READ, &file) != 0) {
        set_error("failed to open file");
        return FUN_ERR_FILE;
    }

    /* 获取文件大小 */
    uint32_t file_size = file->inode->size;
    if (file_size < sizeof(fun_header_t)) {
        vfs_close(file);
        set_error("file too small");
        return FUN_ERR_FILE;
    }

    /* 读取整个文件 */
    uint8_t *buf = (uint8_t *)kmalloc(file_size);
    if (!buf) {
        vfs_close(file);
        set_error("failed to allocate read buffer");
        return FUN_ERR_MEMORY;
    }

    int32_t bytes_read = vfs_read(file, buf, file_size);
    vfs_close(file);

    if (bytes_read <= 0 || (uint32_t)bytes_read < sizeof(fun_header_t)) {
        kfree(buf);
        set_error("failed to read file");
        return FUN_ERR_FILE;
    }

    int ret = fun_load_from_memory(buf, (uint32_t)bytes_read);
    kfree(buf);
    return ret;
}

/* ================================================================ */
/*  执行                                                             */
/* ================================================================ */

int fun_execute(const char *path, int argc, char **argv) {
    (void)argc;
    (void)argv;

    int ret = fun_load(path);
    if (ret != FUN_ERR_NONE) return ret;

    /* 加载后进程已由 process_create 添加到调度器中
     * 它将在调度器选中它时开始执行 */
    klog_info("FUN: executed '%s'", path);
    return FUN_ERR_NONE;
}

/* ================================================================ */
/*  卸载                                                             */
/* ================================================================ */

int fun_unload(uint32_t process_id) {
    for (int i = 0; i < g_loaded_count; i++) {
        if (g_loaded[i].used && g_loaded[i].process_id == process_id) {
            pcb_t *proc = process_get_pcb((pid_t)process_id);
            if (proc) {
                process_exit(0);
            }

            g_loaded[i].used = 0;
            g_loaded[i].base = NULL;
            g_loaded[i].size = 0;

            klog_info("FUN: unloaded pid %d", process_id);
            set_error("success");
            return FUN_ERR_NONE;
        }
    }

    set_error("process not found");
    return FUN_ERR_PROCESS;
}

/* ================================================================ */
/*  列出已加载的程序                                                    */
/* ================================================================ */

void fun_list_loaded(void) {
    klog_info("FUN: %d loaded program(s):", g_loaded_count);
    printf("FUN Loaded Programs:\n");
    printf("  PID   Name                    Entry      Size\n");
    printf("  ----- ----------------------- ---------- ----------\n");

    int shown = 0;
    for (int i = 0; i < g_loaded_count; i++) {
        if (g_loaded[i].used) {
            printf("  %5d %-23s 0x%08X %u\n",
                   g_loaded[i].process_id,
                   g_loaded[i].name,
                   g_loaded[i].entry_point,
                   g_loaded[i].size);
            shown++;
        }
    }

    if (shown == 0) {
        printf("  (none)\n");
    }
}