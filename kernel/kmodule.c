/*
 * kernel/kmodule.c - Kernel Module Loader
 *
 * Loads relocatable ELF (.ko) files into kernel space.  A module is
 * a relocatable ELF object (ET_REL) that contains:
 *   - A publicly visible 'module_info' symbol (kmodule_info_t)
 *   - Code/data sections (.text, .data, .rodata, .bss)
 *   - Relocation tables (.rel.text, .rel.data, etc.)
 *   - Symbol table (.symtab) and string table (.strtab)
 *
 * Loading process:
 *   1. Validate ELF header (magic, class, type == ET_REL)
 *   2. Parse section headers to find .symtab, .strtab, .rel*
 *   3. Allocate memory for all loadable sections
 *   4. Copy section data into allocated memory
 *   5. Resolve undefined symbols against ksym
 *   6. Apply relocations (R_386_32, R_386_PC32)
 *   7. Find and read 'module_info' structure
 *   8. Resolve dependency modules
 *   9. Register module's exported symbols
 *  10. Call module->init_func()
 *
 * Unloading reverses the process, with safety checks.
 */

#include "kmodule.h"
#include "ksym.h"
#include "elf.h"
#include "kheap.h"
#include "string.h"
#include "klog.h"
#include "vmm.h"

/* Default (weak) export symbol section boundaries.
 * Overridden by linker script if .export_syms section exists. */
uint8_t __attribute__((weak)) __export_syms_start[1] = {0};
uint8_t __attribute__((weak)) __export_syms_end[1] = {0};

/* Module table. */
static kmodule_t *module_table[KMODULE_MAX];
static uint32_t module_count = 0;

/* Forward declarations. */
static int kmodule_apply_relocations(kmodule_t *mod, uint8_t *elf_data,
				      Elf32_Shdr *shdrs, uint32_t shnum,
				      Elf32_Shdr *symtab, Elf32_Shdr *strtab);
static int kmodule_resolve_symbols(kmodule_t *mod, uint8_t *elf_data,
				    Elf32_Shdr *shdrs, uint32_t shnum,
				    Elf32_Shdr *symtab, Elf32_Shdr *strtab);
static int kmodule_find_sections(uint8_t *elf_data, Elf32_Ehdr *ehdr,
				  Elf32_Shdr **symtab_out,
				  Elf32_Shdr **strtab_out,
				  uint32_t *shnum_out);

/* ------------------------------------------------------------------
 *  kmodule_init - initialise the module subsystem
 * ------------------------------------------------------------------ */
void kmodule_init(void)
{
	uint32_t i;

	for (i = 0; i < KMODULE_MAX; i++)
		module_table[i] = NULL;

	module_count = 0;
	klog_info("kmodule: subsystem initialised (max %u modules)", KMODULE_MAX);
}

/* ------------------------------------------------------------------
 *  kmodule_register_export - register a kernel symbol for module use
 *
 *  Called by EXPORT_SYMBOL() macro.  Stores the symbol in ksym.
 * ------------------------------------------------------------------ */
void kmodule_register_export(const char *name, uint32_t addr)
{
	if (!name || !addr)
		return;

	ksym_register(name, addr, 0, KSYM_FLAG_BUILTIN | KSYM_FLAG_FUNCTION);
}

/* ------------------------------------------------------------------
 *  kmodule_export_init - called at boot to register all exported symbols
 *
 *  Iterates the .export_syms section (if it exists) and calls
 *  the registration functions placed there by EXPORT_SYMBOL().
 * ------------------------------------------------------------------ */
void kmodule_export_init(void)
{
	/*
	 * The linker script should place all __export_symbol_* functions
	 * in a .export_syms section.  We walk this section and call
	 * each function pointer to register the corresponding symbol.
	 *
	 * Since we can't easily access the section boundaries from C,
	 * we provide the symbols via linker script externs.
	 */
	extern uint8_t __export_syms_start[];
	extern uint8_t __export_syms_end[];

	/*
	 * If no export symbols section exists (e.g., not linked in),
	 * the start and end pointers will be equal or zero.
	 */
	uint32_t start = (uint32_t)__export_syms_start;
	uint32_t end   = (uint32_t)__export_syms_end;

	if (start == 0 || end == 0 || start >= end) {
		klog_info("kmodule: no export symbols section found");
		return;
	}

	uint32_t count = (end - start) / sizeof(void *);
	typedef void (*export_func_t)(void);
	export_func_t *funcs = (export_func_t *)start;

	for (uint32_t i = 0; i < count; i++) {
		if (funcs[i])
			funcs[i]();
	}

	klog_info("kmodule: registered %u exported symbols", count);
}

