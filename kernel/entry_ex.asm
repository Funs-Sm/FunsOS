; =============================================================================
; kernel/entry_ex.asm - Extended Entry (Boot Enhancements) for FunsOS
;
; Provides Multiboot/Multiboot2 headers, early CPU detection, FPU/SSE
; initialization, BSS zeroing, and quick self-tests.
;
; This file is linked early in the kernel and provides the entry point
; for Multiboot-compliant bootloaders (GRUB, etc.).
; =============================================================================

[BITS 32]

; ---- Multiboot Constants ----
MULTIBOOT_MAGIC     EQU 0x1BADB002
MULTIBOOT_FLAGS     EQU 0x00000007  ; Align modules | Memory info | Video info
MULTIBOOT_CHECKSUM  EQU -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)

; Multiboot2 magic
MULTIBOOT2_MAGIC    EQU 0xE85250D6
MULTIBOOT2_ARCH     EQU 0           ; i386

[SECTION .multiboot]

; =============================================================================
; Multiboot Header (legacy)
; =============================================================================
ALIGN 4
multiboot_header:
    DD MULTIBOOT_MAGIC
    DD MULTIBOOT_FLAGS
    DD MULTIBOOT_CHECKSUM

    ; Address fields (required by flags bit 0)
    DD multiboot_header     ; header_addr
    DD _start               ; load_addr
    DD 0                    ; load_end_addr (filled by linker)
    DD 0                    ; bss_end_addr (filled by linker)
    DD _start_ex             ; entry_addr

    ; Video mode requests (flags bit 2)
    DD 0                    ; mode_type: 0 = linear graphics
    DD 1024                 ; width
    DD 768                  ; height
    DD 32                   ; depth

; =============================================================================
; Multiboot2 Header
; =============================================================================
ALIGN 8
multiboot2_header_start:
    DD MULTIBOOT2_MAGIC
    DD MULTIBOOT2_ARCH
    DD multiboot2_header_end - multiboot2_header_start
    DD -(MULTIBOOT2_MAGIC + MULTIBOOT2_ARCH + (multiboot2_header_end - multiboot2_header_start))

    ; Information request tag
    ALIGN 8
    DW 1                    ; type: information request
    DW 0                    ; flags
    DD 32                   ; size: 8 + 4*6 = 32
    DD 4                    ; basic memory info
    DD 5                    ; boot device
    DD 6                    ; memory map
    DD 9                    ; ELF symbols
    DD 8                    ; framebuffer info
    DD 14                   ; ACPI RSDP v1

    ; Address tag
    ALIGN 8
    DW 2                    ; type: address
    DW 0                    ; flags
    DD 24                   ; size
    DD multiboot2_header_start
    DD _start               ; load address
    DD 0                    ; load end (filled by linker)
    DD 0                    ; BSS end (filled by linker)

    ; Entry address tag
    ALIGN 8
    DW 3                    ; type: entry address
    DW 0                    ; flags
    DD 12                   ; size
    DD _start_ex             ; entry point

    ; Framebuffer tag
    ALIGN 8
    DW 5                    ; type: framebuffer
    DW 0                    ; flags
    DD 20                   ; size
    DD 1024                 ; width
    DD 768                  ; height
    DD 32                   ; depth

    ; End tag
    ALIGN 8
    DW 0                    ; type: end
    DW 0                    ; flags
    DD 8                    ; size

multiboot2_header_end:

[SECTION .text]

[GLOBAL _start_ex]
[GLOBAL multiboot_magic]
[GLOBAL multiboot_info]
[GLOBAL cpu_features]
[GLOBAL cpu_vendor]

[EXTERN _kernel_main]
[EXTERN _gdt_ptr]
[EXTERN _idt_ptr]

; =============================================================================
; _start_ex - Multiboot-compliant kernel entry point (GRUB)
;
; Called by the bootloader (GRUB) with:
;   EAX = Multiboot magic number (0x2BADB002)
;   EBX = Pointer to Multiboot information structure
;
; Performs:
;   1. Save boot info
;   2. Set up stack
;   3. CPU detection (CPUID, features)
;   4. FPU/SSE initialization
;   5. BSS zeroing
;   6. Quick self-test
;   7. Jump to kernel_main
;
; NOTE: This entry point requires Multiboot-compliant bootloader (GRUB).
; For the custom bootloader, kernel/entry.asm's _start is used instead.
; =============================================================================
_start_ex:
    CLI

    ; Save boot information
    MOV     [multiboot_magic], EAX
    MOV     [multiboot_info], EBX

    ; Set up stack (defined in linker script or entry.asm)
    MOV     ESP, stack_top

    ; Set up segment registers to kernel data segment
    MOV     AX, 0x10
    MOV     DS, AX
    MOV     ES, AX
    MOV     FS, AX
    MOV     GS, AX
    MOV     SS, AX

    ; ---- Early CPU Detection ----
    CALL    early_cpu_detect

    ; ---- Early FPU/SSE Initialization ----
    CALL    early_fpu_sse_init

    ; ---- BSS Zeroing ----
    CALL    zero_bss

    ; ---- Quick Self-Test ----
    CALL    quick_self_test

    ; ---- Load GDT and IDT ----
    LGDT    [_gdt_ptr]
    LIDT    [_idt_ptr]

    ; Reload CS with far jump
    JMP     0x08:.reload_cs
