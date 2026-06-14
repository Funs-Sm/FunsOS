[BITS 32]

[SECTION .text]

[GLOBAL _start]
[GLOBAL __stack_chk_fail]
[GLOBAL __stack_chk_guard]

[EXTERN _kernel_main]
[EXTERN _gdt_ptr]
[EXTERN _idt_ptr]

_start:
    CLI

    MOV ESP, stack_top

    PUSH EAX

    MOV AX, 0x10
    MOV DS, AX
    MOV ES, AX
    MOV FS, AX
    MOV GS, AX
    MOV SS, AX

    ; Zero BSS section before any C code runs.
    ; Without this, BSS variables (gdt_ptr, idt_ptr, etc.) contain
    ; garbage from memory, causing LGDT/LIDT to load invalid tables.
    EXTERN __bss_start
    EXTERN __bss_end
    MOV EDI, __bss_start
    MOV ECX, __bss_end
    SUB ECX, EDI
    XOR EAX, EAX
    REP STOSB

    ; Do NOT load GDT/IDT here - the BSS was just zeroed, so
    ; gdt_ptr/idt_ptr are 0.  Loading them would set up invalid
    ; tables.  init_gdt() and init_idt() in C will set them up
    ; properly and execute LGDT/LIDT themselves.
    ; The segment registers (0x10) still use the descriptor cache
    ; from the loader's GDT, which is valid until init_gdt() replaces it.

    CALL _kernel_main

    MOV EDI, 0xC00B8000
    MOV ESI, msg_returned
    MOV AH, 0x0C
.print_loop:
    LODSB
    OR AL, AL
    JZ .print_done
    MOV [EDI], AX
    ADD EDI, 2
    JMP .print_loop
.print_done:
    CLI
.halt:
    HLT
    JMP .halt

; ---- Stack Canary 实现 ----
; Canary 检查失败处理
__stack_chk_fail:
    ; 打印错误信息到 VGA 文本缓冲区
    MOV EDI, 0xC00B8000
    MOV ESI, message_canary
    MOV AH, 0x4F      ; 红底白字
.canary_print_loop:
    LODSB
    OR AL, AL
    JZ .canary_print_done
    MOV [EDI], AX
    ADD EDI, 2
    JMP .canary_print_loop
.canary_print_done:
    ; panic - 无限挂起
    CLI
.canary_halt:
    HLT
    JMP .canary_halt

[SECTION .data]

; Canary 值 (随机生成)
__stack_chk_guard:
    DD 0xDEADBEEF

[SECTION .rodata]

msg_returned: DB 'kernel_main returned!', 0
message_canary: DB 'FATAL: Stack buffer overflow detected!', 0

[SECTION .bss]

ALIGN 16
resb 8192
stack_top:
