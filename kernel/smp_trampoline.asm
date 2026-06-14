; smp_trampoline.asm - AP trampoline code
; This 16-bit code runs at physical address 0x8000
; It switches the AP from real mode to protected mode, then jumps to C code.
;
; NOTE: The trampoline code is copied to 0x8000 by the BSP.
; All labels within the trampoline must use relative addressing
; since the link address (0x100000+) differs from the run address (0x8000).

[BITS 16]

section .text progbits alloc exec nowrite align=16

global _smp_trampoline_start

_smp_trampoline_start:
    ; Disable interrupts
    cli

    ; Load our data segments
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; Enable A20 line via fast A20 gate
    in al, 0x92
    or al, 0x02
    and al, 0xFE
    out 0x92, al

    ; Load the GDT (pointer at 0x9000)
    lgdt [0x9000]

    ; Switch to protected mode - set PE bit in CR0
    mov eax, cr0
    or eax, 0x01
    mov cr0, eax

    ; Far jump to flush pipeline and enter 32-bit mode
    ; Use 0x8000 + offset since code runs at 0x8000
    db 0x66, 0xEA          ; jmp dword 0x08:offset (32-bit far jump)
    dd pm_entry - _smp_trampoline_start + 0x8000
    dw 0x08

[BITS 32]
pm_entry:
    ; We are now in 32-bit protected mode

    ; Set up segment selectors
    mov ax, 0x10        ; Data segment selector (index 2, GDT)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Set up the stack (pointer at 0x9000 + 0x1000)
    mov esp, [0x9000 + 0x1000]

    ; Signal ready to BSP (flag at 0x9000 + 0x1010)
    mov dword [0x9000 + 0x1010], 1

    ; Jump to the C entry point (address at 0x9000 + 0x1008)
    mov eax, [0x9000 + 0x1008]
    call eax

    ; If the C entry returns, halt
.halt:
    cli
    hlt
    jmp .halt

global _smp_trampoline_end
_smp_trampoline_end:
