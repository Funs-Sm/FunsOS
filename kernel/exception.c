#include "stdint.h"
#include "idt.h"
#include "panic.h"
#include "fpu.h"
#include "vmm.h"

static const char *exception_messages[] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bounds Check",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 FPU Error",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating Point",
    "Virtualization Exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved"
};

extern void vga_print(const char *str);
extern void vga_print_hex(uint32_t value);

void exception_handler(regs_t *regs) {
    /* #NM - Device Not Available: handle lazy FPU switching */
    if (regs->int_no == 7) {
        fpu_handler(regs);
        return;
    }

    vga_print("Exception: ");
    vga_print(exception_messages[regs->int_no]);
    vga_print("\n");

    vga_print("EIP: 0x");
    vga_print_hex(regs->eip);
    vga_print("  CS: 0x");
    vga_print_hex(regs->cs);
    vga_print("  EFLAGS: 0x");
    vga_print_hex(regs->eflags);
    vga_print("\n");

    vga_print("EAX: 0x");
    vga_print_hex(regs->eax);
    vga_print("  EBX: 0x");
    vga_print_hex(regs->ebx);
    vga_print("  ECX: 0x");
    vga_print_hex(regs->ecx);
    vga_print("  EDX: 0x");
    vga_print_hex(regs->edx);
    vga_print("\n");

    vga_print("ESI: 0x");
    vga_print_hex(regs->esi);
    vga_print("  EDI: 0x");
    vga_print_hex(regs->edi);
    vga_print("  EBP: 0x");
    vga_print_hex(regs->ebp);
    vga_print("  ESP: 0x");
    vga_print_hex(regs->esp_kernel);
    vga_print("\n");

    vga_print("Error Code: 0x");
    vga_print_hex(regs->err_code);
    vga_print("\n");

    if (regs->int_no == 14) {
        uint32_t cr2;
        asm volatile("mov %%cr2, %0" : "=r"(cr2));
        vga_print("Faulting Address (CR2): 0x");
        vga_print_hex(cr2);
        vga_print("\n");

        /* Try to handle the page fault (swap-in, COW, etc.) */
        if (vmm_handle_page_fault(regs->err_code, cr2) == 0) {
            return;
        }
    }

    if (regs->int_no != 3) {
        kernel_panic("Fatal exception", __FILE__, __LINE__);
    }
}

void init_exception(void) {
    extern uint32_t interrupt_entry_table[];
    for (int i = 0; i < 32; i++) {
        idt_set_gate(i, interrupt_entry_table[i], 0x08, 0x8E);
    }
}
