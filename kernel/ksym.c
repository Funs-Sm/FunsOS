/*
 * kernel/ksym.c - Kernel Symbol Table
 *
 * Implements a hash-based symbol lookup table used by the debugger
 * (kdebug.c) and the module loader (kmodule.c).
 *
 * Architecture:
 *   A fixed-size hash table (KSYM_HASH_SIZE buckets).  Each bucket
 *   is a singly-linked list of ksym_entry_t nodes.  Hash function:
 *   djb2 on the symbol name modulo KSYM_HASH_SIZE.
 *
 * Registration:
 *   - At boot, core kernel symbols are registered via ksym_register()
 *     with KSYM_FLAG_BUILTIN.  These never go away.
 *   - When a module loads, its exported symbols are registered with
 *     KSYM_FLAG_MODULE and can be bulk-removed during unload.
 *
 * Name lookup by address:
 *   We iterate all symbols and pick the closest one whose address is
 *   <= the target address.  This allows resolving any address inside
 *   a function to that function's name + offset.
 */

#include "ksym.h"
#include "kheap.h"
#include "string.h"
#include "klog.h"

/* The hash table: an array of singly-linked-list heads. */
static ksym_entry_t *hash_table[KSYM_HASH_SIZE];
static uint32_t sym_count = 0;

/* ------------------------------------------------------------------
 * djb2 hash function - simple, fast, good distribution for ASCII
 * ------------------------------------------------------------------ */
static uint32_t hash_string(const char *str)
{
	uint32_t hash = 5381;
	int c;

	while ((c = *str++))
		hash = ((hash << 5) + hash) + (uint32_t)c; /* hash * 33 + c */

	return hash % KSYM_HASH_SIZE;
}

/* ------------------------------------------------------------------
 * ksym_init - zero out the hash table
 * ------------------------------------------------------------------ */
void ksym_init(void)
{
	uint32_t i;

	for (i = 0; i < KSYM_HASH_SIZE; i++)
		hash_table[i] = NULL;

	sym_count = 0;
	klog_info("ksym: table initialised, %u buckets", KSYM_HASH_SIZE);
}

/* ------------------------------------------------------------------
 * ksym_register - insert a new symbol into the table
 *
 * Allocates a ksym_entry_t from the heap, fills in the fields,
 * and inserts it at the head of the appropriate bucket chain.
 * Rejects duplicates (same name in same bucket).
 * ------------------------------------------------------------------ */
int ksym_register(const char *name, uint32_t addr, uint32_t size, uint32_t flags)
{
	uint32_t bucket;
	ksym_entry_t *entry;
	ksym_entry_t *walk;
	size_t namelen;

	if (!name || name[0] == '\0') {
		klog_warn("ksym_register: empty name");
		return -1;
	}

	namelen = strlen(name);
	if (namelen >= KSYM_NAME_MAX) {
		klog_warn("ksym_register: name too long (%u chars)", (uint32_t)namelen);
		return -1;
	}

	bucket = hash_string(name);

	/* Check for duplicates in this chain. */
	walk = hash_table[bucket];
	while (walk) {
		if (strcmp(walk->name, name) == 0) {
			klog_warn("ksym_register: duplicate symbol '%s'", name);
			return -1;
		}
		walk = walk->next;
	}

	/* Allocate and populate the new entry. */
	entry = (ksym_entry_t *)kmalloc(sizeof(ksym_entry_t));
	if (!entry) {
		klog_err("ksym_register: kmalloc failed for '%s'", name);
		return -1;
	}

	memset(entry, 0, sizeof(ksym_entry_t));
	memcpy(entry->name, name, namelen);
	entry->name[namelen] = '\0';
	entry->addr = addr;
	entry->size = size;
	entry->flags = flags;

	/* Insert at head of chain. */
	entry->next = hash_table[bucket];
	hash_table[bucket] = entry;
	sym_count++;

	return 0;
}

/* ------------------------------------------------------------------
 * ksym_unregister - remove a symbol by name
 *
 * Walks the bucket chain, removes the matching node, and frees it.
 * ------------------------------------------------------------------ */
int ksym_unregister(const char *name)
{
	uint32_t bucket;
	ksym_entry_t *prev;
	ksym_entry_t *cur;

	if (!name)
		return -1;

	bucket = hash_string(name);
	prev = NULL;
	cur = hash_table[bucket];

	while (cur) {
		if (strcmp(cur->name, name) == 0) {
			/* Unlink from chain. */
			if (prev)
				prev->next = cur->next;
			else
				hash_table[bucket] = cur->next;

			kfree(cur);
			sym_count--;
			return 0;
		}
		prev = cur;
		cur = cur->next;
	}

	return -1; /* not found */
}

