#include "process.h"
#include "thread.h"
#include "pmm.h"
#include "vmm.h"
#include "kheap.h"
#include "sched.h"
#include "syscall.h"
#include "signal.h"
#include "panic.h"
#include "string.h"
#include "elf.h"
#include "tls.h"
#include "../fs/vfs.h"

static pid_t next_pid = 1;
static pcb_t *process_table[MAX_PROCESSES];
static spinlock_t process_lock;

pcb_t *current_process = NULL;
volatile int need_resched = 0;

/* Virtual address range for kernel stacks (2 pages each)
 * Must NOT overlap with kheap at 0xD0000000 (32MB = 0xD2000000) */
#define KSTACK_VIRT_BASE  0xD2000000
static uint32_t next_kstack_virt = KSTACK_VIRT_BASE;

void init_process(void)
{
    int i;
    for (i = 0; i < MAX_PROCESSES; i++) {
        process_table[i] = (void *)0;
    }
    next_pid = 1;
    spinlock_init(&process_lock);
}

static pid_t alloc_pid(void)
{
    pid_t pid = next_pid;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (pid >= MAX_PROCESSES) pid = 1;
        if (!process_table[pid]) {
            next_pid = pid + 1;
            return pid;
        }
        pid++;
    }
    return -1;
}

static uint32_t calculate_timeslice(uint32_t priority) {
    if (priority > SCHED_PRIORITY_MAX) priority = SCHED_PRIORITY_MAX;
    return DEFAULT_TIME_SLICE + (priority * DEFAULT_TIME_SLICE) / SCHED_PRIORITY_MAX;
}

static pcb_t *create_process_common(const char *name)
{
    pcb_t *proc = (pcb_t *)kcalloc(1, sizeof(pcb_t));
    if (!proc) return (void *)0;

    proc->pid = alloc_pid();
    if (proc->pid < 0) {
        kfree(proc);
        return (void *)0;
    }

    proc->type = PROCESS_USER;
    proc->state = PROCESS_READY;
    proc->sched_policy = PROCESS_NORMAL;
    proc->queue_level = 0;
    proc->priority = SCHED_DEFAULT_PRIORITY;
    proc->original_priority = SCHED_DEFAULT_PRIORITY;
    proc->effective_priority = SCHED_DEFAULT_PRIORITY;
    proc->time_slice = calculate_timeslice(SCHED_DEFAULT_PRIORITY);
    proc->ticks_used = 0;
    proc->cpu_time = 0;
    proc->last_run_time = 0;
    proc->nice = 0;

    if (name) {
        int k;
        for (k = 0; k < 31 && name[k]; k++) {
            proc->name[k] = name[k];
        }
        proc->name[k] = '\0';
    }

    for (int s = 0; s < 32; s++) {
        proc->signal_handlers[s] = SIG_DFL;
    }

    for (int f = 0; f < MAX_OPEN_FILES; f++) {
        proc->fd_table[f] = (void *)0;
    }

    process_table[proc->pid] = proc;
    return proc;
}

/* Allocate a 2-page kernel stack, mapped contiguously in kernel virtual
 * memory.  Sets proc->kernel_stack (top) and proc->kernel_esp.  For a
 * new process the stack is set up so that context_switch will "return"
 * to process_first_run.  For fork the caller adjusts kernel_esp later. */
