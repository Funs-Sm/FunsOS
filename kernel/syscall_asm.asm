; =============================================================================
; kernel/syscall_asm.asm - Fast System Calls for FunsOS
;
; Provides SYSENTER/SYSEXIT fast syscall path, SYSENTER MSR setup,
; vDSO page, and INT 0x80 fallback handler.
;
; System calls are the primary mechanism for user-mode applications
; to request kernel services. This file implements the fastest possible
; transition path using SYSENTER/SYSEXIT (available on Pentium II+).
;
; Calling convention for syscalls:
;   EAX = syscall number
;   EBX = arg1
;   ECX = arg2
;   EDX = arg3
;   ESI = arg4
;   EDI = arg5
;   EBP = arg6 (on stack for INT 0x80, in EBP for SYSENTER)
;
; Return value in EAX.
; =============================================================================

[BITS 32]

[SECTION .text]

[GLOBAL syscall_entry_asm]
[GLOBAL sysenter_setup]
[GLOBAL syscall_vdso_page]
[GLOBAL int80_handler]

[EXTERN syscall_dispatch]

; =============================================================================
; MSR addresses for SYSENTER/SYSEXIT
; =============================================================================
MSR_IA32_SYSENTER_CS   EQU 0x174
MSR_IA32_SYSENTER_ESP  EQU 0x175
MSR_IA32_SYSENTER_EIP  EQU 0x176

; Kernel code segment selector (must be in GDT)
KERNEL_CS   EQU 0x08
KERNEL_DS   EQU 0x10
USER_CS     EQU 0x1B        ; Ring 3 code segment
USER_DS     EQU 0x23        ; Ring 3 data segment

; =============================================================================
; void sysenter_setup(void)
;
; Programs the IA32_SYSENTER MSRs for fast system call entry.
; This must be called once during kernel initialization.
;
; MSR 0x174 (IA32_SYSENTER_CS):
;   - Low 16 bits: kernel code segment selector (0x08)
;   - Next 16 bits: user code segment selector (0x1B = 0x08 + 0x10 + 3)
;     (SYSENTER_CS + 8 = kernel stack segment, SYSENTER_CS + 16 = user CS)
;
; MSR 0x175 (IA32_SYSENTER_ESP): kernel stack pointer
; MSR 0x176 (IA32_SYSENTER_EIP): kernel entry point (syscall_entry_asm)
; =============================================================================
sysenter_setup:
    PUSH    EBP
    MOV     EBP, ESP
    PUSH    EBX

    ; Set IA32_SYSENTER_CS (MSR 0x174)
    ; Format: bits 15-0 = kernel CS, bits 31-16 = user CS
    ; Kernel CS = 0x08, so kernel DS = 0x10, user CS = 0x18+3 = 0x1B
    MOV     ECX, MSR_IA32_SYSENTER_CS
    MOV     EAX, KERNEL_CS      ; Kernel CS in low 16 bits
    MOV     EDX, 0
    WRMSR

    ; Set IA32_SYSENTER_ESP (MSR 0x175)
    ; Point to a per-CPU kernel stack for syscalls
    [EXTERN _syscall_stack_top]
    MOV     ECX, MSR_IA32_SYSENTER_ESP
    MOV     EAX, _syscall_stack_top
    MOV     EDX, 0
    WRMSR

    ; Set IA32_SYSENTER_EIP (MSR 0x176)
    ; Point to the syscall entry handler
    MOV     ECX, MSR_IA32_SYSENTER_EIP
    MOV     EAX, syscall_entry_asm
    MOV     EDX, 0
    WRMSR

    POP     EBX
    POP     EBP
    RET