/* ------------------------------------------------------------------
 *  Helper: find a free slot in the module table
 * ------------------------------------------------------------------ */
static int kmodule_find_slot(void)
{
	for (uint32_t i = 0; i < KMODULE_MAX; i++) {
		if (module_table[i] == NULL)
			return (int)i;
	}
	return -1;
}

/* ------------------------------------------------------------------
 *  Helper: parse key=value parameter string
 * ------------------------------------------------------------------ */
static int kmodule_parse_param(const char *param_str, kmodule_param_t *param)
{
	const char *eq;
	size_t key_len, val_len;

	if (!param_str || !param)
		return -1;

	eq = strchr(param_str, '=');
	if (!eq)
		return -1;

	key_len = (size_t)(eq - param_str);
	if (key_len >= KMODULE_PARAM_NAME_MAX)
		key_len = KMODULE_PARAM_NAME_MAX - 1;

	val_len = strlen(eq + 1);
	if (val_len >= KMODULE_PARAM_VALUE_MAX)
		val_len = KMODULE_PARAM_VALUE_MAX - 1;

	memcpy(param->name, param_str, key_len);
	param->name[key_len] = '\0';

	memcpy(param->value, eq + 1, val_len);
	param->value[val_len] = '\0';

	return 0;
}

/* ------------------------------------------------------------------
 *  Helper: find section headers by name
 * ------------------------------------------------------------------ */
static int kmodule_find_sections(uint8_t *elf_data, Elf32_Ehdr *ehdr,
				  Elf32_Shdr **symtab_out,
				  Elf32_Shdr **strtab_out,
				  uint32_t *shnum_out)
{
	uint32_t shnum = ehdr->e_shnum;
	uint32_t shoff = ehdr->e_shoff;
	uint32_t shstrndx = ehdr->e_shstrndx;

	*shnum_out = shnum;

	if (shnum == 0 || shoff == 0)
		return -1;

	Elf32_Shdr *shdrs = (Elf32_Shdr *)(elf_data + shoff);
	Elf32_Shdr *shstrtab = &shdrs[shstrndx];
	uint8_t *shstrtab_data = elf_data + shstrtab->sh_offset;

	*symtab_out = NULL;
	*strtab_out = NULL;

	for (uint32_t i = 0; i < shnum; i++) {
		const char *name = (const char *)(shstrtab_data + shdrs[i].sh_name);

		if (shdrs[i].sh_type == SHT_SYMTAB)
			*symtab_out = &shdrs[i];
		else if (shdrs[i].sh_type == SHT_STRTAB &&
		         strcmp(name, ".strtab") == 0)
			*strtab_out = &shdrs[i];
	}

	/* If we found a symtab, find its associated strtab. */
	if (*symtab_out && !*strtab_out) {
		uint32_t link = (*symtab_out)->sh_link;
		if (link < shnum)
			*strtab_out = &shdrs[link];
	}

	if (!*symtab_out || !*strtab_out) {
		klog_warn("kmodule: missing .symtab or .strtab");
		return -1;
	}

	return 0;
}

/* ------------------------------------------------------------------
 *  Resolve undefined symbols in the module's symbol table
 *
 *  For each undefined symbol (st_shndx == 0), look up its address
 *  in the kernel symbol table via ksym_lookup().  Also check the
 *  module_info symbol for the structure.
 * ------------------------------------------------------------------ */
