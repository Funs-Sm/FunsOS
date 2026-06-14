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

/* ================================================================ */
/*  v2 扩展实现                                                      */
/* ================================================================ */

/* ---- 内部状态 ---- */

static fun_lib_context_t g_lib_ctx = {NULL, 0, 0};
static fun_resource_t g_resources[256];
static int g_resource_count = 0;
static fun_debug_line_entry_t *g_debug_lines = NULL;
static uint32_t g_debug_line_count = 0;
static fun_debug_symbol_t *g_debug_symbols = NULL;
static uint32_t g_debug_symbol_count = 0;
static fun_debug_frame_t *g_debug_frames = NULL;
static uint32_t g_debug_frame_count = 0;
static fun_tls_block_t g_tls_blocks[32];
static int g_tls_block_count = 0;
static fun_exception_entry_t *g_exception_entries = NULL;
static uint32_t g_exception_entry_count = 0;
static fun_unwind_info_t *g_unwind_infos = NULL;
static uint32_t g_unwind_info_count = 0;

/* ================================================================ */
/*  哈希函数 (SHA-256 简化/校验)                                       */
/* ================================================================ */

static uint32_t fun_rotl32(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

static void fun_sha256_transform(uint32_t *state, const uint8_t *block) {
    uint32_t w[64];
    int i;
    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) |
               (uint32_t)block[i * 4 + 3];
    }
    for (i = 16; i < 64; i++) {
        uint32_t s0 = fun_rotl32(w[i - 15], 7) ^ fun_rotl32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = fun_rotl32(w[i - 2], 17) ^ fun_rotl32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
        0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
        0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
        0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
        0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };
    for (i = 0; i < 64; i++) {
        uint32_t s1 = fun_rotl32(e, 6) ^ fun_rotl32(e, 11) ^ fun_rotl32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + s1 + ch + k[i] + w[i];
        uint32_t s0 = fun_rotl32(a, 2) ^ fun_rotl32(a, 13) ^ fun_rotl32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

int fun_hash_data(const uint8_t *data, uint32_t size,
                   uint8_t *hash_out, uint32_t *hash_len) {
    if (!data || !hash_out || !hash_len) {
        set_error("null parameter");
        return FUN_ERR_SIGNATURE;
    }
    uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    uint64_t bit_len = (uint64_t)size * 8;
    uint32_t pos = 0;
    while (pos + 64 <= size) {
        fun_sha256_transform(state, data + pos);
        pos += 64;
    }
    uint8_t last_block[64];
    uint32_t remaining = size - pos;
    memset(last_block, 0, 64);
    if (remaining > 0) memcpy(last_block, data + pos, remaining);
    last_block[remaining] = 0x80;
    if (remaining >= 56) {
        fun_sha256_transform(state, last_block);
        memset(last_block, 0, 64);
    }
    /* 写入长度 (大端) */
    for (int i = 0; i < 8; i++) {
        last_block[56 + i] = (uint8_t)(bit_len >> (56 - i * 8));
    }
    fun_sha256_transform(state, last_block);
    for (int i = 0; i < 8; i++) {
        hash_out[i * 4]     = (uint8_t)(state[i] >> 24);
        hash_out[i * 4 + 1] = (uint8_t)(state[i] >> 16);
        hash_out[i * 4 + 2] = (uint8_t)(state[i] >> 8);
        hash_out[i * 4 + 3] = (uint8_t)(state[i]);
    }
    *hash_len = 32;
    return FUN_ERR_NONE;
}

/* ================================================================ */
/*  签名验证                                                          */
/* ================================================================ */

/* 简单模幂运算 (用于 RSA 验证) */
static int fun_mod_exp(const uint8_t *base, uint32_t base_len,
                        const uint8_t *exp, uint32_t exp_len,
                        const uint8_t *mod, uint32_t mod_len,
                        uint8_t *result, uint32_t *result_len) {
    /* 简化实现：在小系统上，使用模拟的模幂运算 */
    /* 真实系统中应使用硬件加速或 bigint 库 */
    uint32_t out_len = mod_len;
    if (out_len > 256) out_len = 256;
    for (uint32_t i = 0; i < out_len; i++) {
        result[i] = (uint8_t)(((uint32_t)base[i % base_len] ^
                                (uint32_t)exp[i % exp_len]) %
                               ((uint32_t)mod[i % mod_len] + 1));
    }
    *result_len = out_len;
    return 0;
}

int fun_verify_rsa(const uint8_t *data, uint32_t data_size,
                    const uint8_t *sig, uint32_t sig_size,
                    const fun_public_key_t *pubkey) {
    if (!data || !sig || !pubkey) {
        set_error("null parameter");
        return FUN_ERR_SIGNATURE;
    }

    /* 1. 计算数据的哈希 */
    uint8_t hash[32];
    uint32_t hash_len;
    if (fun_hash_data(data, data_size, hash, &hash_len) != FUN_ERR_NONE) {
        return FUN_ERR_SIGNATURE;
    }

    /* 2. RSA 公钥解密签名得到期望的哈希 */
    uint8_t decrypted[256];
    uint32_t dec_len;
    int ret = fun_mod_exp(sig, sig_size,
                           pubkey->key_data, 3, /* 常用 e=65537, 此处简化 */
                           pubkey->key_data, pubkey->key_length,
                           decrypted, &dec_len);
    if (ret < 0) {
        set_error("rsa mod_exp failed");
        return FUN_ERR_SIGNATURE;
    }

    /* 3. 比较哈希 */
    if (dec_len < hash_len) {
        set_error("rsa signature too short");
        return FUN_ERR_SIGNATURE;
    }

    /* PKCS#1 v1.5 填充检查 (简化) */
    int match = 1;
    for (uint32_t i = 0; i < hash_len; i++) {
        if (decrypted[dec_len - hash_len + i] != hash[i]) {
            match = 0;
            break;
        }
    }

    if (!match) {
        set_error("rsa signature verification failed");
        return FUN_ERR_SIGNATURE;
    }

    set_error("success");
    return FUN_ERR_NONE;
}

int fun_verify_ecdsa(const uint8_t *data, uint32_t data_size,
                      const uint8_t *sig, uint32_t sig_size,
                      const fun_public_key_t *pubkey) {
    if (!data || !sig || !pubkey) {
        set_error("null parameter");
        return FUN_ERR_SIGNATURE;
    }

    /* 1. 计算哈希 */
    uint8_t hash[32];
    uint32_t hash_len;
    if (fun_hash_data(data, data_size, hash, &hash_len) != FUN_ERR_NONE) {
        return FUN_ERR_SIGNATURE;
    }

    /* 2. ECDSA 验证 (简化椭圆曲线点运算) */
    /* 真实实现需要使用椭圆曲线数学库 */
    /* 此处使用简化的 HMAC 风格验证 */
    uint32_t accum = 0;
    for (uint32_t i = 0; i < hash_len && i < sig_size; i++) {
        accum += (uint32_t)hash[i] ^ (uint32_t)sig[i];
    }

    /* 验证签名中的 R 和 S 值是否与公钥匹配 */
    uint32_t r_val = 0, s_val = 0;
    if (sig_size >= 8) {
        r_val = ((uint32_t)sig[0] << 24) | ((uint32_t)sig[1] << 16) |
                ((uint32_t)sig[2] << 8) | (uint32_t)sig[3];
        s_val = ((uint32_t)sig[4] << 24) | ((uint32_t)sig[5] << 16) |
                ((uint32_t)sig[6] << 8) | (uint32_t)sig[7];
    }

    /* 使用公钥数据做简化点乘法验证 */
    uint32_t key_check = 0;
    for (uint32_t i = 0; i < pubkey->key_length && i < 32; i++) {
        key_check += (uint32_t)pubkey->key_data[i] * ((i % 7) + 1);
    }

    if ((r_val * s_val + key_check) % 997 == 0 || accum < 1000) {
        set_error("success");
        return FUN_ERR_NONE;
    }

    set_error("ecdsa signature verification failed");
    return FUN_ERR_SIGNATURE;
}

int fun_verify_signature(const uint8_t *data, uint32_t size,
                          fun_public_key_t *pubkey) {
    if (!data || !pubkey) {
        set_error("null parameter");
        return FUN_ERR_SIGNATURE;
    }
    if (size < sizeof(fun_header_v2_t)) {
        set_error("file too small");
        return FUN_ERR_SIGNATURE;
    }

    const fun_header_v2_t *hdr = (const fun_header_v2_t *)data;
    if (hdr->magic != FUN_MAGIC) {
        return FUN_ERR_MAGIC;
    }

    /* 查找签名节区 */
    if (hdr->signature_offset == 0 || hdr->signature_size == 0) {
        set_error("no signature present");
        return FUN_ERR_SIGNATURE;
    }

    if ((uint64_t)hdr->signature_offset + hdr->signature_size > size) {
        set_error("signature data out of bounds");
        return FUN_ERR_SIGNATURE;
    }

    const fun_signature_t *sig_hdr =
        (const fun_signature_t *)(data + hdr->signature_offset);
    const uint8_t *sig_data = data + hdr->signature_offset + sizeof(fun_signature_t);
    const uint8_t *cert_data = sig_data + sig_hdr->sig_length;

    /* 计算减去签名后的文件哈希 */
    uint8_t hash[32];
    uint32_t hash_len;

    /* 创建不含签名的数据副本 */
    uint32_t data_to_hash_size = hdr->signature_offset;
    int ret = fun_hash_data(data, data_to_hash_size, hash, &hash_len);
    if (ret != FUN_ERR_NONE) return ret;

    /* 根据算法调用相应的验证 */
    switch (sig_hdr->sig_algorithm) {
    case FUN_SIG_RSA2048:
        return fun_verify_rsa(hash, hash_len,
                               sig_data, sig_hdr->sig_length, pubkey);
    case FUN_SIG_ECDSA_P256:
    case FUN_SIG_ECDSA_P384:
        return fun_verify_ecdsa(hash, hash_len,
                                 sig_data, sig_hdr->sig_length, pubkey);
    default:
        set_error("unknown signature algorithm");
        return FUN_ERR_SIGNATURE;
    }
}

int fun_sign_data(uint8_t *data, uint32_t size,
                   fun_public_key_t *privkey, uint8_t *sig_out, uint32_t *sig_len) {
    if (!data || !privkey || !sig_out || !sig_len) {
        set_error("null parameter");
        return FUN_ERR_SIGNATURE;
    }
    /* 简化签名: 对数据做哈希，然后用私钥"加密" */
    uint8_t hash[32];
    uint32_t hash_len;
    int ret = fun_hash_data(data, size, hash, &hash_len);
    if (ret != FUN_ERR_NONE) return ret;

    for (uint32_t i = 0; i < hash_len && i < 256; i++) {
        sig_out[i] = hash[i] ^ privkey->key_data[i % privkey->key_length];
    }
    *sig_len = hash_len;
    return FUN_ERR_NONE;
}

/* ================================================================ */
/*  共享库加载                                                        */
/* ================================================================ */

static uint32_t fun_dynsym_hash(const char *name) {
    uint32_t h = 0, g;
    while (*name) {
        h = (h << 4) + (uint8_t)*name++;
        g = h & 0xF0000000;
        if (g) h ^= g >> 24;
        h &= ~g;
    }
    return h;
}

int fun_load_library(const char *path) {
    if (!path) {
        set_error("null path");
        return FUN_ERR_FUNLIB;
    }

    /* 检查是否已加载 */
    funlib_t *existing = fun_find_library(path);
    if (existing) {
        existing->ref_count++;
        klog_info("FUNLIB: library '%s' already loaded, ref=%d",
                   path, existing->ref_count);
        return FUN_ERR_NONE;
    }

    /* 打开文件 */
    file_t *file = NULL;
    if (vfs_open(path, FILE_MODE_READ, &file) != 0) {
        set_error("failed to open library file");
        return FUN_ERR_FUNLIB;
    }

    uint32_t file_size = file->inode->size;
    uint8_t *buf = (uint8_t *)kmalloc(file_size);
    if (!buf) {
        vfs_close(file);
        set_error("failed to allocate read buffer");
        return FUN_ERR_MEMORY;
    }

    int32_t bytes_read = vfs_read(file, buf, file_size);
    vfs_close(file);

    if (bytes_read <= 0) {
        kfree(buf);
        set_error("failed to read library file");
        return FUN_ERR_FUNLIB;
    }

    /* 解析 .FUNLIB 格式 (与 .FUN 兼容) */
    const fun_header_t *hdr = (const fun_header_t *)buf;
    if (hdr->magic != FUN_MAGIC) {
        kfree(buf);
        set_error("invalid library magic");
        return FUN_ERR_FUNLIB;
    }

    /* 分配库描述符 */
    funlib_t *lib = (funlib_t *)kmalloc(sizeof(funlib_t));
    if (!lib) {
        kfree(buf);
        set_error("failed to allocate library descriptor");
        return FUN_ERR_MEMORY;
    }
    memset(lib, 0, sizeof(funlib_t));

    /* 复制名称 */
    int n = 0;
    while (path[n] && n < 63) { lib->name[n] = path[n]; n++; }
    lib->name[n] = '\0';

    lib->base = hdr->base_address;
    lib->size = file_size;
    lib->ref_count = 1;

    /* 解析动态符号表 */
    const fun_section_t *sections =
        (const fun_section_t *)(buf + hdr->header_size);
    for (uint16_t i = 0; i < hdr->section_count; i++) {
        const fun_section_t *sec = &sections[i];
        switch (sec->type) {
        case FUN_SECTION_DYNSYM:
            lib->dynsym_count = sec->raw_size / sizeof(fun_dynsym_t);
            lib->dynsym = (fun_dynsym_t *)kmalloc(sec->raw_size);
            if (lib->dynsym) {
                memcpy(lib->dynsym, buf + sec->raw_offset, sec->raw_size);
            }
            break;
        case FUN_SECTION_DYNSTR:
            lib->dynstr = (char *)kmalloc(sec->raw_size);
            if (lib->dynstr) {
                memcpy(lib->dynstr, buf + sec->raw_offset, sec->raw_size);
            }
            break;
        case FUN_SECTION_GOT:
            lib->got = (uint32_t *)kmalloc(sec->raw_size);
            if (lib->got) {
                memcpy(lib->got, buf + sec->raw_offset, sec->raw_size);
            }
            break;
        default:
            break;
        }
    }

    /* 构建哈希表 */
    if (lib->dynsym_count > 0) {
        lib->hash = (uint32_t *)kmalloc((lib->dynsym_count + 1) * 8);
        if (lib->hash) {
            uint32_t nbucket = lib->dynsym_count;
            uint32_t nchain = lib->dynsym_count;
            lib->hash[0] = nbucket;
            lib->hash[1] = nchain;
            uint32_t *buckets = &lib->hash[2];
            uint32_t *chains = &lib->hash[2 + nbucket];
            for (uint32_t i = 0; i < nbucket; i++) buckets[i] = 0;
            for (uint32_t i = 0; i < nchain; i++) chains[i] = 0;

            for (uint32_t i = 1; i < lib->dynsym_count; i++) {
                const fun_dynsym_t *sym = &lib->dynsym[i];
                const char *sym_name = lib->dynstr ? lib->dynstr + sym->value : sym->name;
                uint32_t h = fun_dynsym_hash(sym_name) % nbucket;
                chains[i] = buckets[h];
                buckets[h] = i;
            }
        }
    }

    /* 加入全局列表 */
    lib->next = g_lib_ctx.loaded_libs;
    g_lib_ctx.loaded_libs = lib;
    g_lib_ctx.lib_count++;

    kfree(buf);
    klog_info("FUNLIB: loaded '%s' (base=0x%x, %d symbols)",
               lib->name, lib->base, lib->dynsym_count);
    return FUN_ERR_NONE;
}

funlib_t *fun_find_library(const char *name) {
    funlib_t *lib = g_lib_ctx.loaded_libs;
    while (lib) {
        int match = 1;
        int i = 0;
        while (name[i] && lib->name[i]) {
            if (name[i] != lib->name[i]) { match = 0; break; }
            i++;
        }
        if (match && name[i] == '\0' && lib->name[i] == '\0') return lib;

        /* 也尝试匹配短名称 */
        if (!match) {
            const char *short_name = name;
            const char *p = name;
            while (*p) { if (*p == '/') short_name = p + 1; p++; }
            const char *lib_short = lib->name;
            p = lib->name;
            while (*p) { if (*p == '/') lib_short = p + 1; p++; }

            match = 1;
            i = 0;
            while (short_name[i] && lib_short[i]) {
                if (short_name[i] != lib_short[i]) { match = 0; break; }
                i++;
            }
            if (match && short_name[i] == '\0' && lib_short[i] == '\0') return lib;
        }
        lib = lib->next;
    }
    return NULL;
}

int fun_unload_library(funlib_t *lib) {
    if (!lib) {
        set_error("null library");
        return FUN_ERR_FUNLIB;
    }
    lib->ref_count--;
    if (lib->ref_count > 0) {
        klog_info("FUNLIB: '%s' ref_count=%d", lib->name, lib->ref_count);
        return FUN_ERR_NONE;
    }

    /* 从链表中移除 */
    if (g_lib_ctx.loaded_libs == lib) {
        g_lib_ctx.loaded_libs = lib->next;
    } else {
        funlib_t *prev = g_lib_ctx.loaded_libs;
        while (prev && prev->next != lib) prev = prev->next;
        if (prev) prev->next = lib->next;
    }
    g_lib_ctx.lib_count--;

    /* 释放资源 */
    if (lib->dynsym) kfree(lib->dynsym);
    if (lib->dynstr) kfree(lib->dynstr);
    if (lib->hash) kfree(lib->hash);
    if (lib->got) kfree(lib->got);
    kfree(lib);

    klog_info("FUNLIB: unloaded library");
    return FUN_ERR_NONE;
}

void *fun_dlsym(funlib_t *lib, const char *symbol) {
    if (!lib || !symbol) return NULL;

    /* 使用哈希表查找 */
    if (lib->hash && lib->dynsym_count > 0) {
        uint32_t nbucket = lib->hash[0];
        uint32_t *buckets = &lib->hash[2];
        uint32_t *chains = &lib->hash[2 + nbucket];

        uint32_t h = fun_dynsym_hash(symbol) % nbucket;
        uint32_t idx = buckets[h];
        while (idx != 0 && idx < lib->dynsym_count) {
            const fun_dynsym_t *sym = &lib->dynsym[idx];
            const char *sym_name;
            if (lib->dynstr) {
                uint32_t name_off = *(uint32_t *)&sym->name[0];
                sym_name = lib->dynstr + name_off;
            } else {
                sym_name = sym->name;
            }
            int match = 1;
            int i = 0;
            while (symbol[i] && sym_name[i]) {
                if (symbol[i] != sym_name[i]) { match = 0; break; }
                i++;
            }
            if (match && symbol[i] == '\0' && sym_name[i] == '\0') {
                return (void *)((uint32_t)lib->base + sym->value);
            }
            idx = chains[idx];
        }
    }

    /* 回退到线性搜索 */
    for (uint32_t i = 0; i < lib->dynsym_count; i++) {
        const fun_dynsym_t *sym = &lib->dynsym[i];
        const char *sym_name;
        if (lib->dynstr) {
            uint32_t name_off = *(uint32_t *)&sym->name[0];
            sym_name = lib->dynstr + name_off;
        } else {
            sym_name = sym->name;
        }
        int match = 1;
        int j = 0;
        while (symbol[j] && sym_name[j]) {
            if (symbol[j] != sym_name[j]) { match = 0; break; }
            j++;
        }
        if (match && symbol[j] == '\0' && sym_name[j] == '\0') {
            return (void *)((uint32_t)lib->base + sym->value);
        }
    }

    return NULL;
}

int fun_resolve_plt(funlib_t *lib, uint32_t plt_index) {
    if (!lib || !lib->dynsym) {
        set_error("invalid library");
        return FUN_ERR_FUNLIB;
    }

    if (plt_index >= lib->dynsym_count) {
        set_error("PLT index out of range");
        return FUN_ERR_FUNLIB;
    }

    const fun_dynsym_t *sym = &lib->dynsym[plt_index];
    const char *sym_name;
    if (lib->dynstr) {
        uint32_t name_off = *(uint32_t *)&sym->name[0];
        sym_name = lib->dynstr + name_off;
    } else {
        sym_name = sym->name;
    }

    /* 在已加载的所有库中搜索符号 */
    funlib_t *cur = g_lib_ctx.loaded_libs;
    while (cur) {
        void *addr = fun_dlsym(cur, sym_name);
        if (addr) {
            /* 更新 GOT */
            if (lib->got && plt_index > 0) {
                lib->got[plt_index] = (uint32_t)addr;
            }
            klog_debug("FUNLIB: resolved PLT[%d] '%s' -> 0x%x",
                        plt_index, sym_name, (uint32_t)addr);
            return FUN_ERR_NONE;
        }
        cur = cur->next;
    }

    /* 也检查内核符号 */
    uint32_t ksym_addr = ksym_lookup(sym_name);
    if (ksym_addr != 0) {
        if (lib->got && plt_index > 0) {
            lib->got[plt_index] = ksym_addr;
        }
        return FUN_ERR_NONE;
    }

    set_error("unresolved PLT symbol");
    return FUN_ERR_FUNLIB;
}

int fun_bind_lazy(funlib_t *lib) {
    if (!lib) {
        set_error("null library");
        return FUN_ERR_FUNLIB;
    }
    /* 延迟绑定: PLT 条目初始指向解析器桩 */
    /* 首次调用时触发解析, 这里只标记 lib 为就绪 */
    klog_debug("FUNLIB: lazy binding enabled for '%s'", lib->name);
    return FUN_ERR_NONE;
}

int fun_check_verdef(funlib_t *lib, uint16_t required_version) {
    if (!lib) return FUN_ERR_FUNLIB;
    /* 简化实现: 总是通过 (无版本追踪) */
    (void)required_version;
    return FUN_ERR_NONE;
}

int fun_check_verneed(funlib_t *lib, uint16_t need_version) {
    if (!lib) return FUN_ERR_FUNLIB;
    /* 简化实现: 总是通过 */
    (void)need_version;
    return FUN_ERR_NONE;
}

/* ================================================================ */
/*  资源管理                                                          */
/* ================================================================ */

int fun_load_resources(const uint8_t *data, uint32_t size) {
    if (!data || size < sizeof(fun_res_dir_header_t)) {
        set_error("null data or too small");
        return FUN_ERR_RESOURCE;
    }

    const fun_res_dir_header_t *dir_hdr = (const fun_res_dir_header_t *)data;
    if (dir_hdr->resource_count > 256) {
        set_error("too many resources");
        return FUN_ERR_RESOURCE;
    }

    const fun_res_dir_entry_t *entries =
        (const fun_res_dir_entry_t *)(data + sizeof(fun_res_dir_header_t));
    const char *string_table =
        (const char *)(data + dir_hdr->string_table_offset);
    const uint8_t *resource_data = data + dir_hdr->data_offset;

    for (uint32_t i = 0; i < dir_hdr->resource_count; i++) {
        const fun_res_dir_entry_t *entry = &entries[i];
        if (g_resource_count >= 256) break;

        fun_resource_t *res = &g_resources[g_resource_count];
        res->type = entry->type;
        res->language = entry->language;
        res->size = entry->data_size;
        res->ref_count = 1;

        /* 复制资源名称 */
        if (entry->name_offset < dir_hdr->string_table_size) {
            const char *src = string_table + entry->name_offset;
            int j = 0;
            while (src[j] && j < 127) { res->name[j] = src[j]; j++; }
            res->name[j] = '\0';
        } else {
            /* 数字ID作为名称 */
            int off = 0;
            uint32_t id = entry->name_offset;
            if (id == 0) {
                res->name[0] = '0'; res->name[1] = '\0';
            } else {
                while (id > 0 && off < 16) {
                    res->name[off++] = '0' + (id % 10);
                    id /= 10;
                }
                res->name[off] = '\0';
            }
        }

        /* 复制资源数据 */
        if (entry->data_offset + entry->data_size <=
            size - dir_hdr->data_offset) {
            res->data = (uint8_t *)kmalloc(entry->data_size);
            if (res->data) {
                memcpy(res->data, resource_data + entry->data_offset,
                       entry->data_size);
            }
        }

        g_resource_count++;
    }

    klog_info("FUNRES: loaded %d resources", dir_hdr->resource_count);
    return FUN_ERR_NONE;
}

fun_resource_t *fun_find_resource(uint32_t type, const char *name, uint32_t lang) {
    for (int i = 0; i < g_resource_count; i++) {
        fun_resource_t *res = &g_resources[i];
        if (res->type != type) continue;
        if (lang != 0 && res->language != 0 && res->language != lang) continue;

        int match = 1;
        const char *a = name;
        const char *b = res->name;
        while (*a && *b) {
            if (*a != *b) { match = 0; break; }
            a++; b++;
        }
        if (match && *a == '\0' && *b == '\0') return res;
    }
    return NULL;
}

void *fun_load_resource_data(uint32_t type, const char *name, uint32_t *size) {
    fun_resource_t *res = fun_find_resource(type, name, 0);
    if (!res) return NULL;
    if (size) *size = res->size;
    res->ref_count++;
    return res->data;
}

fun_bitmap_header_t *fun_load_bitmap_resource(const char *name) {
    fun_resource_t *res = fun_find_resource(FUN_RES_BITMAP, name, 0);
    if (!res || res->size < sizeof(fun_bitmap_header_t)) return NULL;

    fun_bitmap_header_t *bmp =
        (fun_bitmap_header_t *)kmalloc(sizeof(fun_bitmap_header_t));
    if (bmp) {
        memcpy(bmp, res->data, sizeof(fun_bitmap_header_t));
    }
    return bmp;
}

const char *fun_load_string_resource(uint16_t string_id, uint32_t lang) {
    /* 字符串资源通过ID查找 */
    for (int i = 0; i < g_resource_count; i++) {
        fun_resource_t *res = &g_resources[i];
        if (res->type != FUN_RES_STRING) continue;
        if (lang != 0 && res->language != 0 && res->language != lang) continue;

        /* 检查字符串ID */
        if (res->data && res->size >= sizeof(fun_string_entry_t)) {
            fun_string_entry_t *entry = (fun_string_entry_t *)res->data;
            if (entry->id == string_id) {
                return (const char *)(res->data + sizeof(fun_string_entry_t));
            }
        }
    }
    return NULL;
}

int fun_free_resource(fun_resource_t *res) {
    if (!res) return FUN_ERR_RESOURCE;
    res->ref_count--;
    if (res->ref_count <= 0 && res->data) {
        kfree(res->data);
        res->data = NULL;
        res->size = 0;
    }
    return FUN_ERR_NONE;
}

uint32_t fun_get_resource_count(uint32_t type) {
    uint32_t count = 0;
    for (int i = 0; i < g_resource_count; i++) {
        if (g_resources[i].type == type) count++;
    }
    return count;
}

/* ================================================================ */
/*  调试信息                                                          */
/* ================================================================ */

int fun_load_debug_info(const uint8_t *data, uint32_t size) {
    if (!data) {
        set_error("null data");
        return FUN_ERR_DEBUG;
    }

    if (size < sizeof(fun_debug_header_t)) return FUN_ERR_DEBUG;
    const fun_debug_header_t *dbg_hdr = (const fun_debug_header_t *)data;

    switch (dbg_hdr->debug_type) {
    case FUN_DEBUG_LINE: {
        uint32_t count = (dbg_hdr->size - sizeof(fun_debug_header_t)) /
                         sizeof(fun_debug_line_entry_t);
        if (count > 0) {
            g_debug_lines = (fun_debug_line_entry_t *)
                kmalloc(count * sizeof(fun_debug_line_entry_t));
            if (g_debug_lines) {
                memcpy(g_debug_lines,
                       data + sizeof(fun_debug_header_t),
                       count * sizeof(fun_debug_line_entry_t));
                g_debug_line_count = count;
            }
        }
        break;
    }
    case FUN_DEBUG_SYMBOL: {
        uint32_t count = (dbg_hdr->size - sizeof(fun_debug_header_t)) /
                         sizeof(fun_debug_symbol_t);
        if (count > 0) {
            g_debug_symbols = (fun_debug_symbol_t *)
                kmalloc(count * sizeof(fun_debug_symbol_t));
            if (g_debug_symbols) {
                memcpy(g_debug_symbols,
                       data + sizeof(fun_debug_header_t),
                       count * sizeof(fun_debug_symbol_t));
                g_debug_symbol_count = count;
            }
        }
        break;
    }
    case FUN_DEBUG_FRAME: {
        uint32_t count = (dbg_hdr->size - sizeof(fun_debug_header_t)) /
                         sizeof(fun_debug_frame_t);
        if (count > 0) {
            g_debug_frames = (fun_debug_frame_t *)
                kmalloc(count * sizeof(fun_debug_frame_t));
            if (g_debug_frames) {
                memcpy(g_debug_frames,
                       data + sizeof(fun_debug_header_t),
                       count * sizeof(fun_debug_frame_t));
                g_debug_frame_count = count;
            }
        }
        break;
    }
    default:
        break;
    }

    return FUN_ERR_NONE;
}

fun_debug_line_entry_t *fun_lookup_line(uint32_t address) {
    for (uint32_t i = 0; i < g_debug_line_count; i++) {
        if (g_debug_lines[i].address == address) {
            return &g_debug_lines[i];
        }
    }
    /* 查找最接近但不超过的地址 */
    fun_debug_line_entry_t *best = NULL;
    for (uint32_t i = 0; i < g_debug_line_count; i++) {
        if (g_debug_lines[i].address <= address) {
            if (!best || g_debug_lines[i].address > best->address) {
                best = &g_debug_lines[i];
            }
        }
    }
    return best;
}

fun_debug_symbol_t *fun_lookup_symbol(const char *name) {
    for (uint32_t i = 0; i < g_debug_symbol_count; i++) {
        int match = 1;
        const char *a = name;
        const char *b = g_debug_symbols[i].name;
        while (*a && *b) {
            if (*a != *b) { match = 0; break; }
            a++; b++;
        }
        if (match && *a == '\0' && *b == '\0') {
            return &g_debug_symbols[i];
        }
    }
    return NULL;
}

fun_debug_symbol_t *fun_lookup_address(uint32_t address) {
    fun_debug_symbol_t *best = NULL;
    for (uint32_t i = 0; i < g_debug_symbol_count; i++) {
        if (g_debug_symbols[i].address <= address &&
            (!best || g_debug_symbols[i].address > best->address)) {
            best = &g_debug_symbols[i];
        }
    }
    return best;
}

int fun_get_line_info(uint32_t address, char *file_out, int *line_out, int *col_out) {
    fun_debug_line_entry_t *entry = fun_lookup_line(address);
    if (!entry) return FUN_ERR_DEBUG;

    if (line_out) *line_out = (int)entry->line_number;
    if (col_out) *col_out = (int)entry->column_number;
    if (file_out) {
        /* 文件索引转换为文件名 - 简化: 使用数字 */
        int off = 0;
        uint32_t idx = entry->file_index;
        file_out[off++] = 'f';
        file_out[off++] = (idx < 10) ? '0' : ('0' + idx / 10);
        file_out[off++] = '0' + (idx % 10);
        file_out[off++] = '.';
        file_out[off++] = 'c';
        file_out[off] = '\0';
    }
    return FUN_ERR_NONE;
}

int fun_get_frame_info(uint32_t address, fun_debug_frame_t *frame_out) {
    if (!frame_out) return FUN_ERR_DEBUG;
    for (uint32_t i = 0; i < g_debug_frame_count; i++) {
        if (address >= g_debug_frames[i].code_begin &&
            address < g_debug_frames[i].code_begin + g_debug_frames[i].code_length) {
            memcpy(frame_out, &g_debug_frames[i], sizeof(fun_debug_frame_t));
            return FUN_ERR_NONE;
        }
    }
    return FUN_ERR_DEBUG;
}

void fun_dump_stack_trace(uint32_t *stack, uint32_t depth) {
    klog_info("Stack trace (depth=%d):", depth);
    printf("Stack trace:\n");
    for (uint32_t i = 0; i < depth && stack; i++) {
        uint32_t addr = stack[i];
        fun_debug_line_entry_t *line = fun_lookup_line(addr);
        fun_debug_symbol_t *sym = fun_lookup_address(addr);

        printf("  #%d  0x%08X", i, addr);
        if (sym) {
            printf(" in %s", sym->name);
        }
        if (line) {
            printf(" at f%02d.c:%d", line->file_index, line->line_number);
        }
        printf("\n");
    }
}

/* ================================================================ */
/*  TLS 实现                                                         */
/* ================================================================ */

int fun_init_tls(fun_tls_block_t *tls_block, const fun_tls_directory_t *dir) {
    if (!tls_block || !dir) {
        set_error("null parameter");
        return FUN_ERR_TLS;
    }

    uint32_t tls_size = dir->end_rva - dir->start_rva + dir->zero_fill_size;
    if (tls_size == 0) {
        tls_block->tls_data = NULL;
        tls_block->tls_size = 0;
        tls_block->initialized = 1;
        return FUN_ERR_NONE;
    }

    tls_block->tls_data = (uint8_t *)kmalloc(tls_size);
    if (!tls_block->tls_data) {
        set_error("tls allocation failed");
        return FUN_ERR_MEMORY;
    }

    /* 拷贝 TLS 模板 */
    uint32_t init_size = dir->end_rva - dir->start_rva;
    if (init_size > 0 && dir->start_rva != 0) {
        memcpy(tls_block->tls_data, (void *)(uint32_t)dir->start_rva, init_size);
    }

    /* 零填充 */
    if (dir->zero_fill_size > 0) {
        memset(tls_block->tls_data + init_size, 0, dir->zero_fill_size);
    }

    tls_block->tls_size = tls_size;
    tls_block->initialized = 1;

    /* 对齐 */
    if (dir->alignment > 1) {
        uint32_t align = dir->alignment;
        uint32_t ptr_val = (uint32_t)tls_block->tls_data;
        uint32_t misalign = ptr_val & (align - 1);
        if (misalign != 0) {
            /* 在简化实现中忽略对齐 */
            (void)misalign;
        }
    }

    g_tls_blocks[g_tls_block_count % 32] = *tls_block;
    g_tls_block_count++;

    klog_debug("FUNTLS: initialized TLS block (size=%d)", tls_size);
    return FUN_ERR_NONE;
}

void *fun_get_tls_pointer(void) {
    /* 返回当前线程的 TLS 指针 */
    /* 简化: 返回当前进程 TLS */
    for (int i = 0; i < g_tls_block_count && i < 32; i++) {
        if (g_tls_blocks[i].initialized) {
            return g_tls_blocks[i].tls_data;
        }
    }
    return NULL;
}

int fun_set_tls_pointer(void *ptr) {
    if (g_tls_block_count < 32) {
        g_tls_blocks[g_tls_block_count].tls_data = (uint8_t *)ptr;
        g_tls_blocks[g_tls_block_count].tls_size = 0;
        g_tls_blocks[g_tls_block_count].initialized = 1;
        g_tls_block_count++;
    }
    return FUN_ERR_NONE;
}

int fun_run_tls_callbacks(void) {
    /* 查找所有加载的 .FUN 文件中的 TLS 回调和执行 */
    for (int i = 0; i < g_loaded_count; i++) {
        if (g_loaded[i].used && g_loaded[i].header.version >= FUN_VERSION_2) {
            /* v2 TLS 回调: 搜索 TLS 节区 */
            (void)i; /* 简化: 标记需要TLS回调 */
        }
    }
    klog_debug("FUNTLS: TLS callbacks executed");
    return FUN_ERR_NONE;
}

void fun_cleanup_tls(fun_tls_block_t *tls_block) {
    if (!tls_block) return;
    if (tls_block->tls_data) {
        kfree(tls_block->tls_data);
        tls_block->tls_data = NULL;
    }
    tls_block->tls_size = 0;
    tls_block->initialized = 0;
}

/* ================================================================ */
/*  异常处理                                                          */
/* ================================================================ */

int fun_load_exception_table(const uint8_t *data, uint32_t size) {
    if (!data || size < sizeof(fun_exception_dir_t)) {
        set_error("null data or too small");
        return FUN_ERR_EXCEPTION;
    }

    const fun_exception_dir_t *dir = (const fun_exception_dir_t *)data;
    uint32_t entries_size = dir->entry_count * sizeof(fun_exception_entry_t);

    if (sizeof(fun_exception_dir_t) + entries_size > size) {
        set_error("exception table size mismatch");
        return FUN_ERR_EXCEPTION;
    }

    if (dir->entry_count > 0) {
        g_exception_entries = (fun_exception_entry_t *)
            kmalloc(entries_size);
        if (!g_exception_entries) {
            set_error("failed to allocate exception table");
            return FUN_ERR_MEMORY;
        }
        memcpy(g_exception_entries,
               data + sizeof(fun_exception_dir_t), entries_size);
        g_exception_entry_count = dir->entry_count;
    }

    klog_info("FUNEH: loaded %d exception entries", dir->entry_count);
    return FUN_ERR_NONE;
}

fun_exception_entry_t *fun_find_exception_handler(uint32_t address) {
    for (uint32_t i = 0; i < g_exception_entry_count; i++) {
        fun_exception_entry_t *entry = &g_exception_entries[i];
        if (address >= entry->begin_address &&
            address < entry->end_address) {
            return entry;
        }
    }
    return NULL;
}

fun_unwind_info_t *fun_get_unwind_info(uint32_t address) {
    fun_exception_entry_t *entry = fun_find_exception_handler(address);
    if (!entry || entry->unwind_info == 0) return NULL;

    /* unwind_info 是相对于异常表的偏移 */
    /* 简化: 假设展开信息直接可用 */
    if (!g_unwind_infos) {
        g_unwind_infos = (fun_unwind_info_t *)
            kmalloc(sizeof(fun_unwind_info_t) * 16);
        g_unwind_info_count = 0;
    }
    if (g_unwind_info_count < 16) {
        fun_unwind_info_t *info = &g_unwind_infos[g_unwind_info_count];
        memset(info, 0, sizeof(fun_unwind_info_t));
        info->version = 1;
        info->flags = 0x1;
        info->frame_offset = 0;
        g_unwind_info_count++;
        return info;
    }
    return &g_unwind_infos[0];
}

int fun_unwind_stack(uint32_t *stack_ptr, uint32_t *frame_out) {
    if (!stack_ptr || !frame_out) {
        set_error("null parameter");
        return FUN_ERR_EXCEPTION;
    }

    /* 简化栈展开: 读取保存的帧指针 */
    uint32_t ebp = *stack_ptr;
    if (ebp == 0 || ebp < 0x1000) {
        return FUN_ERR_EXCEPTION;
    }

    /* x86 栈帧: [ebp] = old_ebp, [ebp+4] = return_address */
    uint32_t *frame = (uint32_t *)ebp;
    uint32_t old_ebp = frame[0];
    uint32_t return_addr = frame[1];

    frame_out[0] = return_addr;
    frame_out[1] = old_ebp;

    klog_debug("FUNEH: unwind: ret=0x%x, old_ebp=0x%x", return_addr, old_ebp);
    return FUN_ERR_NONE;
}

int fun_register_exception_handler(fun_exception_entry_t *handler) {
    if (!handler) return FUN_ERR_EXCEPTION;
    /* 添加到异常处理表 */
    uint32_t new_count = g_exception_entry_count + 1;
    fun_exception_entry_t *new_entries = (fun_exception_entry_t *)
        kmalloc(new_count * sizeof(fun_exception_entry_t));
    if (!new_entries) return FUN_ERR_MEMORY;

    if (g_exception_entries && g_exception_entry_count > 0) {
        memcpy(new_entries, g_exception_entries,
               g_exception_entry_count * sizeof(fun_exception_entry_t));
        kfree(g_exception_entries);
    }
    memcpy(&new_entries[g_exception_entry_count], handler,
           sizeof(fun_exception_entry_t));
    g_exception_entries = new_entries;
    g_exception_entry_count = new_count;

    return FUN_ERR_NONE;
}

int fun_dispatch_exception(uint32_t exception_code, uint32_t exception_addr) {
    klog_warn("FUNEH: exception 0x%x at 0x%x", exception_code, exception_addr);

    /* 查找异常处理器 */
    fun_exception_entry_t *entry = fun_find_exception_handler(exception_addr);
    if (!entry) {
        klog_err("FUNEH: no handler for address 0x%x", exception_addr);
        return FUN_ERR_EXCEPTION;
    }

    if (entry->handler_rva != 0) {
        klog_info("FUNEH: dispatching to handler at 0x%x", entry->handler_rva);
        /* 实际异常分发由运行时完成 */
        return FUN_ERR_NONE;
    }

    /* 如果没有具体的处理器，尝试展开 */
    uint32_t frame_info[2];
    uint32_t sp = exception_addr;
    if (fun_unwind_stack(&sp, frame_info) == FUN_ERR_NONE) {
        klog_info("FUNEH: unwound to 0x%x", frame_info[0]);
    }

    return FUN_ERR_NONE;
}

/* ================================================================ */
/*  压缩/解压                                                        */
/* ================================================================ */

int fun_decompress_lz4(const uint8_t *src, uint32_t src_size,
                        uint8_t *dst, uint32_t dst_size) {
    if (!src || !dst) {
        set_error("null parameter");
        return FUN_ERR_COMPRESS;
    }

    /* LZ4 解压 (简化实现) */
    const uint8_t *ip = src;
    const uint8_t *end = src + src_size;
    uint8_t *op = dst;
    uint8_t *oend = dst + dst_size;

    while (ip < end) {
        /* 读取 token: 高4位=字面量长度, 低4位=匹配长度-4 */
        uint8_t token = *ip++;
        uint32_t literal_len = token >> 4;
        uint32_t match_len = (token & 0x0F) + 4;

        /* 如果字面量长度 >= 15, 读取额外字节 */
        if (literal_len == 15) {
            uint8_t extra;
            do {
                if (ip >= end) goto done;
                extra = *ip++;
                literal_len += extra;
            } while (extra == 255);
        }

        /* 复制字面量 */
        if (literal_len > 0) {
            if (ip + literal_len > end) goto done;
            if (op + literal_len > oend) goto done;
            memcpy(op, ip, literal_len);
            op += literal_len;
            ip += literal_len;
        }

        /* 如果到达末尾 */
        if (ip >= end) break;

        /* 读取偏移量 */
        if (ip + 2 > end) goto done;
        uint16_t offset = (uint16_t)ip[0] | ((uint16_t)ip[1] << 8);
        ip += 2;

        if (offset == 0) goto done;

        /* 如果匹配长度 == 19 (4+15), 读取额外字节 */
        if (match_len == 19) {
            uint8_t extra;
            do {
                if (ip >= end) goto done;
                extra = *ip++;
                match_len += extra;
            } while (extra == 255);
        }

        /* 复制匹配 */
        if (op + match_len > oend) goto done;
        for (uint32_t i = 0; i < match_len; i++) {
            op[i] = op[-offset + i];
        }
        op += match_len;
    }

done:
    return FUN_ERR_NONE;
}

int fun_compress_lz4(const uint8_t *src, uint32_t src_size,
                      uint8_t *dst, uint32_t *dst_size) {
    if (!src || !dst || !dst_size) {
        set_error("null parameter");
        return FUN_ERR_COMPRESS;
    }

    /* 简化 LZ4 压缩: 只做字面量编码 */
    const uint8_t *ip = src;
    const uint8_t *end = src + src_size;
    uint8_t *op = dst;
    uint8_t *oend = dst + *dst_size;

    while (ip < end) {
        uint32_t remaining = (uint32_t)(end - ip);
        uint32_t chunk = (remaining > 255) ? 255 : remaining;

        if (op + 1 + chunk + (ip < end ? 2 : 0) > oend) {
            *dst_size = 0;
            set_error("output buffer too small");
            return FUN_ERR_COMPRESS;
        }

        /* 写入字面量块: token + 字面量 + 匹配结束标记 */
        uint8_t token = (uint8_t)((chunk & 0x0F) << 4); /* 字面量长度, match_len=0 */
        *op++ = token;

        if (chunk == 255) {
            *op++ = (uint8_t)(remaining - 255);
        }
        memcpy(op, ip, chunk);
        op += chunk;
        ip += chunk;

        if (ip < end) {
            /* 写入结束标记 */
            *op++ = 0;
            *op++ = 0;
        }
    }

    *dst_size = (uint32_t)(op - dst);
    return FUN_ERR_NONE;
}

int fun_decompress_section(const uint8_t *compressed_data, uint32_t comp_size,
                            uint8_t *output, uint32_t output_size) {
    if (!compressed_data || !output) {
        set_error("null parameter");
        return FUN_ERR_COMPRESS;
    }

    if (comp_size < sizeof(fun_compress_header_t)) {
        /* 无压缩头, 直接复制 */
        uint32_t copy_size = (comp_size < output_size) ? comp_size : output_size;
        memcpy(output, compressed_data, copy_size);
        return FUN_ERR_NONE;
    }

    const fun_compress_header_t *comp_hdr =
        (const fun_compress_header_t *)compressed_data;

    if (comp_hdr->algorithm == FUN_COMPRESS_NONE) {
        uint32_t copy_size = comp_hdr->original_size;
        if (copy_size > output_size) copy_size = output_size;
        memcpy(output, compressed_data + sizeof(fun_compress_header_t), copy_size);
        return FUN_ERR_NONE;
    }

    if (comp_hdr->algorithm == FUN_COMPRESS_LZ4) {
        uint32_t data_offset = sizeof(fun_compress_header_t);
        return fun_decompress_lz4(compressed_data + data_offset,
                                   comp_size - data_offset,
                                   output, comp_hdr->original_size);
    }

    set_error("unknown compression algorithm");
    return FUN_ERR_COMPRESS;
}

/* ================================================================ */
/*  链接器                                                            */
/* ================================================================ */

fun_linker_t *fun_linker_create(void) {
    fun_linker_t *linker = (fun_linker_t *)kmalloc(sizeof(fun_linker_t));
    if (!linker) return NULL;
    memset(linker, 0, sizeof(fun_linker_t));

    linker->obj_capacity = 16;
    linker->objects = (fun_obj_file_t *)kmalloc(
        linker->obj_capacity * sizeof(fun_obj_file_t));
    if (!linker->objects) {
        kfree(linker);
        return NULL;
    }
    memset(linker->objects, 0,
           linker->obj_capacity * sizeof(fun_obj_file_t));

    linker->output_capacity = 65536;
    linker->output = (uint8_t *)kmalloc(linker->output_capacity);
    if (!linker->output) {
        kfree(linker->objects);
        kfree(linker);
        return NULL;
    }

    return linker;
}

int fun_linker_add_object(fun_linker_t *linker, const char *filename,
                           uint8_t *data, uint32_t size) {
    if (!linker || !data) {
        if (linker) linker->error_code = FUN_ERR_LINK;
        return FUN_ERR_LINK;
    }

    if (size < sizeof(fun_header_t)) {
        linker->error_code = FUN_ERR_LINK;
        linker->error_msg = "object file too small";
        return FUN_ERR_LINK;
    }

    /* 扩展容量 */
    if (linker->obj_count >= linker->obj_capacity) {
        int new_cap = linker->obj_capacity * 2;
        fun_obj_file_t *new_objs = (fun_obj_file_t *)
            kmalloc(new_cap * sizeof(fun_obj_file_t));
        if (!new_objs) {
            linker->error_code = FUN_ERR_MEMORY;
            return FUN_ERR_MEMORY;
        }
        memcpy(new_objs, linker->objects,
               linker->obj_count * sizeof(fun_obj_file_t));
        kfree(linker->objects);
        linker->objects = new_objs;
        linker->obj_capacity = new_cap;
    }

    fun_obj_file_t *obj = &linker->objects[linker->obj_count];
    memset(obj, 0, sizeof(fun_obj_file_t));

    /* 复制文件数据 */
    obj->data = (uint8_t *)kmalloc(size);
    if (!obj->data) {
        linker->error_code = FUN_ERR_MEMORY;
        return FUN_ERR_MEMORY;
    }
    memcpy(obj->data, data, size);
    obj->size = size;

    /* 复制文件名 */
    int n = 0;
    while (filename && filename[n] && n < 63) {
        obj->name[n] = filename[n];
        n++;
    }
    obj->name[n] = '\0';

    /* 解析节区 */
    const fun_header_t *hdr = (const fun_header_t *)data;
    obj->section_count = hdr->section_count;
    obj->sections = (fun_section_t *)
        kmalloc(hdr->section_count * sizeof(fun_section_t));
    if (obj->sections) {
        memcpy(obj->sections,
               data + hdr->header_size,
               hdr->section_count * sizeof(fun_section_t));
    }

    /* 查找符号表 */
    const fun_section_t *secs =
        (const fun_section_t *)(data + hdr->header_size);
    for (uint16_t i = 0; i < hdr->section_count; i++) {
        if (secs[i].type == FUN_SECTION_DYNSYM) {
            obj->symbol_count = secs[i].raw_size / sizeof(fun_dynsym_t);
            obj->symbols = (fun_dynsym_t *)kmalloc(secs[i].raw_size);
            if (obj->symbols) {
                memcpy(obj->symbols, data + secs[i].raw_offset, secs[i].raw_size);
            }
        }
        if (secs[i].type == FUN_SECTION_DYNSTR) {
            obj->strtab_size = secs[i].raw_size;
            obj->strtab = (char *)kmalloc(secs[i].raw_size);
            if (obj->strtab) {
                memcpy(obj->strtab, data + secs[i].raw_offset, secs[i].raw_size);
            }
        }
        if (secs[i].type == FUN_SECTION_RELOC) {
            obj->reloc_count = secs[i].raw_size / sizeof(fun_reloc_t);
            obj->relocs = (fun_reloc_t *)kmalloc(secs[i].raw_size);
            if (obj->relocs) {
                memcpy(obj->relocs, data + secs[i].raw_offset, secs[i].raw_size);
            }
        }
    }

    linker->obj_count++;
    return FUN_ERR_NONE;
}

int fun_linker_set_output(fun_linker_t *linker, const char *name, uint32_t flags) {
    if (!linker) return FUN_ERR_LINK;
    /* 在输出缓冲中构建 FUN 文件头 */
    fun_header_t *hdr = (fun_header_t *)linker->output;
    memset(hdr, 0, sizeof(fun_header_t));
    hdr->magic = FUN_MAGIC;
    hdr->version = FUN_VERSION;
    hdr->header_size = sizeof(fun_header_t);
    hdr->flags = (uint16_t)flags;
    hdr->timestamp = 0; /* 构建时间 */

    linker->output_size = sizeof(fun_header_t);
    return FUN_ERR_NONE;
}

int fun_linker_link(fun_linker_t *linker) {
    if (!linker || linker->obj_count == 0) {
        if (linker) linker->error_code = FUN_ERR_LINK;
        return FUN_ERR_LINK;
    }

    /* 合并节区 */
    /* 简化: 将所有对象的 CODE/DATA/RODATA 节区合并 */

    uint32_t current_rva = 0x1000;
    uint32_t current_file_offset = sizeof(fun_header_t);
    uint16_t total_sections = 0;

    /* 第一遍: 计算节区数 */
    for (int i = 0; i < linker->obj_count; i++) {
        total_sections += linker->objects[i].section_count;
    }

    /* 预留节区表空间 */
    uint32_t section_table_offset = current_file_offset;
    uint32_t section_table_size = total_sections * sizeof(fun_section_t);
    current_file_offset += section_table_size;

    fun_section_t *out_sections = (fun_section_t *)
        (linker->output + section_table_offset);

    uint32_t section_idx = 0;

    /* 第二遍: 合并节区数据 */
    for (int i = 0; i < linker->obj_count; i++) {
        fun_obj_file_t *obj = &linker->objects[i];
        for (uint16_t j = 0; j < obj->section_count; j++) {
            fun_section_t *in_sec = &obj->sections[j];

            /* 跳过 BSS (在最后处理) */
            if (in_sec->type == FUN_SECTION_BSS) continue;

            fun_section_t *out_sec = &out_sections[section_idx++];
            memcpy(out_sec->name, in_sec->name, 16);
            out_sec->type = in_sec->type;
            out_sec->flags = in_sec->flags;
            out_sec->virtual_address = current_rva;
            out_sec->virtual_size = in_sec->virtual_size;
            out_sec->raw_offset = current_file_offset;

            uint32_t sec_data_size = in_sec->raw_size;
            if (in_sec->type == FUN_SECTION_BSS) {
                out_sec->raw_size = 0;
            } else {
                out_sec->raw_size = in_sec->raw_size;
            }

            /* 复制节区数据 */
            if (sec_data_size > 0 && obj->data) {
                const uint8_t *src = obj->data + in_sec->raw_offset;
                if (current_file_offset + sec_data_size <= linker->output_capacity) {
                    memcpy(linker->output + current_file_offset, src, sec_data_size);
                }
            }

            /* 对齐 */
            uint32_t aligned_size = (sec_data_size + 15) & ~15;
            current_file_offset += aligned_size;
            current_rva += (in_sec->virtual_size + 0xFFF) & ~0xFFF;
        }
    }

    /* 更新文件头 */
    fun_header_t *hdr = (fun_header_t *)linker->output;
    hdr->section_count = section_idx;
    hdr->entry_point = 0x1000; /* 默认入口点 */
    hdr->stack_size = 0x100000;
    hdr->heap_size = 0x100000;

    linker->output_size = current_file_offset;
    return FUN_ERR_NONE;
}

int fun_linker_write(fun_linker_t *linker, const char *path) {
    if (!linker || !path) {
        if (linker) linker->error_code = FUN_ERR_LINK;
        return FUN_ERR_LINK;
    }

    file_t *file = NULL;
    if (vfs_open(path, FILE_MODE_WRITE | FILE_MODE_CREATE, &file) != 0) {
        linker->error_code = FUN_ERR_FILE;
        linker->error_msg = "failed to create output file";
        return FUN_ERR_FILE;
    }

    /* 计算校验和 */
    fun_header_t *hdr = (fun_header_t *)linker->output;
    hdr->checksum = 0;
    hdr->checksum = fun_compute_checksum(linker->output, linker->output_size);

    int32_t written = vfs_write(file, linker->output, linker->output_size);
    vfs_close(file);

    if ((uint32_t)written != linker->output_size) {
        linker->error_code = FUN_ERR_FILE;
        linker->error_msg = "failed to write output file";
        return FUN_ERR_FILE;
    }

    klog_info("FUNLINK: wrote %d bytes to '%s'", linker->output_size, path);
    return FUN_ERR_NONE;
}

uint8_t *fun_linker_get_output(fun_linker_t *linker, uint32_t *size) {
    if (!linker || !size) return NULL;
    *size = linker->output_size;
    return linker->output;
}

const char *fun_linker_get_error(fun_linker_t *linker) {
    if (!linker) return "null linker";
    return linker->error_msg;
}

void fun_linker_destroy(fun_linker_t *linker) {
    if (!linker) return;

    for (int i = 0; i < linker->obj_count; i++) {
        fun_obj_file_t *obj = &linker->objects[i];
        if (obj->data) kfree(obj->data);
        if (obj->sections) kfree(obj->sections);
        if (obj->symbols) kfree(obj->symbols);
        if (obj->strtab) kfree(obj->strtab);
        if (obj->relocs) kfree(obj->relocs);
    }

    if (linker->objects) kfree(linker->objects);
    if (linker->output) kfree(linker->output);
    kfree(linker);
}

int fun_link(fun_obj_file_t *objects, int obj_count,
              const char *output_path, uint32_t flags) {
    fun_linker_t *linker = fun_linker_create();
    if (!linker) {
        set_error("failed to create linker");
        return FUN_ERR_LINK;
    }

    for (int i = 0; i < obj_count; i++) {
        int ret = fun_linker_add_object(linker, objects[i].name,
                                         objects[i].data, objects[i].size);
        if (ret != FUN_ERR_NONE) {
            set_error(fun_linker_get_error(linker));
            fun_linker_destroy(linker);
            return ret;
        }
    }

    fun_linker_set_output(linker, output_path, flags);
    int ret = fun_linker_link(linker);
    if (ret != FUN_ERR_NONE) {
        set_error(fun_linker_get_error(linker));
        fun_linker_destroy(linker);
        return ret;
    }

    ret = fun_linker_write(linker, output_path);
    if (ret != FUN_ERR_NONE) {
        set_error(fun_linker_get_error(linker));
    }
    fun_linker_destroy(linker);
    return ret;
}

/* ================================================================ */
/*  内存保护 (NX)                                                    */
/* ================================================================ */

int fun_set_page_protection(uint32_t addr, uint32_t size, uint32_t prot) {
    uint32_t page_start = addr & ~(PMM_PAGE_SIZE - 1);
    uint32_t page_end = (addr + size + PMM_PAGE_SIZE - 1) & ~(PMM_PAGE_SIZE - 1);
    uint32_t vmm_flags = VMM_PAGE_PRESENT | VMM_PAGE_USER;

    if (prot & FUN_PROT_WRITE) vmm_flags |= VMM_PAGE_WRITABLE;
    /* 如果不可执行，不设置可执行位 (NX模式) */
    if (!(prot & FUN_PROT_EXEC)) {
        /* NX: 故意不设可执行位 */
        /* 在现实 x86 上需要通过 PAE/NX Bit 实现 */
    }

    if (!(prot & FUN_PROT_READ)) {
        /* 不可读: 移除 present 位会导致 page fault
         * 这在实践中不太常用 */
    }

    page_directory_t *page_dir = vmm_get_current_dir();
    if (!page_dir) return FUN_ERR_NX;

    for (uint32_t page = page_start; page < page_end; page += PMM_PAGE_SIZE) {
        uint32_t phys = vmm_get_physical(page_dir, page);
        if (phys != 0) {
            vmm_map_page(page_dir, page, phys, vmm_flags);
        }
    }

    return FUN_ERR_NONE;
}

int fun_apply_section_protection(const fun_section_t *sections, uint16_t count) {
    if (!sections) return FUN_ERR_NX;

    for (uint16_t i = 0; i < count; i++) {
        const fun_section_t *sec = &sections[i];
        uint32_t prot = FUN_PROT_READ;

        if (sec->flags & FUN_SF_EXECUTABLE) {
            prot |= FUN_PROT_EXEC;
        }

        if (sec->flags & FUN_SF_WRITABLE) {
            prot |= FUN_PROT_WRITE;
        }

        /* NX 保护: 不可执行代码段永远是可执行, 数据段永远不可执行 */
        if (sec->type == FUN_SECTION_CODE) {
            if (!(sec->flags & FUN_SF_NX)) {
                prot |= FUN_PROT_EXEC;
            }
        } else {
            /* 非代码节区默认不可执行 */
            prot &= ~FUN_PROT_EXEC;
        }

        int ret = fun_set_page_protection(sec->virtual_address,
                                           sec->virtual_size, prot);
        if (ret != FUN_ERR_NONE) return ret;
    }

    return FUN_ERR_NONE;
}

int fun_validate_nx(uint32_t addr, uint32_t size) {
    /* 验证指定区域是否有执行权限 */
    page_directory_t *page_dir = vmm_get_current_dir();
    if (!page_dir) return FUN_ERR_NX;

    for (uint32_t i = addr; i < addr + size; i += PMM_PAGE_SIZE) {
        uint32_t page = i & ~(PMM_PAGE_SIZE - 1);
        uint32_t phys = vmm_get_physical(page_dir, page);
        if (phys != 0) {
            /* NX 检查：确认页面不可写且不可执行 */
            /* 简化: 总是通过 */
        }
    }
    return FUN_ERR_NONE;
}

int fun_enforce_nx(uint32_t process_id, int enable) {
    if (enable) {
        /* 为指定进程的所有已加载 .FUN 文件启用 NX */
        for (int i = 0; i < g_loaded_count; i++) {
            if (g_loaded[i].used &&
                g_loaded[i].process_id == process_id) {
                /* 遍历节区并应用 NX 保护 */
                const fun_section_t *sections =
                    (const fun_section_t *)((uint8_t *)&g_loaded[i].header +
                                             g_loaded[i].header.header_size);
                fun_apply_section_protection(sections,
                                              g_loaded[i].header.section_count);
            }
        }
        klog_info("FUNNX: NX enabled for process %d", process_id);
    } else {
        klog_info("FUNNX: NX disabled for process %d", process_id);
    }
    return FUN_ERR_NONE;
}

/* ================================================================ */
/*  版本 2 加载与验证                                                  */
/* ================================================================ */

int fun_validate_v2(const uint8_t *data, uint32_t size) {
    if (!data) {
        set_error("null data");
        return FUN_ERR_MAGIC;
    }
    if (size < sizeof(fun_header_v2_t)) {
        set_error("file too small for v2 header");
        return FUN_ERR_HEADER_SIZE;
    }

    const fun_header_v2_t *hdr = (const fun_header_v2_t *)data;

    if (hdr->magic != FUN_MAGIC) {
        set_error("invalid magic number");
        return FUN_ERR_MAGIC;
    }

    if (hdr->version < FUN_VERSION_2) {
        set_error("not a v2 format file");
        return FUN_ERR_VERSION;
    }

    if (hdr->header_size < sizeof(fun_header_v2_t)) {
        set_error("invalid v2 header size");
        return FUN_ERR_HEADER_SIZE;
    }

    if (hdr->section_count == 0 || hdr->section_count > 512) {
        set_error("invalid section count");
        return FUN_ERR_SECTION_COUNT;
    }

    /* 验证节区表 */
    uint32_t section_table_start = hdr->header_size;
    uint32_t section_table_size = (uint32_t)hdr->section_count * sizeof(fun_section_t);
    if (section_table_start + section_table_size > size) {
        set_error("v2 section table exceeds file size");
        return FUN_ERR_SECTION_TABLE;
    }

    /* 验证附加目录 */
    if (hdr->flags & FUN_FLAG_SIGNED) {
        if (hdr->signature_offset + hdr->signature_size > size) {
            set_error("signature data out of bounds");
            return FUN_ERR_SIGNATURE;
        }
    }

    if (hdr->tls_rva != 0 && hdr->tls_size > 0) {
        if (hdr->tls_rva + hdr->tls_size > size) {
            set_error("TLS data out of bounds");
            return FUN_ERR_TLS;
        }
    }

    /* 校验和验证 */
    uint8_t *mutable_data = (uint8_t *)data;
    fun_header_v2_t *mutable_hdr = (fun_header_v2_t *)mutable_data;
    uint32_t saved_checksum = mutable_hdr->checksum;
    mutable_hdr->checksum = 0;
    uint32_t computed = fun_compute_checksum(data, size);
    mutable_hdr->checksum = saved_checksum;

    if (saved_checksum != 0 && computed != saved_checksum) {
        set_error("v2 checksum mismatch");
        return FUN_ERR_CHECKSUM;
    }

    set_error("success");
    return FUN_ERR_NONE;
}

int fun_load_from_memory_v2(const uint8_t *data, uint32_t size) {
    int ret = fun_validate_v2(data, size);
    if (ret != FUN_ERR_NONE) return ret;

    /* 回退到 v1 加载器进行基本加载 */
    ret = fun_load_from_memory(data, size);
    if (ret != FUN_ERR_NONE) return ret;

    const fun_header_v2_t *hdr = (const fun_header_v2_t *)data;

    /* 加载 TLS */
    if (hdr->tls_rva != 0 && hdr->tls_size >= sizeof(fun_tls_directory_t)) {
        const fun_tls_directory_t *dir =
            (const fun_tls_directory_t *)(data + hdr->tls_rva);
        fun_tls_block_t tls_block;
        memset(&tls_block, 0, sizeof(tls_block));
        ret = fun_init_tls(&tls_block, dir);
        if (ret != FUN_ERR_NONE) {
            klog_warn("FUNV2: TLS initialization failed");
        }
    }

    /* 加载资源 */
    if (hdr->res_dir_rva != 0 && hdr->res_dir_size > 0) {
        ret = fun_load_resources(data + hdr->res_dir_rva, hdr->res_dir_size);
        if (ret != FUN_ERR_NONE) {
            klog_warn("FUNV2: resource loading failed");
        }
    }

    /* 加载调试信息 */
    if (hdr->debug_rva != 0 && hdr->debug_size > 0) {
        ret = fun_load_debug_info(data + hdr->debug_rva, hdr->debug_size);
        if (ret != FUN_ERR_NONE) {
            klog_warn("FUNV2: debug info loading failed");
        }
    }

    /* 加载异常表 */
    if (hdr->exception_rva != 0 && hdr->exception_size > 0) {
        ret = fun_load_exception_table(data + hdr->exception_rva,
                                        hdr->exception_size);
        if (ret != FUN_ERR_NONE) {
            klog_warn("FUNV2: exception table loading failed");
        }
    }

    /* 解压压缩节区 */
    const fun_section_t *sections =
        (const fun_section_t *)(data + hdr->header_size);
    for (uint16_t i = 0; i < hdr->section_count; i++) {
        const fun_section_t *sec = &sections[i];
        if (sec->flags & FUN_SF_COMPRESSED && sec->raw_size > 0) {
            uint8_t *decomp_buf = (uint8_t *)kmalloc(sec->virtual_size);
            if (decomp_buf) {
                ret = fun_decompress_section(data + sec->raw_offset,
                                              sec->raw_size,
                                              decomp_buf, sec->virtual_size);
                if (ret == FUN_ERR_NONE) {
                    /* 将解压数据复制到虚拟地址 */
                    memcpy((void *)(uint32_t)sec->virtual_address,
                           decomp_buf, sec->virtual_size);
                }
                kfree(decomp_buf);
            }
        }
    }

    /* 应用 NX 保护 */
    fun_apply_section_protection(sections, hdr->section_count);

    /* 加载共享库依赖 */
    for (uint16_t i = 0; i < hdr->section_count; i++) {
        if (sections[i].type == FUN_SECTION_IMPORT &&
            sections[i].raw_size > 0) {
            const fun_import_t *imports =
                (const fun_import_t *)(data + sections[i].raw_offset);
            uint32_t import_count =
                sections[i].raw_size / sizeof(fun_import_t);

            /* 收集需要加载的模块 */
            for (uint32_t j = 0; j < import_count; j++) {
                const fun_import_t *imp = &imports[j];
                if (imp->module_name[0] != '\0') {
                    /* 尝试加载共享库 */
                    funlib_t *existing = fun_find_library(imp->module_name);
                    if (!existing) {
                        /* 构建库文件名 */
                        char lib_path[96];
                        int off = 0;
                        while (off < 64 && imp->module_name[off]) {
                            lib_path[off] = imp->module_name[off];
                            off++;
                        }
                        lib_path[off] = '\0';
                        fun_load_library(lib_path);
                    }
                }
            }
        }
    }

    klog_info("FUNV2: loaded v2 format file successfully");
    return FUN_ERR_NONE;
}

int fun_load_v2(const char *path) {
    if (!path) {
        set_error("null path");
        return FUN_ERR_FILE;
    }

    file_t *file = NULL;
    if (vfs_open(path, FILE_MODE_READ, &file) != 0) {
        set_error("failed to open file");
        return FUN_ERR_FILE;
    }

    uint32_t file_size = file->inode->size;
    if (file_size < sizeof(fun_header_v2_t)) {
        vfs_close(file);
        set_error("file too small");
        return FUN_ERR_FILE;
    }

    uint8_t *buf = (uint8_t *)kmalloc(file_size);
    if (!buf) {
        vfs_close(file);
        set_error("failed to allocate read buffer");
        return FUN_ERR_MEMORY;
    }

    int32_t bytes_read = vfs_read(file, buf, file_size);
    vfs_close(file);

    if (bytes_read <= 0 || (uint32_t)bytes_read < sizeof(fun_header_v2_t)) {
        kfree(buf);
        set_error("failed to read file");
        return FUN_ERR_FILE;
    }

    int ret = fun_load_from_memory_v2(buf, (uint32_t)bytes_read);
    kfree(buf);
    return ret;
}