static int alloc_kernel_stack(pcb_t *proc)
{
    uint32_t kstack_virt = next_kstack_virt;
    next_kstack_virt += 2 * PAGE_SIZE;

    void *phys1 = pmm_alloc_page();
    void *phys2 = pmm_alloc_page();
    if (!phys1 || !phys2) {
        if (phys1) pmm_free_page(phys1);
        if (phys2) pmm_free_page(phys2);
        return -1;
    }

    vmm_map_page(vmm_get_current_dir(), kstack_virt, (uint32_t)phys1,
                 PTE_PRESENT | PTE_WRITABLE);
    vmm_map_page(vmm_get_current_dir(), kstack_virt + PAGE_SIZE, (uint32_t)phys2,
                 PTE_PRESENT | PTE_WRITABLE);

    /* Zero the stack pages */
    memset((void *)kstack_virt, 0, 2 * PAGE_SIZE);

    proc->kernel_stack = kstack_virt + 2 * PAGE_SIZE;  /* top of stack */

    /* Set up for context_switch: the asm pops edi,esi,ebx,ebp then RET.
     * For a new process, RET should jump to process_first_run. */
    uint32_t *sp = (uint32_t *)proc->kernel_stack;
    sp--; *sp = (uint32_t)process_first_run;   /* return address */
    sp--; *sp = 0;   /* ebp */
    sp--; *sp = 0;   /* ebx */
    sp--; *sp = 0;   /* esi */
    sp--; *sp = 0;   /* edi */
    proc->kernel_esp = (uint32_t)sp;

    return 0;
}

/* Create a kernel-only thread (no user-space, no ELF loading).
 * Used for idle task and other kernel threads. */
pcb_t *process_create_kernel(const char *name, void (*entry)(void))
{
    pcb_t *proc = create_process_common(name);
    if (!proc) return NULL;

    if (alloc_kernel_stack(proc) != 0) {
        return NULL;
    }

    proc->entry_point = (uint32_t)entry;
    proc->user_stack = 0;
    proc->page_dir = vmm_get_current_dir();
    proc->state = PROCESS_READY;
    proc->sched_policy = PROCESS_IDLE;
    proc->priority = SCHED_PRIORITY_MAX;
    proc->effective_priority = SCHED_PRIORITY_MAX;

    /* Override the return address on the kernel stack to point to entry.
     * The context_switch will pop callee-saved regs and RET to entry. */
    uint32_t *sp = (uint32_t *)proc->kernel_esp;
    /* sp points to: [edi=0] [esi=0] [ebx=0] [ebp=0] [ret=process_first_run]
     * Replace process_first_run with our kernel entry point */
    sp[4] = (uint32_t)entry;  /* overwrite return address */

    return proc;
}

pcb_t *process_create(const char *name, uint8_t *elf_data, uint32_t elf_size)
{
    if (!elf_validate(elf_data, elf_size)) return (void *)0;

    pcb_t *proc = create_process_common(name);
    if (!proc) return (void *)0;

    proc->page_dir = vmm_create_address_space();
    if (!proc->page_dir) {
        goto err_free_proc;
    }

    Elf32_Ehdr *hdr = (Elf32_Ehdr *)elf_data;
    uint32_t ph_offset = hdr->e_phoff;
    uint16_t ph_entry_size = hdr->e_phentsize;
    uint16_t ph_num = hdr->e_phnum;

    /* Phase 1: Map all PT_LOAD pages into the new address space.
     * This must happen while CR3 still points to the kernel page
     * directory because vmm_map_page dereferences physical addresses
     * that rely on the identity mapping. */
    for (uint16_t i = 0; i < ph_num; i++) {
        Elf32_Phdr *ph = (Elf32_Phdr *)(elf_data + ph_offset + i * ph_entry_size);

        if (ph->p_type != PT_LOAD) continue;

        uint32_t page_flags = VMM_PAGE_PRESENT | VMM_PAGE_USER;
        if (ph->p_flags & PF_W) page_flags |= VMM_PAGE_WRITABLE;

        uint32_t addr = ph->p_vaddr & 0xFFFFF000;
        uint32_t end_addr = ph->p_vaddr + ph->p_memsz;
        while (addr < end_addr) {
            void *phys = pmm_alloc_page();
            if (!phys) {
                goto err_free_page_dir;
            }
            vmm_map_page(proc->page_dir, addr, (uint32_t)phys, page_flags);
            addr += PAGE_SIZE;
        }
    }

    /* Map user stack (4 pages below USER_STACK_TOP) */
    for (int i = 0; i < 4; i++) {
        uint32_t stack_addr = USER_STACK_TOP - (i + 1) * PMM_PAGE_SIZE;
        void *phys = pmm_alloc_page();
        if (!phys) {
            goto err_free_page_dir;
        }
        vmm_map_page(proc->page_dir, stack_addr, (uint32_t)phys,
                     VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE | VMM_PAGE_USER);
    }

    /* Phase 2: Switch CR3 to the new address space and copy ELF data.
     * The new directory has kernel mappings (high-half PDEs are shared),
     * so kernel code and the elf_data buffer remain accessible. */
    uint32_t old_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(old_cr3));
    asm volatile("mov %0, %%cr3" : : "r"(proc->page_dir) : "memory");

    for (uint16_t i = 0; i < ph_num; i++) {
        Elf32_Phdr *ph = (Elf32_Phdr *)(elf_data + ph_offset + i * ph_entry_size);

        if (ph->p_type != PT_LOAD) continue;

        uint8_t *dest = (uint8_t *)ph->p_vaddr;
        memset(dest, 0, ph->p_memsz);
        memcpy(dest, elf_data + ph->p_offset, ph->p_filesz);
    }

    /* Switch CR3 back to the kernel page directory */
    asm volatile("mov %0, %%cr3" : : "r"(old_cr3) : "memory");

    /* Allocate kernel stack (2 pages, with context_switch setup) */
    if (alloc_kernel_stack(proc) != 0) {
        goto err_free_page_dir;
    }

    proc->user_stack = USER_STACK_TOP;
    proc->entry_point = hdr->e_entry;

    proc->parent_pid = sched_get_current() ? sched_get_current()->pid : 0;
    proc->first_child = (void *)0;
    proc->next_sibling = (void *)0;
    proc->next = (void *)0;
    proc->prev = (void *)0;
    proc->exit_status = 0;
    proc->blocked_reason = 0;
    proc->signal_pending = 0;
    proc->signal_blocked = 0;
    proc->wake_time = 0;

    sched_add(proc);
    return proc;

err_free_page_dir:
    vmm_destroy_address_space(proc->page_dir);
err_free_proc:
    if (proc->pid >= 0 && proc->pid < MAX_PROCESSES) {
        process_table[proc->pid] = (void *)0;
    }
    kfree(proc);
    return (void *)0;
}

void process_exit(int status)
{
    pcb_t *curr = sched_get_current();
    if (!curr) return;

    spinlock_lock(&process_lock);

    /* Run TLS destructors before destroying the process */
    tls_cleanup(curr);

    curr->exit_status = status;
    curr->state = PROCESS_ZOMBIE;

    for (int f = 0; f < MAX_OPEN_FILES; f++) {
        if (curr->fd_table[f]) {
            if (curr->fd_table[f]->ops && curr->fd_table[f]->ops->close) {
                curr->fd_table[f]->ops->close(curr->fd_table[f]);
            }
            kfree(curr->fd_table[f]);
            curr->fd_table[f] = (void *)0;
        }
    }

    if (curr->page_dir) {
        vmm_destroy_address_space(curr->page_dir);
        curr->page_dir = (void *)0;
    }

    /* BUG-10 fix: do NOT clear process_table here.  The ZOMBIE entry
     * must remain so that process_wait() can reap it and read the
     * exit_status.  The table entry is cleared in process_wait(). */

    sched_remove(curr);

    if (curr->parent_pid > 0) {
        pcb_t *parent = process_get_pcb(curr->parent_pid);
        if (parent && parent->state == PROCESS_BLOCKED) {
            sched_unblock(parent);
        }
    }

    spinlock_unlock(&process_lock);
    schedule();
}

pid_t process_wait(int *status)
{
    pcb_t *curr = sched_get_current();
    if (!curr) return -1;

    spinlock_lock(&process_lock);

    while (1) {
        pcb_t *child = curr->first_child;
        while (child) {
            if (child->state == PROCESS_ZOMBIE) {
                if (status) *status = child->exit_status;
                pid_t pid = child->pid;

                if (child->prev) child->prev->next = child->next;
                if (child->next) child->next->prev = child->prev;

                if (child->parent_pid == curr->pid) {
                    if (curr->first_child == child) {
                        curr->first_child = child->next_sibling;
                    } else {
                        pcb_t *sibling = curr->first_child;
                        while (sibling && sibling->next_sibling != child) {
                            sibling = sibling->next_sibling;
                        }
                        if (sibling) {
                            sibling->next_sibling = child->next_sibling;
                        }
                    }
                }

                /* Now safe to clear the process table entry and free the PCB */
                if (pid >= 0 && pid < MAX_PROCESSES) {
                    process_table[pid] = (void *)0;
                }
                kfree(child);
                spinlock_unlock(&process_lock);
                return pid;
            }
            child = child->next_sibling;
        }

        sched_block(curr, BLOCK_REASON_WAIT);
        spinlock_unlock(&process_lock);
        schedule();
        spinlock_lock(&process_lock);
    }

    spinlock_unlock(&process_lock);
    return -1;
}

pid_t process_fork(void)
{
    pcb_t *parent = sched_get_current();
    if (!parent) return -1;

    spinlock_lock(&process_lock);

    pcb_t *child = create_process_common(parent->name);
    if (!child) {
        spinlock_unlock(&process_lock);
        return -1;
    }

    child->page_dir = vmm_create_address_space();
    if (!child->page_dir) {
        kfree(child);
        spinlock_unlock(&process_lock);
        return -1;
    }

    if (vmm_clone_address_space(child->page_dir, parent->page_dir) != 0) {
        vmm_destroy_address_space(child->page_dir);
        kfree(child);
        spinlock_unlock(&process_lock);
        return -1;
    }

    child->context = parent->context;
    child->context.eax = 0;

    child->entry_point = parent->entry_point;
    child->user_stack = parent->user_stack;

    /* Allocate a 2-page kernel stack for the child */
    if (alloc_kernel_stack(child) != 0) {
        vmm_destroy_address_space(child->page_dir);
        if (child->pid >= 0 && child->pid < MAX_PROCESSES) {
            process_table[child->pid] = (void *)0;
        }
        kfree(child);
        spinlock_unlock(&process_lock);
        return -1;
    }

    /* For fork, the child should resume as if returning from the
     * syscall.  We set up the kernel stack so that context_switch
     * "returns" into the interrupt return path.  The child's
     * kernel_esp must point to a regs_t frame that iret will use.
     *
     * Layout (growing down from kernel_stack):
     *   [gs] [fs] [es] [ds]           -- segment regs
     *   [edi] [esi] [ebp] [esp_k] [ebx] [edx] [ecx] [eax]  -- PUSHAD
     *   [int_no] [err_code]           -- ISR stub
     *   [eip] [cs] [eflags] [useresp] [ss]  -- CPU
     *   [callee-saved for context_switch: edi, esi, ebx, ebp]
     *   [return address -> fork_return_trampoline]
     *
     * Actually, the simplest approach: set up the stack so that
     * context_switch returns to a small trampoline that pops the
     * regs_t frame and does iret, just like the normal interrupt
     * return path. */

    /* We build a regs_t frame on the child's kernel stack, then
     * set up context_switch's callee-saved regs above it so that
     * context_switch returns into our fork_return trampoline which
     * pops the regs_t and irets. */
    uint32_t *sp = (uint32_t *)child->kernel_stack;

    /* CPU-pushed registers (for iret) */
    sp--; *sp = child->context.ss;          /* ss = 0x23 */
    sp--; *sp = child->context.useresp;     /* useresp */
    sp--; *sp = child->context.eflags;      /* eflags */
    sp--; *sp = child->context.cs;          /* cs = 0x1B or 0x08 */
    sp--; *sp = child->context.eip;         /* eip */

    /* ISR stub pushed */
    sp--; *sp = child->context.err_code;    /* err_code */
    sp--; *sp = child->context.int_no;      /* int_no */

    /* PUSHAD registers */
    sp--; *sp = child->context.eax;
    sp--; *sp = child->context.ecx;
    sp--; *sp = child->context.edx;
    sp--; *sp = child->context.ebx;
    sp--; *sp = child->context.esp_kernel;  /* esp (discarded by POPAD) */
    sp--; *sp = child->context.ebp;
    sp--; *sp = child->context.esi;
    sp--; *sp = child->context.edi;

    /* Segment registers */
    sp--; *sp = child->context.ds;
    sp--; *sp = child->context.es;
    sp--; *sp = child->context.fs;
    sp--; *sp = child->context.gs;

    /* Now sp points to the regs_t frame.  Above it we place the
     * context_switch callee-saved regs and a return address to
     * fork_return_asm (defined in interrupt.asm) which does:
     *   pop gs; pop fs; pop es; pop ds; popad; add esp,8; iret */
    sp--; *sp = 0;   /* ebp for context_switch */
    sp--; *sp = 0;   /* ebx */
    sp--; *sp = 0;   /* esi */
    sp--; *sp = 0;   /* edi */

    /* Return address: jump to the interrupt return path */
    extern void fork_return_trampoline(void);
    sp--; *sp = (uint32_t)fork_return_trampoline;

    child->kernel_esp = (uint32_t)sp;

    child->parent_pid = parent->pid;
    child->time_slice = calculate_timeslice(child->priority);
    child->exit_status = 0;
    child->blocked_reason = 0;
    child->signal_pending = 0;
    child->signal_blocked = 0;
    child->wake_time = 0;

    for (int s = 0; s < 32; s++) {
        child->signal_handlers[s] = parent->signal_handlers[s];
    }

    for (int f = 0; f < MAX_OPEN_FILES; f++) {
        if (parent->fd_table[f]) {
            child->fd_table[f] = parent->fd_table[f];
            child->fd_table[f]->ref_count++;
        } else {
            child->fd_table[f] = (void *)0;
        }
    }

    child->next_sibling = parent->first_child;
    parent->first_child = child;
    child->first_child = (void *)0;

    spinlock_unlock(&process_lock);

    sched_add(child);
    return child->pid;
}

