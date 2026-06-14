#ifndef KMODULE_H
#define KMODULE_H

#include "stdint.h"
#include "stddef.h"

/*
 * Kernel Module System
 *
 * Supports loading relocatable ELF (.ko) files into the running kernel.
 * Modules can export symbols for other modules to use, and can reference
 * kernel symbols exported via EXPORT_SYMBOL().
 *
 * Module lifecycle:
 *   1. Module is loaded from initrd or disk via kmodule_load()
 *   2. Symbols are resolved against the kernel symbol table (ksym)
 *   3. ELF relocations (R_386_32, R_386_PC32) are applied
 *   4. Module's init() function is called
 *   5. Module runs until kmodule_unload() is called
 *   6. Module's exit() function is called, symbols unregistered
 *
 * Dependency resolution:
 *   Modules declare dependencies via the 'deps' array in their
 *   module_info structure.  The loader ensures all dependencies
 *   are loaded before the module's init() is called.
 */

/* Maximum number of simultaneously loaded modules. */
#define KMODULE_MAX      64

/* Module name max length. */
#define KMODULE_NAME_MAX 64

/* Maximum number of dependencies a module can declare. */
#define KMODULE_MAX_DEPS 16

/* Maximum number of module parameters. */
#define KMODULE_MAX_PARAMS 16
#define KMODULE_PARAM_NAME_MAX 32
#define KMODULE_PARAM_VALUE_MAX 128

/*
 * Module parameter descriptor.
 * Passed as key=value pairs at load time.
 */
typedef struct kmodule_param {
	char name[KMODULE_PARAM_NAME_MAX];
	char value[KMODULE_PARAM_VALUE_MAX];
} kmodule_param_t;

/*
 * Module information structure.
 *
 * Each module .ko file must contain a global 'module_info' symbol
 * of this type.  The loader reads this structure to determine the
 * module's name, version, dependencies, and entry points.
 */
typedef struct kmodule_info {
	char        name[KMODULE_NAME_MAX];
	char        version[32];
	uint32_t    flags;          /* see KMODULE_FLAG_* */
	uint32_t    num_deps;       /* number of dependencies */
	const char *deps[KMODULE_MAX_DEPS]; /* dependency names */
	void       *init_func;      /* module_init() entry point */
	void       *exit_func;      /* module_exit() entry point */
} kmodule_info_t;

#define KMODULE_FLAG_NONE       0x00
#define KMODULE_FLAG_AUTO_INIT  0x01  /* auto-call init after load */

/*
 * Module descriptor - internal representation of a loaded module.
 */
typedef struct kmodule {
	char            name[KMODULE_NAME_MAX];
	char            version[32];
	uint32_t        flags;
	uint32_t        ref_count;      /* reference count */
	uint32_t        state;          /* see KMODULE_STATE_* */

	void           *load_addr;      /* virtual address where .text is loaded */
	uint32_t        load_size;      /* total size of loaded segments */

	void           *init_func;      /* init entry point */
	void           *exit_func;      /* exit entry point */

	uint32_t        num_deps;
	char            deps[KMODULE_MAX_DEPS][KMODULE_NAME_MAX];

	uint32_t        num_params;
	kmodule_param_t params[KMODULE_MAX_PARAMS];
} kmodule_t;

#define KMODULE_STATE_UNLOADED  0
#define KMODULE_STATE_LOADING   1
#define KMODULE_STATE_LOADED    2
#define KMODULE_STATE_UNLOADING 3

/*
 * EXPORT_SYMBOL macro - marks a kernel function as available for
 * module linking.  When a module is loaded, the loader looks up
 * unresolved symbols in the kernel symbol table (ksym).
 *
 * Usage in kernel code:
 *   EXPORT_SYMBOL(my_function);
 *
 * This registers the symbol with ksym_register() during
 * kmodule_export_init() which is called at boot.
 */
#define EXPORT_SYMBOL(name) \
	static void __attribute__((used)) \
	__export_symbol_##name(void) __attribute__((section(".export_syms"))); \
	static void __export_symbol_##name(void) { \
		extern void kmodule_register_export(const char *sym_name, uint32_t addr); \
		kmodule_register_export(#name, (uint32_t)(void *)&name); \
	}

/* ---- Public API -------------------------------------------------- */

/*
 * kmodule_init - initialise the module subsystem.
 * Called once at boot.
 */
void kmodule_init(void);

/*
 * kmodule_register_export - register a kernel symbol for module use.
 * Called by the EXPORT_SYMBOL macro expansion.
 */
void kmodule_register_export(const char *name, uint32_t addr);

/*
 * kmodule_export_init - register all exported kernel symbols.
 * Called after ksym_init() during boot.
 */
void kmodule_export_init(void);

/*
 * kmodule_load - load a module from memory.
 * 'data' points to the ELF .ko file contents.
 * 'size' is the size of the ELF file in bytes.
 * 'params' is an optional array of key=value parameters.
 * 'num_params' is the number of parameters.
 * Returns a pointer to the kmodule descriptor on success, NULL on error.
 */
kmodule_t *kmodule_load(const uint8_t *data, uint32_t size,
			const kmodule_param_t *params, uint32_t num_params);

/*
 * kmodule_unload - unload a module.
 * Decrements reference count; actually unloads when count reaches 0.
 * Returns 0 on success, -1 if module is in use (ref_count > 1).
 */
int kmodule_unload(kmodule_t *mod);

/*
 * kmodule_find - find a loaded module by name.
 * Returns the kmodule pointer or NULL if not found.
 */
kmodule_t *kmodule_find(const char *name);

/*
 * kmodule_get_ref - increment reference count.
 * Call this when another module or subsystem depends on this module.
 */
void kmodule_get_ref(kmodule_t *mod);

/*
 * kmodule_put_ref - decrement reference count.
 */
void kmodule_put_ref(kmodule_t *mod);

/*
 * kmodule_list - list all loaded modules.
 * Calls 'callback' for each loaded module.
 */
typedef void (*kmodule_list_cb_t)(kmodule_t *mod, void *arg);
void kmodule_list(kmodule_list_cb_t callback, void *arg);

/*
 * kmodule_count - return the number of loaded modules.
 */
uint32_t kmodule_count(void);

/*
 * kmodule_get_param - get a module parameter value by name.
 * Returns the value string or NULL if not found.
 */
const char *kmodule_get_param(kmodule_t *mod, const char *name);

#endif /* KMODULE_H */