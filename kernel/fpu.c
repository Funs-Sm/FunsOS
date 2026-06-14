#include "fpu.h"
#include "sched.h"

void fpu_init(void) {
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1 << 2);  /* Clear CR0.EM (emulate) */
    cr0 |= (1 << 1);   /* Set CR0.MP (monitor coprocessor) */
    cr0 |= (1 << 5);   /* Set CR0.NE (numeric error) */
    asm volatile("mov %0, %%cr0" : : "r"(cr0));

    /* Clear CR0.TS to enable FPU */
    asm volatile("clts" ::: "memory");

    /* Set up CR4 for SSE: OSFXSR and OSXMMEXCPT */
    uint32_t cr4;
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1 << 9);   /* CR4.OSFXSR */
    cr4 |= (1 << 10);  /* CR4.OSXMMEXCPT */
    asm volatile("mov %0, %%cr4" : : "r"(cr4));

    /* Initialize FPU to a known state */
    asm volatile("finit" ::: "memory");
}

void fpu_save(pcb_t *proc) {
    if (!proc) return;

    if (proc->fpu_saved) {
        asm volatile("fnsave %0" : "=m"(proc->fpu_state) :: "memory");
    }

    /* Set CR0.TS to trigger lazy switching */
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= (1 << 3);   /* Set CR0.TS */
    asm volatile("mov %0, %%cr0" : : "r"(cr0));
}

void fpu_restore(pcb_t *proc) {
    if (!proc || !proc->fpu_saved) return;

    /* Clear CR0.TS first */
    asm volatile("clts" ::: "memory");

    asm volatile("frstor %0" :: "m"(proc->fpu_state) : "memory");
}

void fpu_handler(regs_t *regs) {
    (void)regs;

    /* Clear CR0.TS */
    asm volatile("clts" ::: "memory");

    pcb_t *curr = sched_get_current();
    if (curr) {
        if (curr->fpu_saved) {
            /* Restore previously saved FPU state */
            asm volatile("frstor %0" :: "m"(curr->fpu_state) : "memory");
        } else {
            /* First FPU use: initialize FPU to clean state */
            asm volatile("finit" ::: "memory");
            curr->fpu_saved = 1;
        }
    }
}
