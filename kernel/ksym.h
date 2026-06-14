#ifndef KSYM_H
#define KSYM_H

#include "stdint.h"
#include "stddef.h"

/*
 * Kernel Symbol Table
 *
 * Provides a hash-based lookup table mapping symbol names to their
 * virtual addresses.  The debugger and module loader both depend on
 * the ability to resolve addresses back to names (and vice-versa).
 *
 * Symbols fall into two categories:
 *   - Built-in symbols: registered at boot time, either pulled from
 *     the kernel ELF's own symbol table (.symtab / .strtab) or
 *     registered manually via ksym_register().
 *   - Module symbols: registered when a kernel module (.ko) is loaded
 *     and removed when the module is unloaded.
 *
 * Hash table uses a simple chained-bucket design with a prime-sized
 * table for good distribution.
 */

#define KSYM_HASH_SIZE  1021    /* prime number for good hashing */
#define KSYM_NAME_MAX   64      /* maximum symbol name length */

/* A single symbol entry in the hash table. */
typedef struct ksym_entry {
	char            name[KSYM_NAME_MAX];
	uint32_t        addr;
	uint32_t        size;           /* symbol size (0 = unknown) */
	uint32_t        flags;          /* see KSYM_FLAG_* below */
	struct ksym_entry *next;        /* next entry in hash chain */
} ksym_entry_t;

#define KSYM_FLAG_BUILTIN   0x01    /* symbol belongs to kernel core */
#define KSYM_FLAG_MODULE    0x02    /* symbol registered by a module */
#define KSYM_FLAG_FUNCTION  0x04    /* symbol is a function */
#define KSYM_FLAG_DATA      0x08    /* symbol is a data object */

/*
 * ksym_init - initialise the symbol table.
 * Called once at boot, before any other ksym_* functions.
 */
void ksym_init(void);

/*
 * ksym_register - register a new symbol.
 * Returns 0 on success, -1 on error (duplicate name, table full).
 * 'flags' is a bitmask of KSYM_FLAG_* values.
 */
int ksym_register(const char *name, uint32_t addr, uint32_t size, uint32_t flags);

/*
 * ksym_unregister - remove a symbol by name.
 * Returns 0 on success, -1 if not found.
 */
int ksym_unregister(const char *name);

/*
 * ksym_unregister_all - remove all symbols marked with KSYM_FLAG_MODULE.
 * Used when unloading a module to clean up its exported symbols.
 */
void ksym_unregister_all(uint32_t flags_mask);

/*
 * ksym_lookup - look up address by name.
 * Returns the symbol's address, or 0 if not found.
 */
uint32_t ksym_lookup(const char *name);

/*
 * ksym_lookup_name - look up the nearest symbol name for an address.
 * Writes the name (up to bufsize bytes) into 'buf'.
 * Returns the offset from the symbol base, or -1 if no symbol found.
 */
uint32_t ksym_lookup_name(uint32_t addr, char *buf, size_t bufsize);

/*
 * ksym_get_entry - get the full symbol entry for a given name.
 * Returns a pointer to the entry, or NULL if not found.
 */
ksym_entry_t *ksym_get_entry(const char *name);

/*
 * ksym_for_each - iterate over all registered symbols.
 * Calls 'callback' for each symbol.  'arg' is passed through.
 */
typedef void (*ksym_callback_t)(ksym_entry_t *entry, void *arg);
void ksym_for_each(ksym_callback_t callback, void *arg);

/*
 * ksym_count - return the total number of registered symbols.
 */
uint32_t ksym_count(void);

#endif /* KSYM_H */