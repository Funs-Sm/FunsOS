#ifndef FUN_FORMAT_H
#define FUN_FORMAT_H

#include "stdint.h"

/* .FUN 可执行文件格式
 * 类似 PE/ELF 的简化格式，专为 FunsOS 设计
 * 文件头 + 节区表 + 节区数据
 */

#define FUN_MAGIC       0x4E55462E  /* ".FUN" */
#define FUN_VERSION     1
#define FUN_VERSION_2   2

/* 节区类型 */
#define FUN_SECTION_CODE        0x0001
#define FUN_SECTION_DATA        0x0002
#define FUN_SECTION_BSS         0x0003
#define FUN_SECTION_RODATA      0x0004
#define FUN_SECTION_RESOURCE    0x0005
#define FUN_SECTION_IMPORT      0x0006
#define FUN_SECTION_EXPORT      0x0007
#define FUN_SECTION_RELOC       0x0008
#define FUN_SECTION_TLS         0x0009   /* 线程局部存储 */
#define FUN_SECTION_DEBUG_INFO  0x000A   /* 调试信息 */
#define FUN_SECTION_DEBUG_LINE  0x000B   /* 行号表 */
#define FUN_SECTION_DEBUG_FRAME 0x000C   /* 调用帧信息 */
#define FUN_SECTION_EXCEPTION   0x000D   /* 异常处理表 */
#define FUN_SECTION_GOT         0x000E   /* 全局偏移表 (GOT) */
#define FUN_SECTION_PLT         0x000F   /* 过程链接表 (PLT) */
#define FUN_SECTION_DYNSYM      0x0010   /* 动态符号表 */
#define FUN_SECTION_DYNSTR      0x0011   /* 动态字符串表 */
#define FUN_SECTION_HASH        0x0012   /* 符号哈希表 */
#define FUN_SECTION_SIGNATURE   0x0013   /* 数字签名 */
#define FUN_SECTION_RES_DIR     0x0014   /* 资源目录 */
#define FUN_SECTION_COMPRESSED  0x0015   /* 压缩节区(LZ4) */

/* 节区标志 */
#define FUN_SF_EXECUTABLE       0x0001
#define FUN_SF_READABLE         0x0002
#define FUN_SF_WRITABLE         0x0004
#define FUN_SF_SHARED           0x0008
#define FUN_SF_DISCARDABLE      0x0010
#define FUN_SF_COMPRESSED       0x0020   /* v2: 节区已压缩 */
#define FUN_SF_NX               0x0040   /* v2: 不可执行(安全) */

/* 文件标志 */
#define FUN_FLAG_GUI            0x0001
#define FUN_FLAG_CONSOLE        0x0002
#define FUN_FLAG_DYNAMIC        0x0004
#define FUN_FLAG_STRIP          0x0008
#define FUN_FLAG_64BIT          0x0010   /* v2: 64位程序 */
#define FUN_FLAG_SIGNED         0x0020   /* v2: 已签名 */
#define FUN_FLAG_LARGE_ADDR     0x0040   /* v2: 大地址空间感知 */

/* 重定位类型 */
#define FUN_RELOC_ABSOLUTE      0
#define FUN_RELOC_RELATIVE      1
#define FUN_RELOC_SYMBOL        2
#define FUN_RELOC_GOTPC         3       /* v2: GOT相对寻址 */
#define FUN_RELOC_PLT32         4       /* v2: PLT相对寻址 */
#define FUN_RELOC_TLS_DTPMOD    5       /* v2: TLS模块ID */
#define FUN_RELOC_TLS_DTPOFF    6       /* v2: TLS偏移 */
#define FUN_RELOC_TLS_TPOFF     7       /* v2: TLS线程指针偏移 */

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
#define FUN_ERR_SIGNATURE      -13  /* v2: 签名验证失败 */
#define FUN_ERR_FUNLIB         -14  /* v2: 共享库加载失败 */
#define FUN_ERR_TLS            -15  /* v2: TLS初始化失败 */
#define FUN_ERR_RESOURCE       -16  /* v2: 资源加载失败 */
#define FUN_ERR_DEBUG          -17  /* v2: 调试信息错误 */
#define FUN_ERR_EXCEPTION      -18  /* v2: 异常处理表错误 */
#define FUN_ERR_COMPRESS       -19  /* v2: 解压失败 */
#define FUN_ERR_LINK           -20  /* v2: 链接错误 */
#define FUN_ERR_NX             -21  /* v2: 内存保护错误 */