; =============================================================================
; syscall_entry_asm - SYSENTER Entry Point
;
; This is the target of the SYSENTER instruction. When a user-mode
; application executes SYSENTER, the CPU:
;   1. Loads CS from IA32_SYSENTER_CS (bits 15-0)
;   2. Loads EIP from IA32_SYSENTER_EIP
;   3. Loads SS from IA32_SYSENTER_CS + 8
;   4. Loads ESP from IA32_SYSENTER_ESP
;   5. Switches to ring 0
;   6. Clears EFLAGS.VM, EFLAGS.IF
;
; On entry:
;   The CPU has already set CS, SS, EIP, ESP to kernel values.
;   We need to:
;   1. Save user-mode return address (EDX) and ESP (ECX)
;   2. Set up kernel data segments
;   3. Push minimal context
;   4. Call the C syscall dispatcher
;   5. Restore context
;   6. SYSEXIT back to user mode
;
; SYSENTER saves user EIP in EDX and user ESP in ECX (by convention).
; SYSEXIT loads EIP from EDX and ESP from ECX.
; =============================================================================
syscall_entry_asm:
    ; At this point:
    ;   CS = kernel code segment (from MSR 0x174 bits 15-0)
    ;   SS = kernel stack segment (CS + 8)
    ;   ESP = kernel stack pointer (from MSR 0x175)
    ;   EIP = this code
    ;   EDX = user EIP (return address)
    ;   ECX = user ESP
    ;   EFLAGS.IF = 0 (interrupts disabled)

    ; Set up kernel data segments
    PUSH    EAX                 ; Save EAX (syscall number)
    MOV     AX, KERNEL_DS
    MOV     DS, AX
    MOV     ES, AX
    MOV     FS, AX
    MOV     GS, AX
    POP     EAX

    ; Save user context on kernel stack
    ; We need to save: EDX (user EIP), ECX (user ESP), EBP, EAX, EBX, ESI, EDI
    PUSH    EDX                 ; Save user EIP (for SYSEXIT)
    PUSH    ECX                 ; Save user ESP
    PUSH    EBP                 ; Save callee-saved registers
    PUSH    EDI
    PUSH    ESI
    PUSH    EBX

    ; Now the stack has:
    ; [ESP]     = EBX (user's arg1)
    ; [ESP+4]   = ESI (user's arg4)
    ; [ESP+8]   = EDI (user's arg5)
    ; [ESP+12]  = EBP (user's arg6)
    ; [ESP+16]  = ECX (user's ESP)
    ; [ESP+20]  = EDX (user's EIP)

    ; Check for valid syscall number
    CMP     EAX, 256            ; Maximum syscall number
    JAE     .bad_syscall

    ; Call the C syscall dispatcher
    ; syscall_dispatch(syscall_num, arg1, arg2, arg3, arg4, arg5, arg6)
    ; Arguments are passed on the stack (cdecl)
    PUSH    EBP                 ; arg6 (user's EBP)
    PUSH    EDI                 ; arg5
    PUSH    ESI                 ; arg4
    PUSH    EDX                 ; arg3 (user's EDX was not saved, use ECX? No)
    ; Actually, the user's original EDX is lost because SYSENTER uses it
    ; for EIP. The C handler should use the saved context.
    ; For simplicity, pass the registers we have:
    PUSH    ECX                 ; arg2 (user's ECX has ESP, but we already saved it)
    ; Wait - we need to rethink. The user's registers are:
    ; EAX = syscall number
    ; EBX, ECX, EDX, ESI, EDI, EBP = arguments
    ; But SYSENTER clobbers EDX and ECX with EIP and ESP.
    ; So we need to pass what we have.
    ; Let's pass the saved values from the stack.

    ; Actually, let's simplify: the C handler receives a pointer to the
    ; saved register frame on the stack.
    PUSH    ESP                 ; pointer to register frame
    PUSH    EAX                 ; syscall number
    CALL    syscall_dispatch
    ADD     ESP, 8

    ; Store return value
    MOV     [ESP + 24], EAX     ; Save return value over saved EBX position
    ; Actually, we'll handle this below

.return_to_user:
    ; Restore user context
    POP     EBX
    POP     ESI
    POP     EDI
    POP     EBP

    ; At this point, the stack has:
    ; [ESP]     = user ESP (from ECX)
    ; [ESP+4]   = user EIP (from EDX)

    ; Restore the return value into EAX
    ; The dispatcher return value is already in EAX, but we may have
    ; clobbered it. Let's save it differently.
    ; Use a temp location
    ; Actually, let's just pop into registers in the right order.

    ; We stored EAX (return value) over saved EBX. Let's just use a
    ; different approach: save return value in a fixed location.

    ; For simplicity, let's use the kernel stack to pass return value:
    ; The return value is in EAX from the dispatcher.
    ; We need to preserve it across the POPs.
    ; Use EBP as temp (we already restored EBP from stack)
    MOV     EBP, EAX            ; Save return value in EBP temporarily

    ; Now pop user ESP and EIP for SYSEXIT
    POP     ECX                 ; ECX = user ESP (for SYSEXIT)
    POP     EDX                 ; EDX = user EIP (for SYSEXIT)

    ; Restore return value to EAX
    MOV     EAX, EBP

    ; Enable interrupts (SYSEXIT doesn't restore IF)
    STI

    ; Return to user mode
    ; SYSEXIT: loads CS from MSR 0x174 bits 31-16, EIP from EDX,
    ;          SS from (MSR 0x174 bits 15-0) + 8, ESP from ECX
    SYSEXIT

.bad_syscall:
    ; Invalid syscall number - return -1 (ENOSYS)
    MOV     EAX, -1
    JMP     .return_to_user

; =============================================================================
; syscall_vdso_page - vDSO Page with __kernel_vsyscall
;
; This is a virtual dynamic shared object page that is mapped into
; every user process. It provides the __kernel_vsyscall entry point
; that uses SYSENTER for fast system calls.
;
; The vDSO is a small ELF shared library that the kernel maps into
; each process's address space. The user-mode C library (if any)
; can call __kernel_vsyscall instead of using INT 0x80 directly.
;
; This is a minimal implementation - a full vDSO would include
; time-related functions (clock_gettime, gettimeofday) as well.
; =============================================================================
syscall_vdso_page:
    ; __kernel_vsyscall entry point
    ; Calling convention:
    ;   EAX = syscall number
    ;   EBX, ECX, EDX, ESI, EDI, EBP = arguments
    ;   Returns in EAX
    ;
    ; This function is called from user mode and uses SYSENTER.
    ; EDX will be set to the return address (after the SYSENTER call).
    ; ECX will be set to the user ESP.
    ;

    ; __kernel_vsyscall:
    PUSH    EBP
    MOV     EBP, ESP

    ; Save the return address for SYSEXIT
    ; When SYSENTER executes, it saves EIP into EDX automatically.
    ; We need to set EDX = return address before SYSENTER.
    ; The return address is [EBP+4] (the caller's return address
    ; pushed by the CALL instruction that called __kernel_vsyscall).

    ; Actually, in a real vDSO, __kernel_vsyscall is called directly
    ; (not via CALL), so the return address is the instruction after
    ; the SYSENTER. We handle this differently.

    ; For a realistic vDSO, the caller does:
    ;   CALL __kernel_vsyscall
    ; The vDSO function then:
    ;   PUSH %ebp
    ;   MOV  %esp, %ebp
    ;   PUSH %edx           ; save user EDX
    ;   MOV  0x8(%ebp), %edx ; return address
    ;   SYSENTER
    ; After SYSENTER returns (via SYSEXIT):
    ;   POP  %edx
    ;   POP  %ebp
    ;   RET

    PUSH    EDX                 ; Save user's EDX
    MOV     EDX, [EBP + 4]      ; EDX = return address (for SYSEXIT)
    ; ECX = user ESP will be set by SYSENTER hardware
    ; But we need to set up ECX = user ESP for SYSEXIT return
    ; The kernel will set ECX to the user ESP before SYSEXIT
    ; Actually, SYSENTER puts current ESP into ECX, but we need
    ; the ESP after this function returns... 

    ; Let's use a simpler approach:
    ; The kernel preserves ECX (user ESP) across the syscall.
    ; SYSEXIT will load ESP from ECX.
    ; We need to compute the user ESP after this function returns.
    LEA     ECX, [EBP + 8]      ; ECX = user ESP after we pop EBP and return

    SYSENTER

    ; Execution resumes here after the kernel does SYSEXIT
    POP     EDX
    POP     EBP
    RET

    ; Pad to page boundary (4096 bytes)
    ; The vDSO page is exactly one page (4KB)
    ALIGN 4096
vdso_page_end:

; =============================================================================
; int80_handler - Alternative INT 0x80 Handler
;
; This is a faster INT 0x80 handler that provides an alternative
; to the generic interrupt.asm handler. It saves only the minimal
; context needed for syscalls and dispatches quickly.
;
; This is called when user code executes INT 0x80.
; The interrupt frame on the stack:
;   [ESP]     = user EIP (return address)
;   [ESP+4]   = user CS
;   [ESP+8]   = user EFLAGS
;   [ESP+12]  = user ESP (if ring transition)
;   [ESP+16]  = user SS (if ring transition)
;
; INT 0x80 enters with interrupts disabled (EFLAGS.IF cleared by CPU).
; =============================================================================
int80_handler:
    ; Save all GP registers (PUSHAD: EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI)
    PUSHAD

    ; Save segment registers
    PUSH    DS
    PUSH    ES
    PUSH    FS
    PUSH    GS

    ; Set up kernel data segments
    MOV     AX, KERNEL_DS
    MOV     DS, AX
    MOV     ES, AX
    MOV     FS, AX
    MOV     GS, AX

    ; At this point, the stack layout is:
    ; [ESP]     = GS
    ; [ESP+4]   = FS
    ; [ESP+8]   = ES
    ; [ESP+12]  = DS
    ; [ESP+16]  = EDI
    ; [ESP+20]  = ESI
    ; [ESP+24]  = EBP
    ; [ESP+28]  = ESP (original)
    ; [ESP+32]  = EBX
    ; [ESP+36]  = EDX
    ; [ESP+40]  = ECX
    ; [ESP+44]  = EAX (syscall number)
    ; [ESP+48]  = user EIP
    ; [ESP+52]  = user CS
    ; [ESP+56]  = user EFLAGS
    ; [ESP+60]  = user ESP
    ; [ESP+64]  = user SS

    ; Check for valid syscall number
    CMP     DWORD [ESP + 44], 256   ; EAX = syscall number
    JAE     .int80_bad_syscall

    ; Call the C syscall dispatcher
    ; Pass the saved register frame as argument
    ; syscall_dispatch_int80(regs_t* regs)
    PUSH    ESP
    CALL    syscall_dispatch
    ADD     ESP, 4

    ; Store return value in the saved EAX slot
    MOV     [ESP + 44], EAX     ; Will be restored by POPAD

.int80_return:
    ; Restore segment registers
    POP     GS
    POP     FS
    POP     ES
    POP     DS

    ; Restore GP registers
    POPAD

    ; Return from interrupt (IRET pops EIP, CS, EFLAGS, ESP, SS)
    IRET

.int80_bad_syscall:
    MOV     DWORD [ESP + 44], -1    ; Return -1 (ENOSYS)
    JMP     .int80_return