int process_exec(const char *path, char *const argv[])
{
    pcb_t *curr = sched_get_current();
    if (!curr) return -1;

    /* 1. Open the executable file via VFS */
    file_t *file = (void *)0;
    if (vfs_open(path, FILE_MODE_READ, &file) != 0) {
        return -1;
    }

    /* 2. Determine file size and read the entire ELF binary into a kernel buffer */
    uint32_t file_size = file->inode->size;
    if (file_size < sizeof(Elf32_Ehdr)) {
        vfs_close(file);
        return -1;
    }

    uint8_t *elf_buf = (uint8_t *)kmalloc(file_size);
    if (!elf_buf) {
        vfs_close(file);
        return -1;
    }

    int32_t bytes_read = vfs_read(file, elf_buf, file_size);
    vfs_close(file);

    if (bytes_read <= 0 || (uint32_t)bytes_read < sizeof(Elf32_Ehdr)) {
        kfree(elf_buf);
        return -1;
    }

    /* 3. Validate the ELF header */
    Elf32_Ehdr *hdr = (Elf32_Ehdr *)elf_buf;

    if (hdr->e_ident[EI_MAG0] != ELFMAG0 ||
        hdr->e_ident[EI_MAG1] != ELFMAG1 ||
        hdr->e_ident[EI_MAG2] != ELFMAG2 ||
        hdr->e_ident[EI_MAG3] != ELFMAG3) {
        kfree(elf_buf);
        return -1;
    }

    if (hdr->e_ident[EI_CLASS] != ELFCLASS32) {
        kfree(elf_buf);
        return -1;
    }

    if (hdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        kfree(elf_buf);
        return -1;
    }

    if (hdr->e_type != ET_EXEC) {
        kfree(elf_buf);
        return -1;
    }

    if (hdr->e_machine != EM_386) {
        kfree(elf_buf);
        return -1;
    }

    /* 4. Create a new address space */
    page_directory_t *new_dir = vmm_create_address_space();
    if (!new_dir) {
        kfree(elf_buf);
        return -1;
    }

    /* 5. Map all PT_LOAD segments into the new address space.
     * Must happen while CR3 is still the current directory. */
    uint32_t ph_offset = hdr->e_phoff;
    uint16_t ph_entry_size = hdr->e_phentsize;
    uint16_t ph_num = hdr->e_phnum;

    for (uint16_t i = 0; i < ph_num; i++) {
        Elf32_Phdr *ph = (Elf32_Phdr *)(elf_buf + ph_offset + i * ph_entry_size);

        if (ph->p_type != PT_LOAD) continue;

        uint32_t page_flags = VMM_PAGE_PRESENT | VMM_PAGE_USER;
        if (ph->p_flags & PF_W) page_flags |= VMM_PAGE_WRITABLE;

        uint32_t addr = ph->p_vaddr & 0xFFFFF000;
        uint32_t end_addr = ph->p_vaddr + ph->p_memsz;
        while (addr < end_addr) {
            void *phys = pmm_alloc_page();
            if (!phys) {
                vmm_destroy_address_space(new_dir);
                kfree(elf_buf);
                return -1;
            }
            vmm_map_page(new_dir, addr, (uint32_t)phys, page_flags);
            addr += PAGE_SIZE;
        }
    }

    /* 6. Set up the user stack (4 pages below USER_STACK_TOP) */
    for (int i = 0; i < 4; i++) {
        uint32_t stack_addr = USER_STACK_TOP - (i + 1) * PMM_PAGE_SIZE;
        void *phys = pmm_alloc_page();
        if (!phys) {
            vmm_destroy_address_space(new_dir);
            kfree(elf_buf);
            return -1;
        }
        vmm_map_page(new_dir, stack_addr, (uint32_t)phys,
                     VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE | VMM_PAGE_USER);
    }

    /* 7. Switch CR3 to the new address space and copy ELF data */
    uint32_t old_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(old_cr3));
    asm volatile("mov %0, %%cr3" : : "r"(new_dir) : "memory");

    for (uint16_t i = 0; i < ph_num; i++) {
        Elf32_Phdr *ph = (Elf32_Phdr *)(elf_buf + ph_offset + i * ph_entry_size);
        if (ph->p_type != PT_LOAD) continue;

        uint8_t *dest = (uint8_t *)ph->p_vaddr;
        memset(dest, 0, ph->p_memsz);
        memcpy(dest, elf_buf + ph->p_offset, ph->p_filesz);
    }

    /* Switch CR3 back */
    asm volatile("mov %0, %%cr3" : : "r"(old_cr3) : "memory");

    /* 8. Save the old address space before replacing */
    page_directory_t *old_dir = curr->page_dir;

    /* 9. Switch to the new address space */
    curr->page_dir = new_dir;
    vmm_set_current_dir(new_dir);

    /* 10. Destroy the old address space */
    if (old_dir) {
        vmm_destroy_address_space(old_dir);
    }

    /* 11. Update process metadata */
    curr->entry_point = hdr->e_entry;
    curr->user_stack = USER_STACK_TOP;

    /* Allocate a new 2-page kernel stack */
    if (alloc_kernel_stack(curr) != 0) {
        KERNEL_PANIC("process_exec: failed to allocate kernel stack");
    }

    /* Reset signal state */
    curr->signal_pending = 0;
    curr->signal_blocked = 0;
    for (int s = 0; s < 32; s++) {
        curr->signal_handlers[s] = SIG_DFL;
    }

    /* Free the ELF buffer */
    kfree(elf_buf);

    (void)argv;

    return 0;
}

pcb_t *process_get_pcb(pid_t pid)
{
    if (pid < 0 || pid >= MAX_PROCESSES) return (void *)0;
    return process_table[pid];
}