/* ================================================================ */
/*  签名算法类型                                                     */
/* ================================================================ */
#define FUN_SIG_RSA2048         1
#define FUN_SIG_ECDSA_P256      2
#define FUN_SIG_ECDSA_P384      3

/* 资源类型 */
#define FUN_RES_ICON            1
#define FUN_RES_STRING          2
#define FUN_RES_BITMAP          3
#define FUN_RES_FONT            4
#define FUN_RES_CURSOR          5
#define FUN_RES_MENU            6
#define FUN_RES_DIALOG          7
#define FUN_RES_ACCELERATOR     8
#define FUN_RES_VERSION_INFO    9
#define FUN_RES_MANIFEST        10

/* 调试信息类型 */
#define FUN_DEBUG_LINE           1
#define FUN_DEBUG_SYMBOL         2
#define FUN_DEBUG_FRAME          3
#define FUN_DEBUG_ARANGES        4
#define FUN_DEBUG_PUBNAMES       5

/* 压缩算法 */
#define FUN_COMPRESS_NONE        0
#define FUN_COMPRESS_LZ4         1

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

/* 文件头 v2 (96 字节 - 扩展头) */
typedef struct __attribute__((packed)) {
    uint32_t magic;           /* 魔数 ".FUN" */
    uint16_t version;         /* 格式版本 (>= 2) */
    uint16_t header_size;     /* 文件头大小 */
    uint32_t entry_point;     /* 入口点偏移(RVA) */
    uint64_t stack_size;      /* 栈大小 (64位) */
    uint64_t heap_size;       /* 堆大小 (64位) */
    uint64_t base_address;    /* 首选基址 (64位) */
    uint16_t section_count;   /* 节区数量 */
    uint16_t flags;           /* 标志 */
    uint32_t timestamp;       /* 编译时间戳 */
    uint32_t checksum;        /* 校验和 */
    uint32_t icon_offset;     /* 图标偏移 */
    uint32_t icon_size;       /* 图标大小 */
    uint32_t signature_offset; /* 签名数据偏移 */
    uint32_t signature_size;  /* 签名数据大小 */
    uint32_t tls_rva;         /* TLS目录RVA */
    uint32_t tls_size;        /* TLS目录大小 */
    uint32_t res_dir_rva;     /* 资源目录RVA */
    uint32_t res_dir_size;    /* 资源目录大小 */
    uint32_t debug_rva;       /* 调试信息RVA */
    uint32_t debug_size;      /* 调试信息大小 */
    uint32_t exception_rva;   /* 异常处理表RVA */
    uint32_t exception_size;  /* 异常处理表大小 */
    uint32_t reserved;        /* 保留 */
} fun_header_v2_t;

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

/* 重定位项 v2 (64位支持) */
typedef struct __attribute__((packed)) {
    uint64_t offset;          /* 重定位偏移 (64位) */
    uint8_t type;             /* 重定位类型 */
    uint8_t symbol;           /* 符号索引 */
    uint16_t addend;          /* 附加值 */
    uint32_t addend2;         /* 附加高32位 */
} fun_reloc_v2_t;

/* 导出表项 */
typedef struct __attribute__((packed)) {
    char name[64];            /* 导出名称 */
    uint32_t rva;             /* 导出RVA */
    uint32_t ordinal;         /* 序号 */
} fun_export_t;

/* ================================================================ */
/*  数字签名结构                                                     */
/* ================================================================ */

/* 数字签名头 */
typedef struct __attribute__((packed)) {
    uint32_t sig_algorithm;   /* 签名算法 */
    uint32_t sig_length;      /* 签名数据长度 */
    uint32_t cert_length;     /* 证书链长度 */
    uint32_t hash_algorithm;  /* 哈希算法: 1=SHA256, 2=SHA512 */
    uint8_t reserved[16];     /* 保留 */
    /* 后面跟随: 签名数据[sig_length] + 证书链[cert_length] */
} fun_signature_t;

