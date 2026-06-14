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

    uint32_t stack_phys = (uint32_t)pmm_alloc_page();
    if (!stack_phys) {
        kfree(proc);
        return (void *)0;
    }
    uint32_t stack_virt = stack_phys + VMM_KERNEL_BASE;
    vmm_map_page(proc->page_dir, stack_virt, stack_phys, VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE);

    proc->kernel_stack = stack_virt + PMM_PAGE_SIZE;

    uint32_t *stack_top = (uint32_t *)proc->kernel_stack;

    *(--stack_top) = 0x202;
    *(--stack_top) = 0x08;
    *(--stack_top) = (uint32_t)thread_wrapper;
    *(--stack_top) = 0;
    *(--stack_top) = 0;
    *(--stack_top) = 0;
    *(--stack_top) = (uint32_t)arg;
    *(--stack_top) = 0;
    *(--stack_top) = proc->kernel_stack;
    *(--stack_top) = 0;
    *(--stack_top) = 0;
    *(--stack_top) = 0;

    proc->context.eax = (uint32_t)arg;
    proc->context.eip = (uint32_t)thread_wrapper;
    proc->context.cs = 0x08;
    proc->context.eflags = 0x202;
    proc->context.esp_kernel = (uint32_t)stack_top;
    proc->entry_point = (uint32_t)func;

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
