; =============================================================================
; kernel/context.asm - Context Switch Assembly for FunsOS
;
; Provides low-level context switching, FPU/SSE state save/restore,
; and control register access functions.
;
; All functions follow cdecl calling convention.
; Task context layout (saved on stack):
;   Offset  Size  Register
;   ------  ----  --------
;   0       4     CR3
;   4       4     EIP (return address, set by CALL)
;   8       4     EFLAGS
;   12      4     EAX
;   16      4     ECX
;   20      4     EDX
;   24      4     EBX
;   28      4     ESP (before context save)
;   32      4     EBP
;   36      4     ESI
;   40      4     EDI
;   44      2     ES
;   46      2     padding
;   48      2     DS
;   50      2     padding
;   52      2     FS
;   54      2     padding
;   56      2     GS
;   58      2     padding
;   60      2     SS
;   62      2     padding
;   Total: 64 bytes
; =============================================================================

[BITS 32]

[SECTION .text]

[GLOBAL _context_switch]
[GLOBAL _fork_return_trampoline]
[GLOBAL _context_switch_to]
[GLOBAL _sse_init]
[GLOBAL _get_cr0]
[GLOBAL _get_cr2]
[GLOBAL _get_cr3]
[GLOBAL _get_cr4]
[GLOBAL _set_cr0]
[GLOBAL _set_cr3]
[GLOBAL _get_eflags]

; =============================================================================
; void context_switch(uint32_t *old_esp, uint32_t new_esp)
;
; Saves callee-saved registers, switches stacks, restores, returns.
; For a new process the stack contains:
;   [edi=0] [esi=0] [ebx=0] [ebp=0] [return-address -> trampoline]
; For a preempted process the stack contains:
;   [edi] [esi] [ebx] [ebp] [return-address -> schedule() after call]
;
; Parameters:
;   [ESP+4]  = old_esp - pointer to where to save current ESP
;   [ESP+8]  = new_esp - stack pointer of the new task
; =============================================================================
_context_switch:
    PUSH    EBP
    PUSH    EBX
    PUSH    ESI
    PUSH    EDI

    MOV     EAX, [ESP + 20]     ; old_esp (4 pushes + ret addr + 2 args = 20)
    MOV     ECX, [ESP + 24]     ; new_esp

    MOV     [EAX], ESP          ; save old ESP

    MOV     ESP, ECX            ; switch to new stack

    POP     EDI
    POP     ESI
    POP     EBX
    POP     EBP

    RET

; =============================================================================
; void fork_return_trampoline(void)
;
; Used by process_fork to resume the child. The child's kernel stack
; contains a regs_t frame (same layout as isr_common_stub / irq_common_stub
; would leave). We just need to restore registers and iret.
; =============================================================================
_fork_return_trampoline:
    POP     GS
    POP     FS
    POP     ES
    POP     DS
    POPAD
    ADD     ESP, 8              ; skip int_no and err_code
    IRET

; =============================================================================
; void context_switch_to(uint32_t* task_esp)
;
; Directly switches to a task by loading its stack pointer and
; returning into it. The target stack must have EIP at the top.
;
; This is used when switching to a newly created task for the first time.
; The new task's stack should have EIP on top (as if prepared by
; the task creation code).
;
; Stack: [EBP+8]=task_esp
; =============================================================================
_context_switch_to:
    PUSH    EBP
    MOV     EBP, ESP

    ; Load the new task's ESP
    MOV     ESP, [EBP + 8]

    ; The new task's stack should have EIP on top (placed there
    ; by the task creation code). We just RET to jump to it.
    ; But first, we need to restore EBP from the new task's stack.
    POP     EBP                 ; Restore new task's EBP
    RET                         ; Jump to new task's EIP

; =============================================================================
; void sse_init(void)
;
; Enables SSE support by setting the appropriate control register bits:
; - CR0.EM = 0  (don't emulate FPU)
; - CR0.MP = 1  (monitor coprocessor)
; - CR0.TS = 0  (no task switched)
; - CR4.OSFXSR = 1 (enable FXSAVE/FXRSTOR)
; - CR4.OSXMMEXCPT = 1 (enable #XF exception for SSE)
; =============================================================================
_sse_init:
    ; Enable SSE in CR0
    MOV     EAX, CR0
    AND     EAX, ~(1 << 2)      ; Clear EM (bit 2) - don't emulate
    OR      EAX, (1 << 1)       ; Set MP (bit 1) - monitor coprocessor
    AND     EAX, ~(1 << 3)      ; Clear TS (bit 3) - no task switched
    MOV     CR0, EAX

    ; Enable SSE in CR4
    MOV     EAX, CR4
    OR      EAX, (1 << 9)       ; Set OSFXSR (bit 9) - enable FXSAVE/FXRSTOR
    OR      EAX, (1 << 10)      ; Set OSXMMEXCPT (bit 10) - enable SSE exceptions
    MOV     CR4, EAX

    ; Initialize SSE MXCSR register
    ; Default: 0x1F80 = all exceptions masked, flush-to-zero off,
    ;           round-to-nearest, denormals-are-zero off
    SUB     ESP, 4
    MOV     DWORD [ESP], 0x1F80
    LDMXCSR [ESP]
    ADD     ESP, 4

    RET

; =============================================================================
; uint32_t get_cr0(void)
;
; Returns the value of control register CR0.
; =============================================================================
_get_cr0:
    MOV     EAX, CR0
    RET

; =============================================================================
; uint32_t get_cr2(void)
;
; Returns the value of control register CR2 (page fault linear address).
; =============================================================================
_get_cr2:
    MOV     EAX, CR2
    RET

; =============================================================================
; uint32_t get_cr3(void)
;
; Returns the value of control register CR3 (page directory base).
; =============================================================================
_get_cr3:
    MOV     EAX, CR3
    RET

; =============================================================================
; uint32_t get_cr4(void)
;
; Returns the value of control register CR4.
; =============================================================================
_get_cr4:
    MOV     EAX, CR4
    RET

; =============================================================================
; void set_cr0(uint32_t val)
;
; Sets control register CR0 to the given value.
;
; Stack: [EBP+8]=val
; =============================================================================
_set_cr0:
    PUSH    EBP
    MOV     EBP, ESP

    MOV     EAX, [EBP + 8]
    MOV     CR0, EAX

    POP     EBP
    RET

; =============================================================================
; void set_cr3(uint32_t val)
;
; Sets control register CR3 (page directory base) to the given value.
; This does NOT flush the TLB unless the value is the same as current CR3.
; To flush TLB, set a different value or use INVLPG.
;
; Stack: [EBP+8]=val
; =============================================================================
_set_cr3:
    PUSH    EBP
    MOV     EBP, ESP

    MOV     EAX, [EBP + 8]
    MOV     CR3, EAX

    POP     EBP
    RET

; =============================================================================
; uint32_t get_eflags(void)
;
; Returns the current value of the EFLAGS register.
; Uses PUSHFD / POP EAX.
; =============================================================================
_get_eflags:
    PUSHFD
    POP     EAX
    RET