/* 公钥结构 (简化) */
typedef struct __attribute__((packed)) {
    uint32_t algorithm;       /* 密钥算法 */
    uint32_t key_length;      /* 密钥长度 */
    uint8_t key_data[256];    /* 密钥数据 */
} fun_public_key_t;

/* ================================================================ */
/*  共享库(.FUNLIB) 结构                                             */
/* ================================================================ */

/* 动态符号表项 */
typedef struct __attribute__((packed)) {
    char name[64];            /* 符号名称 */
    uint32_t value;           /* 符号值/地址 */
    uint32_t size;            /* 符号大小 */
    uint8_t type;             /* 符号类型 */
    uint8_t bind;             /* 绑定: 0=LOCAL, 1=GLOBAL, 2=WEAK */
    uint8_t visibility;       /* 可见性 */
    uint8_t section_index;    /* 所在节区索引 */
} fun_dynsym_t;

/* 符号类型 */
#define FUN_STT_NOTYPE  0
#define FUN_STT_OBJECT  1
#define FUN_STT_FUNC    2
#define FUN_STT_SECTION 3
#define FUN_STT_FILE    4
#define FUN_STT_TLS     6

/* 符号绑定 */
#define FUN_STB_LOCAL   0
#define FUN_STB_GLOBAL  1
#define FUN_STB_WEAK    2

/* 符号版本 */
typedef struct __attribute__((packed)) {
    uint16_t version;         /* 版本号 */
    uint16_t flags;           /* 标志 */
    uint16_t name_index;      /* 版本名称在dynstr中的索引 */
    uint16_t next;            /* 下一个版本定义 */
} fun_verdef_t;

/* 版本需求 */
typedef struct __attribute__((packed)) {
    uint16_t version;         /* 所需版本 */
    uint16_t flags;           /* 标志 */
    uint16_t name_index;      /* 文件名在dynstr中的索引 */
    uint16_t next;            /* 下一个版本需求 */
} fun_verneed_t;

/* GOT/PLT 条目 */
typedef struct __attribute__((packed)) {
    uint32_t got_rva;         /* GOT条目RVA */
    uint32_t plt_rva;         /* PLT条目RVA */
    uint32_t sym_index;       /* 对应动态符号索引 */
    uint32_t flags;           /* 0=pending_bind, 1=bound */
} fun_gotplt_t;

/* 共享库描述符 */
struct funlib_s;
typedef struct funlib_s __attribute__((packed)) funlib_t;
struct funlib_s {
    char name[64];            /* 库名 */
    uint32_t base;            /* 加载基址 */
    uint32_t size;            /* 库大小 */
    uint32_t entry;           /* 入口点(若有) */
    int ref_count;            /* 引用计数 */
    uint32_t dynsym_count;    /* 动态符号数 */
    fun_dynsym_t *dynsym;     /* 动态符号表 */
    char *dynstr;             /* 动态字符串表 */
    uint32_t *hash;           /* 哈希表 */
    uint32_t *got;            /* 全局偏移表 */
    funlib_t *next;           /* 链表 */
};

/* 共享库加载上下文 */
typedef struct {
    funlib_t *loaded_libs;    /* 已加载库列表 */
    int lib_count;            /* 库计数 */
    uint32_t next_plt_slot;   /* 下一个PLT槽位 */
} fun_lib_context_t;

/* ================================================================ */
/*  资源嵌入结构                                                     */
/* ================================================================ */

/* 资源目录头 */
typedef struct __attribute__((packed)) {
    uint32_t resource_count;  /* 资源条目数 */
    uint32_t string_table_offset; /* 字符串表偏移 */
    uint32_t string_table_size;   /* 字符串表大小 */
    uint32_t data_offset;     /* 资源数据起始偏移 */
} fun_res_dir_header_t;

/* 资源目录项 */
typedef struct __attribute__((packed)) {
    uint32_t type;            /* 资源类型 */
    uint32_t name_offset;     /* 名称在字符串表中的偏移 */
    uint32_t language;        /* 语言ID */
    uint32_t data_offset;     /* 数据偏移(相对data_offset) */
    uint32_t data_size;       /* 数据大小 */
    uint32_t flags;           /* 标志 */
    uint32_t reserved;        /* 保留 */
} fun_res_dir_entry_t;