static int kmodule_resolve_symbols(kmodule_t *mod, uint8_t *elf_data,
				    Elf32_Shdr *shdrs, uint32_t shnum,
				    Elf32_Shdr *symtab_hdr,
				    Elf32_Shdr *strtab_hdr)
{
	Elf32_Sym *syms = (Elf32_Sym *)(elf_data + symtab_hdr->sh_offset);
	uint32_t sym_count = symtab_hdr->sh_size / sizeof(Elf32_Sym);
	uint8_t *strtab = elf_data + strtab_hdr->sh_offset;

	(void)shdrs;
	(void)shnum;

	for (uint32_t i = 0; i < sym_count; i++) {
		Elf32_Sym *sym = &syms[i];

		/* Only process undefined symbols (global, not local). */
		if (sym->st_shndx != 0)
			continue;

		uint8_t bind = sym->st_info >> 4;
		if (bind == 0) /* STB_LOCAL */
			continue;

		const char *name = (const char *)(strtab + sym->st_name);
		if (!name || name[0] == '\0')
			continue;

		uint32_t addr = ksym_lookup(name);
		if (addr == 0) {
			klog_err("kmodule: unresolved symbol '%s' in module '%s'",
				name, mod->name);
			return -1;
		}

		/* Write the resolved address into the symbol table entry. */
		sym->st_value = addr;
	}

	return 0;
}

/* ------------------------------------------------------------------
 *  Apply ELF relocations
 *
 *  Supports R_386_32 (absolute address) and R_386_PC32 (PC-relative).
 *  The relocation target is computed as:
 *    R_386_32:  *target = S + A
 *    R_386_PC32: *target = S + A - P
 *  where S = symbol value, A = addend (stored in-place), P = target location.
 * ------------------------------------------------------------------ */
static int kmodule_apply_relocations(kmodule_t *mod, uint8_t *elf_data,
				      Elf32_Shdr *shdrs, uint32_t shnum,
				      Elf32_Shdr *symtab_hdr,
				      Elf32_Shdr *strtab_hdr)
{
	Elf32_Sym *syms = (Elf32_Sym *)(elf_data + symtab_hdr->sh_offset);
	uint32_t sym_count = symtab_hdr->sh_size / sizeof(Elf32_Sym);
	uint8_t *strtab = elf_data + strtab_hdr->sh_offset;

	(void)mod;

	for (uint32_t i = 0; i < shnum; i++) {
		Elf32_Shdr *shdr = &shdrs[i];

		/* Process REL relocation sections. */
		if (shdr->sh_type != SHT_REL)
			continue;

		Elf32_Rel *rels = (Elf32_Rel *)(elf_data + shdr->sh_offset);
		uint32_t rel_count = shdr->sh_size / sizeof(Elf32_Rel);

		/* The section being relocated. */
		uint32_t target_sec = shdr->sh_info;
		if (target_sec >= shnum)
			continue;

		Elf32_Shdr *target_shdr = &shdrs[target_sec];
		uint8_t *target_data = (uint8_t *)target_shdr->sh_addr;

		if (!target_data)
			continue;

		for (uint32_t j = 0; j < rel_count; j++) {
			Elf32_Rel *rel = &rels[j];
			uint32_t sym_idx = rel->r_info >> 8;
			uint32_t rel_type = rel->r_info & 0xFF;

			if (sym_idx >= sym_count) {
				klog_warn("kmodule: bad symbol index %u", sym_idx);
				return -1;
			}

			Elf32_Sym *sym = &syms[sym_idx];
			uint32_t *target = (uint32_t *)(target_data + rel->r_offset);

			uint32_t S = sym->st_value;
			uint32_t A = *target;  /* addend is stored in-place for REL */
			uint32_t P = (uint32_t)(target_data + rel->r_offset);

			switch (rel_type) {
			case R_386_NONE:
				break;

			case R_386_32:
				*target = S + A;
				break;

			case R_386_PC32:
				*target = S + A - P;
				break;

			default:
				klog_warn("kmodule: unsupported relocation type %u "
					"for symbol '%s'",
					rel_type,
					(const char *)(strtab + sym->st_name));
				return -1;
			}
		}
	}

	return 0;
}

/* ------------------------------------------------------------------
 *  Parse module_info structure from the loaded ELF
 *
 *  The module_info symbol is a global data symbol in the ELF.
 *  We find it by name in the symbol table, then read the
 *  kmodule_info_t structure from the appropriate section.
 * ------------------------------------------------------------------ */
