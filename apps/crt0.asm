; =============================================================================
; apps/crt0.asm - User-mode C Runtime Startup (FunsOS)
;
; This is the entry point for ELF-loaded user-mode programs. The kernel's
; process_exec() places this at the e_entry field of an ELF ET_EXEC binary.
;
; ABI (System V i386):
;   On entry, the user-mode stack (ESP) is set by the kernel to:
;     [ESP]     = argc
;     [ESP+4]   = argv[0]   (pointer into stack)
;     [ESP+8]   = argv[1]
;     ...
;     [ESP+4+argc*4]  = NULL (argv terminator)
;     ... then argv strings immediately follow on the stack.
;
; We don't know the actual user-stack address statically, so the kernel's
; iret in process_first_run() loads ESP directly.  The job of crt0 is to:
;   1. Save the kernel-supplied argc / argv pointers
;   2. Clear the frame pointer
;   3. Push a final NULL for environ / argv[argc+1] compatibility
;   4. Call main(argc, argv)
;   5. If main returns, invoke SYS_EXIT with the return value
; =============================================================================

[BITS 32]

[GLOBAL _start]
[EXTERN main]

; -------------------------------------------------------------------
; void _start(void) - kernel transfers control here via iret
;
; On entry (in user mode, ring 3):
;   EAX / ECX / EDX are undefined (clobbered by kernel entry path)
;   EBX / ESI / EDI / EBP are undefined
;   ESP points at argc (see ABI comment above)
; -------------------------------------------------------------------
_start:
    ; Save the kernel-provided argc and argv pointer.  We cannot rely on
    ; EBP being set up yet (no CALL instruction preceded us), so use ESI/EDI.
    mov     esi, [esp]            ; esi = argc
    lea     edi, [esp + 4]        ; edi = &argv[0] (pointer table)

    ; Construct a null-terminated envp pointer list just past argv to make
    ; the stack look more like a real Linux startup.  We don't actually
    ; populate envp here (the kernel exec path doesn't pass it), so a
    ; single NULL word is sufficient.
    lea     eax, [edi + esi*4 + 4] ; eax = &argv[argc+1]
    mov     [eax], dword 0         ; envp[0] = NULL

    ; Set up a clean stack frame for main().  main() may legitimately use
    ; __builtin_return_address(0) etc., so push a return address.
    push    ebp
    mov     ebp, esp

    ; Push envp, then argv, then argc per cdecl convention
    ; (esi holds argc, edi holds &argv[0], eax holds &envp[0])
    push    eax                    ; envp
    push    edi                    ; argv
    push    esi                    ; argc
    call    main

    ; main() returned; exit with its return value as the exit code
    ; SYS_EXIT (1) takes the status in ebx on FunsOS.
    mov     ebx, eax
    mov     eax, 1                 ; SYS_EXIT
    int     0x80

    ; If the syscall returns (it shouldn't), loop forever
.hang:  hlt
    jmp     .hang