/* 字符串表项 */
typedef struct __attribute__((packed)) {
    uint16_t id;              /* 字符串ID */
    uint16_t length;          /* 字符串长度 */
    /* 后跟UTF-16字符串 */
} fun_string_entry_t;

/* 位图资源头 */
typedef struct __attribute__((packed)) {
    uint32_t width;
    uint32_t height;
    uint16_t bpp;             /* 每像素位数 */
    uint16_t format;          /* 0=RGBA, 1=BGRA, 2=RGB */
    uint32_t compressed;      /* 0=未压缩, 1=RLE, 2=LZ4 */
    uint32_t pixel_data_size; /* 像素数据大小 */
} fun_bitmap_header_t;

/* 加载的资源描述符 */
typedef struct {
    uint32_t type;
    char name[128];
    uint8_t *data;
    uint32_t size;
    uint32_t language;
    int ref_count;
} fun_resource_t;

/* ================================================================ */
/*  调试信息结构                                                     */
/* ================================================================ */

/* 调试信息头 */
typedef struct __attribute__((packed)) {
    uint32_t debug_type;      /* 调试信息类型 */
    uint32_t size;            /* 总大小 */
    uint32_t version;         /* 调试格式版本 */
    uint32_t reserved;        /* 保留 */
} fun_debug_header_t;

/* 行号表项 */
typedef struct __attribute__((packed)) {
    uint32_t address;         /* 代码地址 */
    uint32_t file_index;      /* 文件名索引 */
    uint32_t line_number;     /* 行号 */
    uint32_t column_number;   /* 列号 */
    uint32_t flags;           /* 标志: 0x1=序言结束, 0x2=结尾 */
} fun_debug_line_entry_t;

/* 符号表项 */
typedef struct __attribute__((packed)) {
    char name[64];            /* 符号名称 */
    uint32_t address;         /* 符号地址 */
    uint32_t size;            /* 符号大小 */
    uint8_t type;             /* 符号类型 */
    uint8_t scope;            /* 作用域: 0=局部, 1=全局 */
    uint16_t file_index;      /* 所在文件索引 */
    uint32_t line_number;     /* 声明行号 */
} fun_debug_symbol_t;

/* 调用帧信息 */
typedef struct __attribute__((packed)) {
    uint32_t code_begin;      /* 代码区起始 */
    uint32_t code_length;     /* 代码区长度 */
    uint32_t frame_size;      /* 栈帧大小 */
    uint32_t saved_reg_count; /* 保存的寄存器数 */
    uint8_t  saved_regs[16];  /* 保存的寄存器编号 */
    uint32_t cfa_offset;      /* CFA偏移 */
} fun_debug_frame_t;

/* ================================================================ */
/*  TLS 结构                                                        */
/* ================================================================ */

/* TLS 目录 */
typedef struct __attribute__((packed)) {
    uint32_t start_rva;       /* TLS模板起始RVA */
    uint32_t end_rva;         /* TLS模板结束RVA */
    uint32_t index_rva;       /* TLS索引RVA */
    uint32_t callback_rva;    /* TLS回调函数RVA */
    uint32_t zero_fill_size;  /* 零填充大小 */
    uint32_t alignment;       /* 对齐要求 */
    uint32_t flags;           /* 标志 */
} fun_tls_directory_t;

/* 每个线程的 TLS 块 */
typedef struct {
    uint8_t *tls_data;        /* TLS数据 */
    uint32_t tls_size;        /* TLS大小 */
    int initialized;          /* 是否已初始化 */
} fun_tls_block_t;

/* ================================================================ */
/*  异常处理结构                                                     */
/* ================================================================ */

/* 异常处理目录 */
typedef struct __attribute__((packed)) {
    uint32_t version;         /* 版本 */
    uint32_t flags;           /* 标志 */
    uint32_t entry_count;     /* 条目数 */
    uint32_t reserved;        /* 保留 */
} fun_exception_dir_t;

/* 异常处理条目 (展开信息) */
typedef struct __attribute__((packed)) {
    uint32_t begin_address;   /* 受保护区域起始 */
    uint32_t end_address;     /* 受保护区域结束 */
    uint32_t unwind_info;     /* 展开信息偏移 */
    uint32_t handler_rva;     /* 异常处理器RVA */
    uint32_t handler_data;    /* 处理器数据 */
    uint32_t personality_rva; /* 个性函数RVA */
} fun_exception_entry_t;