/* ------------------------------------------------------------------
 * ksym_unregister_all - bulk-remove symbols matching a flags mask
 *
 * Used when unloading a module: pass KSYM_FLAG_MODULE to remove
 * all module-registered symbols in one pass.
 * ------------------------------------------------------------------ */
void ksym_unregister_all(uint32_t flags_mask)
{
	uint32_t i;
	ksym_entry_t *prev;
	ksym_entry_t *cur;
	ksym_entry_t *to_free;

	for (i = 0; i < KSYM_HASH_SIZE; i++) {
		prev = NULL;
		cur = hash_table[i];

		while (cur) {
			if (cur->flags & flags_mask) {
				/* Remove this node. */
				to_free = cur;
				if (prev)
					prev->next = cur->next;
				else
					hash_table[i] = cur->next;

				cur = cur->next;
				kfree(to_free);
				sym_count--;
			} else {
				prev = cur;
				cur = cur->next;
			}
		}
	}
}

/* ------------------------------------------------------------------
 * ksym_lookup - find a symbol by name, return its address
 * ------------------------------------------------------------------ */
uint32_t ksym_lookup(const char *name)
{
	uint32_t bucket;
	ksym_entry_t *cur;

	if (!name)
		return 0;

	bucket = hash_string(name);
	cur = hash_table[bucket];

	while (cur) {
		if (strcmp(cur->name, name) == 0)
			return cur->addr;
		cur = cur->next;
	}

	return 0; /* not found */
}

/* ------------------------------------------------------------------
 * ksym_lookup_name - resolve an address to the nearest symbol name
 *
 * Iterates ALL symbols (not just one bucket) and finds the one
 * with the largest address that is still <= the target address.
 * This allows resolving any EIP into "<func>+<offset>".
 *
 * Returns: offset from the symbol's start address, or UINT32_MAX
 * if no symbol covers this address.
 * ------------------------------------------------------------------ */
uint32_t ksym_lookup_name(uint32_t addr, char *buf, size_t bufsize)
{
	uint32_t i;
	ksym_entry_t *cur;
	ksym_entry_t *best = NULL;
	uint32_t best_addr = 0;

	if (!buf || bufsize == 0)
		return (uint32_t)-1;

	for (i = 0; i < KSYM_HASH_SIZE; i++) {
		cur = hash_table[i];
		while (cur) {
			/* Only consider function symbols for backtrace. */
			if ((cur->flags & KSYM_FLAG_FUNCTION) &&
			    cur->addr <= addr && cur->addr > best_addr) {
				best = cur;
				best_addr = cur->addr;
			}
			cur = cur->next;
		}
	}

	/* Fallback: if no function matched, try any symbol type. */
	if (!best) {
		for (i = 0; i < KSYM_HASH_SIZE; i++) {
			cur = hash_table[i];
			while (cur) {
				if (cur->addr <= addr && cur->addr > best_addr) {
					best = cur;
					best_addr = cur->addr;
				}
				cur = cur->next;
			}
		}
	}

	if (!best) {
		buf[0] = '\0';
		return (uint32_t)-1;
	}

	/* Copy name into caller buffer. */
	size_t namelen = strlen(best->name);
	if (namelen >= bufsize)
		namelen = bufsize - 1;

	memcpy(buf, best->name, namelen);
	buf[namelen] = '\0';

	return addr - best->addr;
}

/* ------------------------------------------------------------------
 * ksym_get_entry - return the entry pointer for a given name
 * ------------------------------------------------------------------ */
ksym_entry_t *ksym_get_entry(const char *name)
{
	uint32_t bucket;
	ksym_entry_t *cur;

	if (!name)
		return NULL;

	bucket = hash_string(name);
	cur = hash_table[bucket];

	while (cur) {
		if (strcmp(cur->name, name) == 0)
			return cur;
		cur = cur->next;
	}

	return NULL;
}

/* ------------------------------------------------------------------
 * ksym_for_each - iterate over all registered symbols
 * ------------------------------------------------------------------ */
void ksym_for_each(ksym_callback_t callback, void *arg)
{
	uint32_t i;
	ksym_entry_t *cur;

	if (!callback)
		return;

	for (i = 0; i < KSYM_HASH_SIZE; i++) {
		cur = hash_table[i];
		while (cur) {
			callback(cur, arg);
			cur = cur->next;
		}
	}
}

/* ------------------------------------------------------------------
 * ksym_count - return the number of registered symbols
 * ------------------------------------------------------------------ */
uint32_t ksym_count(void)
{
	return sym_count;
}