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
    /* Simple timeslice calculation: higher priority = more time */
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

        uint8_t *dest = (uint8_t *)ph->p_vaddr;
        memset(dest, 0, ph->p_memsz);
        memcpy(dest, elf_data + ph->p_offset, ph->p_filesz);
    }

    for (int i = 0; i < 4; i++) {
        uint32_t stack_addr = USER_STACK_TOP - (i + 1) * PMM_PAGE_SIZE;
        void *phys = pmm_alloc_page();
        if (!phys) {
            goto err_free_page_dir;
        }
        vmm_map_page(proc->page_dir, stack_addr, (uint32_t)phys,
                     VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE | VMM_PAGE_USER);
    }

    void *kernel_stack_phys = pmm_alloc_page();
    if (!kernel_stack_phys) {
        goto err_free_page_dir;
    }
    proc->user_stack = USER_STACK_TOP;
    proc->kernel_stack = (uint32_t)kernel_stack_phys + PMM_PAGE_SIZE + VMM_KERNEL_BASE;
    proc->entry_point = hdr->e_entry;

    proc->context.eax = 0;
    proc->context.ebx = 0;
    proc->context.ecx = 0;
    proc->context.edx = 0;
    proc->context.esi = 0;
    proc->context.edi = 0;
    proc->context.ebp = 0;
    proc->context.eip = hdr->e_entry;
    proc->context.cs = 0x1B;
    proc->context.eflags = 0x202;
    proc->context.useresp = USER_STACK_TOP;
    proc->context.ss = 0x23;
    proc->context.esp_kernel = proc->kernel_stack;
    proc->context.int_no = 0;
    proc->context.err_code = 0;

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

    process_table[curr->pid] = (void *)0;

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
    child->kernel_stack = (uint32_t)pmm_alloc_page() + PMM_PAGE_SIZE + VMM_KERNEL_BASE;
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

    /* 5. Load each PT_LOAD segment into the new address space */
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

        /* Copy segment data: zero-fill memsz then copy filesz */
        uint8_t *dest = (uint8_t *)ph->p_vaddr;
        memset(dest, 0, ph->p_memsz);
        memcpy(dest, elf_buf + ph->p_offset, ph->p_filesz);
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

    /* 7. Save the old address space before replacing */
    page_directory_t *old_dir = curr->page_dir;

    /* 8. Switch to the new address space */
    curr->page_dir = new_dir;
    vmm_set_current_dir(new_dir);

    /* 9. Destroy the old address space */
    if (old_dir) {
        vmm_destroy_address_space(old_dir);
    }

    /* 10. Update process metadata */
    curr->entry_point = hdr->e_entry;
    curr->user_stack = USER_STACK_TOP;

    /* Allocate a new kernel stack */
    void *kernel_stack_phys = pmm_alloc_page();
    if (!kernel_stack_phys) {
        KERNEL_PANIC("process_exec: failed to allocate kernel stack");
    }
    curr->kernel_stack = (uint32_t)kernel_stack_phys + PMM_PAGE_SIZE + VMM_KERNEL_BASE;

    /* 11. Set up registers for user mode entry */
    curr->context.eax = 0;
    curr->context.ebx = 0;
    curr->context.ecx = 0;
    curr->context.edx = 0;
    curr->context.esi = 0;
    curr->context.edi = 0;
    curr->context.ebp = 0;
    curr->context.eip = hdr->e_entry;
    curr->context.cs = 0x1B;
    curr->context.eflags = 0x202;
    curr->context.useresp = USER_STACK_TOP;
    curr->context.ss = 0x23;
    curr->context.esp_kernel = curr->kernel_stack;
    curr->context.int_no = 0;
    curr->context.err_code = 0;

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
