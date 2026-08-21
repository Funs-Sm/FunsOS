#include "thread.h"
#include "process.h"
#include "pmm.h"
#include "vmm.h"
#include "kheap.h"
#include "sched.h"
#include "string.h"
#include "tls.h"

static void thread_wrapper(void)
{
    pcb_t *curr = sched_get_current();
    if (curr && curr->entry_point) {
        func_t func = (func_t)curr->entry_point;
        void *arg = (void *)curr->context.eax;
        func(arg);
    }
    thread_exit();
}

pcb_t *thread_create(func_t func, void *arg, const char *name)
{
    pcb_t *proc = (pcb_t *)kcalloc(1, sizeof(pcb_t));
    if (!proc) return (void *)0;

    proc->pid = 0;
    proc->type = PROCESS_KERNEL;
    proc->state = PROCESS_READY;
    proc->sched_policy = PROCESS_NORMAL;
    /* Kernel threads share the kernel address space: without a valid
     * page_dir the scheduler would load CR3=0 on the first switch. */
    proc->page_dir = vmm_get_current_dir();

    /* 2-page kernel stack, mapped into the shared kernel page directory */
    void *phys0 = pmm_alloc_page();
    void *phys1 = pmm_alloc_page();
    if (!phys0 || !phys1) {
        if (phys0) pmm_free_page(phys0);
        if (phys1) pmm_free_page(phys1);
        kfree(proc);
        return (void *)0;
    }
    uint32_t stack_virt = (uint32_t)phys0 + VMM_KERNEL_BASE;
    vmm_map_page(proc->page_dir, stack_virt, (uint32_t)phys0,
                 VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE);
    vmm_map_page(proc->page_dir, stack_virt + PMM_PAGE_SIZE, (uint32_t)phys1,
                 VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE);

    proc->kernel_stack = stack_virt + 2 * PMM_PAGE_SIZE;
    proc->kstack_pages = 2;

    /* Stack layout expected by context_switch:
     *   [edi=0] [esi=0] [ebx=0] [ebp=0] [eflags=0x202] [ret=thread_wrapper]
     * thread_wrapper fetches func from entry_point and arg from
     * context.eax, then calls thread_exit() when it returns.  The initial
     * EFLAGS must have IF=1 so the new thread can receive timer ticks. */
    uint32_t *stack_top = (uint32_t *)proc->kernel_stack;
    *(--stack_top) = (uint32_t)thread_wrapper;
    *(--stack_top) = 0x202;  /* eflags: IF=1 (+ reserved bit 1) */
    *(--stack_top) = 0;  /* ebp */
    *(--stack_top) = 0;  /* ebx */
    *(--stack_top) = 0;  /* esi */
    *(--stack_top) = 0;  /* edi */

    proc->kernel_esp = (uint32_t)stack_top;
    proc->entry_point = (uint32_t)func;
    proc->context.eax = (uint32_t)arg;
    proc->context.eip = (uint32_t)thread_wrapper;

    if (name) {
        int i;
        for (i = 0; i < 31 && name[i]; i++) {
            proc->name[i] = name[i];
        }
        proc->name[i] = '\0';
    }

    proc->time_slice = DEFAULT_TIME_SLICE;
    proc->priority = 1;
    proc->parent_pid = 0;
    proc->first_child = (void *)0;
    proc->next_sibling = (void *)0;
    proc->next = (void *)0;
    proc->exit_status = 0;
    proc->blocked_reason = 0;
    proc->signal_pending = 0;
    proc->signal_blocked = 0;
    proc->wake_time = 0;

    int s;
    for (s = 0; s < 32; s++) {
        proc->signal_handlers[s] = SIG_DFL;
    }

    int f;
    for (f = 0; f < MAX_OPEN_FILES; f++) {
        proc->fd_table[f] = (void *)0;
    }

    sched_add(proc);
    return proc;
}

void thread_exit(void)
{
    pcb_t *curr = sched_get_current();
    if (!curr) return;

    /* Run TLS destructors before thread exits */
    tls_cleanup(curr);

    curr->state = PROCESS_ZOMBIE;
    curr->exit_status = 0;
    sched_remove(curr);
    schedule();
}

void thread_join(pcb_t *thread)
{
    while (thread->state != PROCESS_ZOMBIE) {
        thread_yield();
    }
}

void thread_yield(void)
{
    pcb_t *curr = sched_get_current();
    if (curr) {
        curr->time_slice = 0;
    }
    schedule();
}