static kmodule_info_t *kmodule_find_info(uint8_t *elf_data, Elf32_Ehdr *ehdr)
{
	uint32_t shoff = ehdr->e_shoff;
	uint32_t shnum = ehdr->e_shnum;

	if (shnum == 0 || shoff == 0)
		return NULL;

	Elf32_Shdr *shdrs = (Elf32_Shdr *)(elf_data + shoff);

	/* Find .symtab and .strtab. */
	Elf32_Shdr *symtab = NULL;
	Elf32_Shdr *strtab = NULL;

	for (uint32_t i = 0; i < shnum; i++) {
		if (shdrs[i].sh_type == SHT_SYMTAB)
			symtab = &shdrs[i];
		else if (shdrs[i].sh_type == SHT_STRTAB) {
			/* Use the strtab linked from symtab. */
			if (symtab && shdrs[i].sh_offset == shdrs[symtab->sh_link].sh_offset)
				; /* handled below */
		}
	}

	if (!symtab)
		return NULL;

	/* The strtab linked to symtab. */
	if (symtab->sh_link >= shnum)
		return NULL;
	strtab = &shdrs[symtab->sh_link];

	Elf32_Sym *syms = (Elf32_Sym *)(elf_data + symtab->sh_offset);
	uint32_t sym_count = symtab->sh_size / sizeof(Elf32_Sym);
	uint8_t *strtab_data = elf_data + strtab->sh_offset;

	for (uint32_t i = 0; i < sym_count; i++) {
		const char *name = (const char *)(strtab_data + syms[i].st_name);
		if (name && strcmp(name, "module_info") == 0) {
			/* Found the symbol.  Its value is the address of the struct. */
			uint32_t sec_idx = syms[i].st_shndx;
			if (sec_idx == 0 || sec_idx >= shnum)
				return NULL;

			Elf32_Shdr *sec = &shdrs[sec_idx];
			uint32_t offset = syms[i].st_value;

			/* st_value is the offset within the section.
			 * sec->sh_addr now holds the allocated base address. */
			return (kmodule_info_t *)((uint8_t *)sec->sh_addr + offset);
		}
	}

	return NULL;
}

/* ------------------------------------------------------------------
 *  Allocate memory for all loadable sections
 *
 *  Iterates through section headers, allocates kernel heap memory
 *  for each PROGBITS or NOBITS section, and records the allocation
 *  in the section's sh_addr field.
 * ------------------------------------------------------------------ */
static int kmodule_alloc_sections(uint8_t *elf_data, Elf32_Ehdr *ehdr,
				   uint32_t *total_size)
{
	uint32_t shoff = ehdr->e_shoff;
	uint32_t shnum = ehdr->e_shnum;

	if (shnum == 0 || shoff == 0)
		return -1;

	Elf32_Shdr *shdrs = (Elf32_Shdr *)(elf_data + shoff);

	/* First pass: calculate total size needed. */
	uint32_t alloc_size = 0;
	for (uint32_t i = 0; i < shnum; i++) {
		if (!(shdrs[i].sh_flags & SHF_ALLOC))
			continue;

		uint32_t align = shdrs[i].sh_addralign;
		if (align < 4)
			align = 4;

		/* Align up. */
		alloc_size = (alloc_size + align - 1) & ~(align - 1);

		/* Record where this section will be placed. */
		shdrs[i].sh_addr = alloc_size;  /* temporary: offset within allocation */
		alloc_size += shdrs[i].sh_size;
	}

	if (alloc_size == 0) {
		klog_warn("kmodule: no allocatable sections");
		return -1;
	}

	/* Allocate the block. */
	uint8_t *base = (uint8_t *)kmalloc(alloc_size);
	if (!base) {
		klog_err("kmodule: failed to allocate %u bytes for module", alloc_size);
		return -1;
	}

	memset(base, 0, alloc_size);

	/* Second pass: copy section data and fix up sh_addr. */
	for (uint32_t i = 0; i < shnum; i++) {
		if (!(shdrs[i].sh_flags & SHF_ALLOC))
			continue;

		uint32_t offset = shdrs[i].sh_addr;  /* we stored offset here */
		shdrs[i].sh_addr = (uint32_t)(base + offset);

		if (shdrs[i].sh_type == SHT_PROGBITS) {
			/* Copy section data from ELF to allocated memory. */
			memcpy((void *)shdrs[i].sh_addr,
			       elf_data + shdrs[i].sh_offset,
			       shdrs[i].sh_size);
		}
		/* For SHT_NOBITS (.bss), memory is already zeroed. */
	}

	*total_size = alloc_size;
	return 0;
}