/* 展开信息 */
typedef struct __attribute__((packed)) {
    uint8_t version;          /* 版本 */
    uint8_t flags;            /* 0x1=有异常处理器, 0x2=终止 */
    uint8_t prolog_size;      /* 序言大小 */
    uint8_t code_count;       /* 栈帧展开码数量 */
    uint32_t frame_register;  /* 帧寄存器 */
    uint32_t frame_offset;    /* 帧偏移 */
    /* 后跟展开码数组 */
} fun_unwind_info_t;

/* ================================================================ */
/*  压缩节区头                                                      */
/* ================================================================ */

typedef struct __attribute__((packed)) {
    uint32_t algorithm;       /* 压缩算法: FUN_COMPRESS_LZ4 */
    uint32_t original_size;   /* 原始大小 */
    uint32_t compressed_size; /* 压缩后大小 */
    uint32_t checksum;        /* 压缩数据校验和 */
} fun_compress_header_t;

/* ================================================================ */
/*  链接器输入文件描述符                                              */
/* ================================================================ */

/* 目标文件(.o)描述符 */
typedef struct {
    char name[64];            /* 文件名 */
    uint8_t *data;            /* 文件数据 */
    uint32_t size;            /* 文件大小 */
    fun_section_t *sections;  /* 节区表 */
    uint16_t section_count;   /* 节区数 */
    fun_dynsym_t *symbols;    /* 符号表 */
    uint32_t symbol_count;    /* 符号数 */
    char *strtab;             /* 字符串表 */
    uint32_t strtab_size;     /* 字符串表大小 */
    fun_reloc_t *relocs;      /* 重定位表 */
    uint32_t reloc_count;     /* 重定位数 */
} fun_obj_file_t;

/* 链接器状态 */
typedef struct {
    fun_obj_file_t *objects;  /* 输入对象文件 */
    int obj_count;            /* 对象文件数 */
    int obj_capacity;         /* 对象文件容量 */
    uint8_t *output;          /* 输出缓冲区 */
    uint32_t output_size;     /* 输出大小 */
    uint32_t output_capacity; /* 输出缓冲区容量 */
    char *error_msg;          /* 错误信息 */
    int error_code;           /* 错误码 */
} fun_linker_t;

/* ================================================================ */
/*  内存保护(NX)结构                                                */
/* ================================================================ */

/* 内存区域权限 */
#define FUN_PROT_NONE   0
#define FUN_PROT_READ   1
#define FUN_PROT_WRITE  2
#define FUN_PROT_EXEC   4

/* 内存区域描述符 */
typedef struct {
    uint32_t start_addr;      /* 起始地址 */
    uint32_t end_addr;        /* 结束地址 */
    uint32_t permissions;     /* 权限位 */
    int is_code;              /* 是否为代码段 */
} fun_mem_region_t;

/* ================================================================ */
/*  加载器 v1 API (保留)                                             */
/* ================================================================ */

void fun_loader_init(void);
int fun_load(const char *path);
int fun_load_from_memory(const uint8_t *data, uint32_t size);
int fun_execute(const char *path, int argc, char **argv);
int fun_validate(const uint8_t *data, uint32_t size);
int fun_unload(uint32_t process_id);
void fun_list_loaded(void);
int fun_get_entry_point(const uint8_t *data, uint32_t *entry);
const char *fun_get_error(void);

/* ================================================================ */
/*  v2 扩展 API                                                      */
/* ================================================================ */

/* 签名验证 */
int fun_verify_signature(const uint8_t *data, uint32_t size,
                          fun_public_key_t *pubkey);
int fun_sign_data(uint8_t *data, uint32_t size,
                   fun_public_key_t *privkey, uint8_t *sig_out, uint32_t *sig_len);
int fun_hash_data(const uint8_t *data, uint32_t size,
                   uint8_t *hash_out, uint32_t *hash_len);
int fun_verify_rsa(const uint8_t *data, uint32_t data_size,
                    const uint8_t *sig, uint32_t sig_size,
                    const fun_public_key_t *pubkey);
int fun_verify_ecdsa(const uint8_t *data, uint32_t data_size,
                      const uint8_t *sig, uint32_t sig_size,
                      const fun_public_key_t *pubkey);

