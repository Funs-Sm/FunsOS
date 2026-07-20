#ifndef KMOD_H
#define KMOD_H

#include "stdint.h"

/* ============================================================
 * Kmod - Kernel Module Management 内核模块管理器
 *
 * 管理内核模块生命周期，支持内置模块和可加载模块。
 * 与 kmodule(ELF加载器) 不同，这是高层模块管理接口。
 * ============================================================ */

#define KMOD_MAX_MODULES        64
#define KMOD_NAME_MAX           64
#define KMOD_MAX_SYMBOLS        32
#define KMOD_MAX_DEPS            8
#define KMOD_SYMBOL_NAME_MAX    64

typedef enum {
    KMOD_STATE_LIVE       = 0,   /* 模块已加载并运行 */
    KMOD_STATE_LOADING    = 1,   /* 正在加载 */
    KMOD_STATE_UNLOADING  = 2,   /* 正在卸载 */
    KMOD_STATE_COMING     = 3,   /* 初始化中 */
    KMOD_STATE_GOING      = 4,   /* 退出中 */
} kmod_state_t;

typedef int (*kmod_init_func_t)(void);
typedef void (*kmod_exit_func_t)(void);

typedef struct kmod_symbol {
    char     name[KMOD_SYMBOL_NAME_MAX];
    uint32_t addr;
} kmod_symbol_t;

typedef struct kmodule_struct {
    char              name[KMOD_NAME_MAX];
    int               id;
    int               used;
    kmod_state_t      state;
    kmod_init_func_t  init;
    kmod_exit_func_t  exit;
    uint32_t          refcnt;
    uint32_t          size;
    char              version[32];
    char              deps[KMOD_MAX_DEPS][KMOD_NAME_MAX];
    uint32_t          dep_count;
    kmod_symbol_t     syms[KMOD_MAX_SYMBOLS];
    uint32_t          sym_count;
    uint64_t          loaded_time;
    uint64_t          last_used;
} kmod_t;

/* ============================================================
 * 初始化
 * ============================================================ */
int kmod_init(void);

/* ============================================================
 * 核心 API
 * ============================================================ */

kmod_t *init_module(const char *name, kmod_init_func_t init, kmod_exit_func_t exit);
int delete_module(const char *name);
kmod_t *find_module(const char *name);
void module_get(kmod_t *mod);
void module_put(kmod_t *mod);

int kmod_register_builtin(const char *name, kmod_init_func_t init,
                          kmod_exit_func_t exit, const char *version);
int kmod_load_builtin(const char *name);
uint32_t kmod_get_count(void);
uint32_t kmod_get_live_count(void);

/* ============================================================
 * 统计与调试
 * ============================================================ */
void kmod_print_stats(void);

#endif /* KMOD_H */
