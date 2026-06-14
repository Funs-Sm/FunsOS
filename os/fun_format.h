#ifndef FUN_FORMAT_H
#define FUN_FORMAT_H

#include "stdint.h"

/* .FUN 可执行文件格式
 * 类似 PE/ELF 的简化格式，专为 FunsOS 设计
 * 文件头 + 节区表 + 节区数据
 */

#define FUN_MAGIC       0x4E55462E  /* ".FUN" */
#define FUN_VERSION     1

/* 节区类型 */
#define FUN_SECTION_CODE        0x0001
#define FUN_SECTION_DATA        0x0002
#define FUN_SECTION_BSS         0x0003
#define FUN_SECTION_RODATA      0x0004
#define FUN_SECTION_RESOURCE    0x0005
#define FUN_SECTION_IMPORT      0x0006
#define FUN_SECTION_EXPORT      0x0007
#define FUN_SECTION_RELOC       0x0008

/* 节区标志 */
#define FUN_SF_EXECUTABLE       0x0001
#define FUN_SF_READABLE         0x0002
#define FUN_SF_WRITABLE         0x0004
#define FUN_SF_SHARED           0x0008
#define FUN_SF_DISCARDABLE      0x0010

/* 文件标志 */
#define FUN_FLAG_GUI            0x0001
#define FUN_FLAG_CONSOLE        0x0002
#define FUN_FLAG_DYNAMIC        0x0004
#define FUN_FLAG_STRIP          0x0008

/* 重定位类型 */
#define FUN_RELOC_ABSOLUTE      0
#define FUN_RELOC_RELATIVE      1
#define FUN_RELOC_SYMBOL        2

/* 错误码 */
#define FUN_ERR_NONE            0
#define FUN_ERR_MAGIC          -1
#define FUN_ERR_VERSION        -2
#define FUN_ERR_HEADER_SIZE    -3
#define FUN_ERR_CHECKSUM       -4
#define FUN_ERR_SECTION_COUNT  -5
#define FUN_ERR_SECTION_TABLE  -6
#define FUN_ERR_MEMORY         -7
#define FUN_ERR_FILE           -8
#define FUN_ERR_IMPORT         -9
#define FUN_ERR_RELOC          -10
#define FUN_ERR_ENTRY          -11
#define FUN_ERR_PROCESS        -12

/* 文件头 (64 字节) */
typedef struct __attribute__((packed)) {
    uint32_t magic;           /* 魔数 ".FUN" */
    uint16_t version;         /* 格式版本 */
    uint16_t header_size;     /* 文件头大小 */
    uint32_t entry_point;     /* 入口点偏移(RVA) */
    uint32_t stack_size;      /* 栈大小 */
    uint32_t heap_size;       /* 堆大小 */
    uint32_t base_address;    /* 首选基址 */
    uint16_t section_count;   /* 节区数量 */
    uint16_t flags;           /* 标志 */
    uint32_t timestamp;       /* 编译时间戳 */
    uint32_t checksum;        /* 校验和 */
    uint32_t icon_offset;     /* 图标偏移 */
    uint32_t icon_size;       /* 图标大小 */
    uint32_t reserved[3];     /* 保留 */
} fun_header_t;

/* 节区头 (32 字节) */
typedef struct __attribute__((packed)) {
    char name[16];            /* 节区名称 */
    uint32_t type;            /* 节区类型 */
    uint32_t flags;           /* 节区标志 */
    uint32_t virtual_address; /* 虚拟地址(RVA) */
    uint32_t virtual_size;    /* 虚拟大小 */
    uint32_t raw_offset;      /* 文件偏移 */
    uint32_t raw_size;        /* 文件大小 */
} fun_section_t;

/* 导入表项 */
typedef struct __attribute__((packed)) {
    char module_name[32];     /* 模块名 */
    char function_name[64];   /* 函数名 */
    uint32_t import_rva;      /* 导入地址RVA */
} fun_import_t;

/* 重定位项 */
typedef struct __attribute__((packed)) {
    uint32_t offset;          /* 重定位偏移 */
    uint8_t type;             /* 重定位类型 */
    uint8_t symbol;           /* 符号索引 */
    uint16_t addend;          /* 附加值 */
} fun_reloc_t;

/* 导出表项 */
typedef struct __attribute__((packed)) {
    char name[64];            /* 导出名称 */
    uint32_t rva;             /* 导出RVA */
    uint32_t ordinal;         /* 序号 */
} fun_export_t;

/* 加载器 API */
void fun_loader_init(void);
int fun_load(const char *path);
int fun_load_from_memory(const uint8_t *data, uint32_t size);
int fun_execute(const char *path, int argc, char **argv);
int fun_validate(const uint8_t *data, uint32_t size);
int fun_unload(uint32_t process_id);
void fun_list_loaded(void);
int fun_get_entry_point(const uint8_t *data, uint32_t *entry);
const char *fun_get_error(void);

#endif /* FUN_FORMAT_H */