/* 共享库 */
int fun_load_library(const char *path);
funlib_t *fun_find_library(const char *name);
int fun_unload_library(funlib_t *lib);
void *fun_dlsym(funlib_t *lib, const char *symbol);
int fun_resolve_plt(funlib_t *lib, uint32_t plt_index);
int fun_bind_lazy(funlib_t *lib);
int fun_check_verdef(funlib_t *lib, uint16_t required_version);
int fun_check_verneed(funlib_t *lib, uint16_t need_version);

/* 资源管理 */
int fun_load_resources(const uint8_t *data, uint32_t size);
fun_resource_t *fun_find_resource(uint32_t type, const char *name, uint32_t lang);
void *fun_load_resource_data(uint32_t type, const char *name, uint32_t *size);
fun_bitmap_header_t *fun_load_bitmap_resource(const char *name);
const char *fun_load_string_resource(uint16_t string_id, uint32_t lang);
int fun_free_resource(fun_resource_t *res);
uint32_t fun_get_resource_count(uint32_t type);

/* 调试信息 */
int fun_load_debug_info(const uint8_t *data, uint32_t size);
fun_debug_line_entry_t *fun_lookup_line(uint32_t address);
fun_debug_symbol_t *fun_lookup_symbol(const char *name);
fun_debug_symbol_t *fun_lookup_address(uint32_t address);
int fun_get_line_info(uint32_t address, char *file_out, int *line_out, int *col_out);
int fun_get_frame_info(uint32_t address, fun_debug_frame_t *frame_out);
void fun_dump_stack_trace(uint32_t *stack, uint32_t depth);

/* TLS */
int fun_init_tls(fun_tls_block_t *tls_block, const fun_tls_directory_t *dir);
void *fun_get_tls_pointer(void);
int fun_set_tls_pointer(void *ptr);
int fun_run_tls_callbacks(void);
void fun_cleanup_tls(fun_tls_block_t *tls_block);

/* 异常处理 */
int fun_load_exception_table(const uint8_t *data, uint32_t size);
fun_exception_entry_t *fun_find_exception_handler(uint32_t address);
fun_unwind_info_t *fun_get_unwind_info(uint32_t address);
int fun_unwind_stack(uint32_t *stack_ptr, uint32_t *frame_out);
int fun_register_exception_handler(fun_exception_entry_t *handler);
int fun_dispatch_exception(uint32_t exception_code, uint32_t exception_addr);

/* 压缩 */
int fun_decompress_section(const uint8_t *compressed_data, uint32_t comp_size,
                            uint8_t *output, uint32_t output_size);
int fun_decompress_lz4(const uint8_t *src, uint32_t src_size,
                        uint8_t *dst, uint32_t dst_size);
int fun_compress_lz4(const uint8_t *src, uint32_t src_size,
                      uint8_t *dst, uint32_t *dst_size);

/* 链接器 */
fun_linker_t *fun_linker_create(void);
int fun_linker_add_object(fun_linker_t *linker, const char *filename,
                           uint8_t *data, uint32_t size);
int fun_linker_set_output(fun_linker_t *linker, const char *name, uint32_t flags);
int fun_linker_link(fun_linker_t *linker);
int fun_linker_write(fun_linker_t *linker, const char *path);
uint8_t *fun_linker_get_output(fun_linker_t *linker, uint32_t *size);
const char *fun_linker_get_error(fun_linker_t *linker);
void fun_linker_destroy(fun_linker_t *linker);
/* 便捷函数 */
int fun_link(fun_obj_file_t *objects, int obj_count,
              const char *output_path, uint32_t flags);

/* 内存保护 (NX) */
int fun_set_page_protection(uint32_t addr, uint32_t size, uint32_t prot);
int fun_apply_section_protection(const fun_section_t *sections, uint16_t count);
int fun_validate_nx(uint32_t addr, uint32_t size);
int fun_enforce_nx(uint32_t process_id, int enable);

/* 版本2 加载/验证 */
int fun_validate_v2(const uint8_t *data, uint32_t size);
int fun_load_v2(const char *path);
int fun_load_from_memory_v2(const uint8_t *data, uint32_t size);

#endif /* FUN_FORMAT_H */