.reload_cs:
    ; Reload data segments
    MOV     AX, 0x10
    MOV     DS, AX
    MOV     ES, AX
    MOV     FS, AX
    MOV     GS, AX
    MOV     SS, AX

    ; Jump to C kernel main
    PUSH    DWORD [multiboot_magic]
    PUSH    DWORD [multiboot_info]
    CALL    _kernel_main
    ADD     ESP, 8

    ; If kernel_main returns, halt
    CLI
.halt:
    HLT
    JMP     .halt

; =============================================================================
; early_cpu_detect - Detect CPU features using CPUID
;
; Checks:
;   - CPUID support
;   - Vendor string
;   - Feature flags (stored in cpu_features)
;   - 64-bit (Long Mode) support
; =============================================================================
early_cpu_detect:
    PUSHAD

    ; --- Check CPUID support ---
    ; Try to flip ID bit (bit 21) in EFLAGS
    PUSHFD
    POP     EAX
    MOV     ECX, EAX
    XOR     EAX, (1 << 21)      ; Toggle ID bit
    PUSH    EAX
    POPFD
    PUSHFD
    POP     EAX
    XOR     EAX, ECX
    JZ      .no_cpuid           ; ID bit didn't change -> no CPUID

    ; CPUID is supported
    ; Save flag
    MOV     DWORD [cpu_features], 1  ; Feature 0 = CPUID available

    ; --- Get vendor string (CPUID leaf 0) ---
    XOR     EAX, EAX
    CPUID
    MOV     [cpu_vendor], EBX        ; "Genu"
    MOV     [cpu_vendor + 4], EDX    ; "ineI"
    MOV     [cpu_vendor + 8], ECX    ; "ntel"
    MOV     BYTE [cpu_vendor + 12], 0 ; Null terminate

    ; --- Get feature flags (CPUID leaf 1) ---
    MOV     EAX, 1
    CPUID
    ; EDX = feature flags 1
    ; ECX = feature flags 2
    MOV     [cpu_features + 4], EDX  ; Feature flags 1
    MOV     [cpu_features + 8], ECX  ; Feature flags 2

    ; --- Check for Long Mode (64-bit) support ---
    ; CPUID leaf 0x80000001, EDX bit 29
    MOV     EAX, 0x80000000
    CPUID
    CMP     EAX, 0x80000001
    JB      .no_long_mode       ; Extended leaves not available

    MOV     EAX, 0x80000001
    CPUID
    TEST    EDX, (1 << 29)      ; LM bit
    JZ      .no_long_mode
    OR      DWORD [cpu_features], 2  ; Feature 1 = Long Mode available

.no_long_mode:
.no_cpuid:
    POPAD
    RET

; =============================================================================
; early_fpu_sse_init - Initialize FPU and SSE
;
; Enables:
;   - FPU (CR0.EM=0, CR0.MP=1)
;   - SSE (CR4.OSFXSR=1, CR4.OSXMMEXCPT=1)
;   - Initializes FPU control word
;   - Initializes MXCSR
; =============================================================================
early_fpu_sse_init:
    ; Check if FPU is available (CR0.EM clear)
    MOV     EAX, CR0
    AND     EAX, ~(1 << 2)      ; Clear EM (bit 2) - don't emulate FPU
    OR      EAX, (1 << 1)       ; Set MP (bit 1) - monitor coprocessor
    AND     EAX, ~(1 << 3)      ; Clear TS (bit 3) - no task switched
    MOV     CR0, EAX

    ; Initialize FPU
    FNINIT

    ; Set FPU control word: 0x037F
    ; round to nearest, 64-bit precision, all exceptions masked
    SUB     ESP, 4
    FSTCW   [ESP]
    MOV     WORD [ESP], 0x037F
    FLDCW   [ESP]
    ADD     ESP, 4

    ; Clear pending exceptions
    FNCLEX

    ; Check if SSE is available
    TEST    DWORD [cpu_features + 4], (1 << 25)  ; SSE bit in EDX
    JZ      .no_sse

    ; Check if SSE is available (bit 26 = SSE2)
    ; We already checked for SSE above

    ; Enable SSE in CR4
    MOV     EAX, CR4
    OR      EAX, (1 << 9)       ; OSFXSR
    OR      EAX, (1 << 10)      ; OSXMMEXCPT
    MOV     CR4, EAX

    ; Initialize MXCSR: 0x1F80
    ; flush-to-zero off, round-to-nearest, all exceptions masked
    SUB     ESP, 4
    MOV     DWORD [ESP], 0x1F80
    LDMXCSR [ESP]
    ADD     ESP, 4