/* ------------------------------------------------------------------
 *  Resolve module dependencies
 *
 *  Checks that all required dependencies are already loaded.
 * ------------------------------------------------------------------ */
static int kmodule_resolve_deps(kmodule_t *mod, kmodule_info_t *info)
{
	mod->num_deps = 0;

	for (uint32_t i = 0; i < info->num_deps && i < KMODULE_MAX_DEPS; i++) {
		const char *dep_name = info->deps[i];
		if (!dep_name || dep_name[0] == '\0')
			continue;

		/* Check if the dependency is loaded. */
		kmodule_t *dep = kmodule_find(dep_name);
		if (!dep) {
			klog_err("kmodule '%s': dependency '%s' not loaded",
				mod->name, dep_name);
			return -1;
		}

		/* Store dependency name. */
		size_t len = strlen(dep_name);
		if (len >= KMODULE_NAME_MAX)
			len = KMODULE_NAME_MAX - 1;
		memcpy(mod->deps[mod->num_deps], dep_name, len);
		mod->deps[mod->num_deps][len] = '\0';
		mod->num_deps++;

		/* Increment reference count on the dependency. */
		kmodule_get_ref(dep);
	}

	return 0;
}

/* ------------------------------------------------------------------
 *  kmodule_load - load a .ko module from memory
 * ------------------------------------------------------------------ */
kmodule_t *kmodule_load(const uint8_t *data, uint32_t size,
			const kmodule_param_t *params, uint32_t num_params)
{
	if (!data || size < sizeof(Elf32_Ehdr)) {
		klog_err("kmodule_load: invalid ELF data");
		return NULL;
	}

	/* Allocate a module descriptor. */
	int slot = kmodule_find_slot();
	if (slot < 0) {
		klog_err("kmodule_load: module table full (max %u)", KMODULE_MAX);
		return NULL;
	}

	kmodule_t *mod = (kmodule_t *)kmalloc(sizeof(kmodule_t));
	if (!mod) {
		klog_err("kmodule_load: kmalloc failed for descriptor");
		return NULL;
	}

	memset(mod, 0, sizeof(kmodule_t));
	mod->state = KMODULE_STATE_LOADING;
	mod->ref_count = 1;  /* initial reference */

	/* Copy parameters. */
	if (params && num_params > 0) {
		mod->num_params = num_params;
		if (num_params > KMODULE_MAX_PARAMS)
			num_params = KMODULE_MAX_PARAMS;
		for (uint32_t i = 0; i < num_params; i++) {
			memcpy(&mod->params[i], &params[i], sizeof(kmodule_param_t));
		}
	}

	/* Validate ELF header. */
	Elf32_Ehdr *ehdr = (Elf32_Ehdr *)data;

	if (ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
	    ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
	    ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
	    ehdr->e_ident[EI_MAG3] != ELFMAG3) {
		klog_err("kmodule_load: invalid ELF magic");
		kfree(mod);
		return NULL;
	}

	if (ehdr->e_ident[EI_CLASS] != ELFCLASS32) {
		klog_err("kmodule_load: only 32-bit ELF modules supported");
		kfree(mod);
		return NULL;
	}

	if (ehdr->e_type != ET_REL) {
		klog_err("kmodule_load: module must be ET_REL (relocatable)");
		kfree(mod);
		return NULL;
	}

	if (ehdr->e_machine != EM_386) {
		klog_err("kmodule_load: module must be compiled for i386");
		kfree(mod);
		return NULL;
	}

	/* Make a writable copy of the ELF data for section header manipulation. */
	uint8_t *elf_copy = (uint8_t *)kmalloc(size);
	if (!elf_copy) {
		klog_err("kmodule_load: failed to allocate ELF copy");
		kfree(mod);
		return NULL;
	}
	memcpy(elf_copy, data, size);
	Elf32_Ehdr *ehdr_copy = (Elf32_Ehdr *)elf_copy;

	/* Allocate memory for loadable sections. */
	uint32_t total_size = 0;
	if (kmodule_alloc_sections(elf_copy, ehdr_copy, &total_size) != 0) {
		klog_err("kmodule_load: section allocation failed");
		kfree(mod);
		return NULL;
	}

	mod->load_addr = (void *)((Elf32_Shdr *)(elf_copy + ehdr_copy->e_shoff))->sh_addr;
	mod->load_size = total_size;

	/* Find the module_info structure. */
	kmodule_info_t *info = kmodule_find_info(elf_copy, ehdr_copy);
	if (!info) {
		klog_err("kmodule_load: no module_info symbol found");
		kfree(mod);
		return NULL;
	}

	/* Copy module metadata. */
	size_t name_len = strlen(info->name);
	if (name_len >= KMODULE_NAME_MAX)
		name_len = KMODULE_NAME_MAX - 1;
	memcpy(mod->name, info->name, name_len);
	mod->name[name_len] = '\0';

	memcpy(mod->version, info->version, sizeof(info->version) - 1);
	mod->version[sizeof(info->version) - 1] = '\0';
	mod->flags = info->flags;

	/* Check for duplicate module name. */
	if (kmodule_find(mod->name)) {
		klog_err("kmodule_load: module '%s' already loaded", mod->name);
		kfree(mod);
		return NULL;
	}

	/* Find sections. */
	Elf32_Shdr *symtab = NULL;
	Elf32_Shdr *strtab = NULL;
	uint32_t shnum = 0;

	if (kmodule_find_sections(elf_copy, ehdr_copy, &symtab, &strtab, &shnum) != 0) {
		klog_err("kmodule_load: section lookup failed");
		kfree(mod);
		return NULL;
	}

	Elf32_Shdr *shdrs = (Elf32_Shdr *)(elf_copy + ehdr_copy->e_shoff);

	/* Resolve dependencies. */
	if (kmodule_resolve_deps(mod, info) != 0) {
		kfree(mod);
		return NULL;
	}

	/* Resolve undefined symbols. */
	if (kmodule_resolve_symbols(mod, elf_copy, shdrs, shnum, symtab, strtab) != 0) {
		kfree(mod);
		return NULL;
	}

	/* Apply relocations. */
	if (kmodule_apply_relocations(mod, elf_copy, shdrs, shnum, symtab, strtab) != 0) {
		klog_err("kmodule_load: relocation failed for '%s'", mod->name);
		kfree(mod);
		return NULL;
	}

	/* Register module's exported symbols with ksym. */
	Elf32_Sym *syms = (Elf32_Sym *)(elf_copy + symtab->sh_offset);
	uint32_t sym_count = symtab->sh_size / sizeof(Elf32_Sym);
	uint8_t *strtab_data = elf_copy + strtab->sh_offset;

	for (uint32_t i = 0; i < sym_count; i++) {
		Elf32_Sym *sym = &syms[i];

		/* Only export global symbols that are defined in this module. */
		uint8_t bind = sym->st_info >> 4;
		if (bind != 1) /* STB_GLOBAL */
			continue;

		if (sym->st_shndx == 0) /* undefined */
			continue;

		const char *sym_name = (const char *)(strtab_data + sym->st_name);
		if (!sym_name || sym_name[0] == '\0')
			continue;

		/* Skip special symbols. */
		if (strcmp(sym_name, "module_info") == 0)
			continue;

		uint32_t sym_type = sym->st_info & 0x0F;
		uint32_t flags = KSYM_FLAG_MODULE;
		if (sym_type == 2) /* STT_FUNC */
			flags |= KSYM_FLAG_FUNCTION;
		else
			flags |= KSYM_FLAG_DATA;

		ksym_register(sym_name, sym->st_value, sym->st_size, flags);
	}

	/* Store entry points. */
	mod->init_func = info->init_func;
	mod->exit_func = info->exit_func;

	/* Call module init function. */
	if (mod->init_func) {
		typedef int (*init_func_t)(kmodule_t *);
		init_func_t init = (init_func_t)(void *)mod->init_func;

		klog_info("kmodule: calling init() for '%s'", mod->name);
		int ret = init(mod);
		if (ret != 0) {
			klog_err("kmodule: init() failed for '%s' (ret=%d)", mod->name, ret);
			/* Cleanup: unregister symbols, free memory. */
			ksym_unregister_all(KSYM_FLAG_MODULE);
			kfree(mod);
			return NULL;
		}
	}

	mod->state = KMODULE_STATE_LOADED;
	module_table[slot] = mod;
	module_count++;

	klog_info("kmodule: loaded '%s' v%s (size=%u bytes)", mod->name, mod->version, total_size);

	return mod;
}

/* ------------------------------------------------------------------
 *  kmodule_unload - unload a module
 * ------------------------------------------------------------------ */
int kmodule_unload(kmodule_t *mod)
{
	if (!mod)
		return -1;

	if (mod->state != KMODULE_STATE_LOADED) {
		klog_warn("kmodule_unload: module '%s' not in loaded state", mod->name);
		return -1;
	}

	if (mod->ref_count > 1) {
		klog_warn("kmodule_unload: module '%s' in use (ref_count=%u)",
			mod->name, mod->ref_count);
		mod->ref_count--;
		return -1;
	}

	mod->state = KMODULE_STATE_UNLOADING;

	/* Call exit function. */
	if (mod->exit_func) {
		typedef void (*exit_func_t)(void);
		exit_func_t exit_fn = (exit_func_t)(void *)mod->exit_func;

		klog_info("kmodule: calling exit() for '%s'", mod->name);
		exit_fn();
	}

	/* Unregister module's exported symbols. */
	ksym_unregister_all(KSYM_FLAG_MODULE);

	/* Release references to dependencies. */
	for (uint32_t i = 0; i < mod->num_deps; i++) {
		kmodule_t *dep = kmodule_find(mod->deps[i]);
		if (dep)
			kmodule_put_ref(dep);
	}

	/* Free module memory. */
	if (mod->load_addr)
		kfree(mod->load_addr);

	/* Remove from module table. */
	for (uint32_t i = 0; i < KMODULE_MAX; i++) {
		if (module_table[i] == mod) {
			module_table[i] = NULL;
			module_count--;
			break;
		}
	}

	klog_info("kmodule: unloaded '%s'", mod->name);
	kfree(mod);

	return 0;
}

/* ------------------------------------------------------------------
 *  kmodule_find - find a module by name
 * ------------------------------------------------------------------ */
kmodule_t *kmodule_find(const char *name)
{
	if (!name)
		return NULL;

	for (uint32_t i = 0; i < KMODULE_MAX; i++) {
		if (module_table[i] && strcmp(module_table[i]->name, name) == 0)
			return module_table[i];
	}

	return NULL;
}

/* ------------------------------------------------------------------
 *  kmodule_get_ref / kmodule_put_ref
 * ------------------------------------------------------------------ */
void kmodule_get_ref(kmodule_t *mod)
{
	if (mod)
		mod->ref_count++;
}

void kmodule_put_ref(kmodule_t *mod)
{
	if (mod && mod->ref_count > 1)
		mod->ref_count--;
}

/* ------------------------------------------------------------------
 *  kmodule_list - iterate over all loaded modules
 * ------------------------------------------------------------------ */
void kmodule_list(kmodule_list_cb_t callback, void *arg)
{
	if (!callback)
		return;

	for (uint32_t i = 0; i < KMODULE_MAX; i++) {
		if (module_table[i])
			callback(module_table[i], arg);
	}
}

/* ------------------------------------------------------------------
 *  kmodule_count - return number of loaded modules
 * ------------------------------------------------------------------ */
uint32_t kmodule_count(void)
{
	return module_count;
}

/* ------------------------------------------------------------------
 *  kmodule_get_param - get a module parameter value
 * ------------------------------------------------------------------ */
const char *kmodule_get_param(kmodule_t *mod, const char *name)
{
	if (!mod || !name)
		return NULL;

	for (uint32_t i = 0; i < mod->num_params; i++) {
		if (strcmp(mod->params[i].name, name) == 0)
			return mod->params[i].value;
	}

	return NULL;
}