.no_sse:
    RET

; =============================================================================
; zero_bss - Clear the .bss section
;
; Uses the linker-defined symbols __bss_start and __bss_end.
; If these are not defined, this function does nothing.
; =============================================================================
zero_bss:
    ; Linker symbols for BSS boundaries
    [EXTERN __bss_start]
    [EXTERN __bss_end]

    MOV     EDI, __bss_start
    MOV     ECX, __bss_end
    SUB     ECX, EDI            ; ECX = BSS size in bytes
    JZ      .done
    JS      .done               ; Negative size? Skip

    ; Zero the BSS using STOSD
    XOR     EAX, EAX

    ; Do DWORD-aligned clear first
    MOV     EDX, ECX
    SHR     EDX, 2
    JZ      .byte_clear
    MOV     EBX, ECX
    MOV     ECX, EDX
    REP STOSD
    MOV     ECX, EBX
    AND     ECX, 0x03
    JZ      .done

.byte_clear:
    REP STOSB

.done:
    RET

; =============================================================================
; quick_self_test - Verify critical data structures
;
; Checks:
;   - GDT pointer is valid (non-null)
;   - IDT pointer is valid (non-null)
;   - Stack pointer is sane
;   - Multiboot magic is correct
; =============================================================================
quick_self_test:
    ; Verify GDT pointer
    CMP     DWORD [_gdt_ptr], 0
    JE      .self_test_fail

    ; Verify IDT pointer
    CMP     DWORD [_idt_ptr], 0
    JE      .self_test_fail

    ; Verify stack pointer is in valid range
    CMP     ESP, 0x100000       ; Should be above 1MB
    JB      .self_test_fail

    ; Verify Multiboot magic
    CMP     DWORD [multiboot_magic], 0x2BADB002
    JE      .self_test_ok

    ; Could also be Multiboot2 (0x36D76289)
    CMP     DWORD [multiboot_magic], 0x36D76289
    JE      .self_test_ok

.self_test_fail:
    ; Self-test failed - print error and halt
    MOV     EDI, 0xC00B8000
    MOV     ESI, .msg_test_fail
    MOV     AH, 0x4F
.print_fail:
    LODSB
    TEST    AL, AL
    JZ      .halt_fail
    MOV     [EDI], AX
    ADD     EDI, 2
    JMP     .print_fail
.halt_fail:
    CLI
    HLT
    JMP     .halt_fail

.self_test_ok:
    RET

.msg_test_fail: DB 'BOOT: Self-test FAILED!', 0

; =============================================================================
; Stack Canary Support
; =============================================================================
[GLOBAL __stack_chk_fail]
[GLOBAL __stack_chk_guard]

__stack_chk_fail:
    ; Stack buffer overflow detected!
    MOV     EDI, 0xC00B8000
    MOV     ESI, .msg_canary
    MOV     AH, 0x4F
.canary_print:
    LODSB
    TEST    AL, AL
    JZ      .canary_halt
    MOV     [EDI], AX
    ADD     EDI, 2
    JMP     .canary_print
.canary_halt:
    CLI
    HLT
    JMP     .canary_halt

.msg_canary: DB 'FATAL: Stack smashing detected!', 0

; =============================================================================
; Data Section
; =============================================================================
[SECTION .data]

ALIGN 4
multiboot_magic:
    DD 0
multiboot_info:
    DD 0

; CPU feature flags array:
; [0] = CPUID available (1) or not (0)
; [1] = Long Mode available (2) or not (0)
; [4] = Feature flags from CPUID leaf 1, EDX
; [8] = Feature flags from CPUID leaf 1, ECX
ALIGN 4
cpu_features:
    DD 0, 0, 0, 0

; CPU vendor string (12 bytes + null)
ALIGN 4
cpu_vendor:
    DB 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0

; Stack canary guard value
ALIGN 4
__stack_chk_guard:
    DD 0xDEADBEEF

; =============================================================================
; BSS Section
; =============================================================================
[SECTION .bss]

ALIGN 16
RESB 16384                       ; 16KB kernel stack
stack_top: