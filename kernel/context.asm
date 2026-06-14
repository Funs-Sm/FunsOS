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

[GLOBAL context_switch]
[GLOBAL context_switch_to]
[GLOBAL fpu_save]
[GLOBAL fpu_restore]
[GLOBAL fpu_init]
[GLOBAL sse_init]
[GLOBAL get_cr0]
[GLOBAL get_cr2]
[GLOBAL get_cr3]
[GLOBAL get_cr4]
[GLOBAL set_cr0]
[GLOBAL set_cr3]
[GLOBAL get_eflags]

; =============================================================================
; void context_switch(uint32_t** old_esp_ptr, uint32_t* new_esp)
;
; Saves the current task context and switches to the new task.
;
; Parameters:
;   [EBP+8]  = old_esp_ptr - pointer to where to save current ESP
;   [EBP+12] = new_esp     - stack pointer of the new task
;
; The context is saved on the current stack. After saving, ESP is stored
; in *old_esp_ptr, then the stack is switched to new_esp and the context
; is restored from there.
; =============================================================================
context_switch:
    ; Save caller-saved registers that C doesn't preserve across calls
    PUSH    EAX
    PUSH    ECX
    PUSH    EDX

    ; At this point, we have a minimal frame on the stack.
    ; Load parameters
    MOV     EAX, [ESP + 16]     ; old_esp_ptr (12 + 4 for the 3 pushes)
    MOV     EDX, [ESP + 20]     ; new_esp

    ; Save current ESP into *old_esp_ptr
    MOV     [EAX], ESP

    ; Switch to new task's stack
    MOV     ESP, EDX

    ; Restore caller-saved registers from new task's stack
    POP     EDX
    POP     ECX
    POP     EAX

    RET

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
context_switch_to:
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
; void fpu_save(void* buffer)
;
; Saves the complete FPU/MMX/SSE state to buffer using FXSAVE.
; The buffer must be 16-byte aligned and at least 512 bytes.
;
; Stack: [EBP+8]=buffer
; =============================================================================
fpu_save:
    PUSH    EBP
    MOV     EBP, ESP

    MOV     EAX, [EBP + 8]      ; EAX = buffer pointer

    ; NULL pointer check
    TEST    EAX, EAX
    JZ      .done

    ; Save FPU/MMX/SSE state
    FXSAVE  [EAX]

.done:
    POP     EBP
    RET

; =============================================================================
; void fpu_restore(const void* buffer)
;
; Restores the complete FPU/MMX/SSE state from buffer using FXRSTOR.
; The buffer must be 16-byte aligned and contain valid FXSAVE data.
;
; Stack: [EBP+8]=buffer
; =============================================================================
fpu_restore:
    PUSH    EBP
    MOV     EBP, ESP

    MOV     EAX, [EBP + 8]      ; EAX = buffer pointer

    TEST    EAX, EAX
    JZ      .done

    ; Restore FPU/MMX/SSE state
    FXRSTOR [EAX]

.done:
    POP     EBP
    RET

; =============================================================================
; void fpu_init(void)
;
; Initializes the FPU to a known state:
; - FNINIT: initialize FPU without checking for pending exceptions
; - Sets control word to default (0x037F):
;   - Round to nearest
;   - 64-bit precision
;   - All exceptions masked
; =============================================================================
fpu_init:
    ; Initialize FPU
    FNINIT

    ; Set control word: 0x037F = round nearest, 64-bit precision, all exceptions masked
    ; Wait for FPU to be ready
    ; FSTCW requires a memory operand
    SUB     ESP, 4
    FSTCW   [ESP]               ; Get current control word (just for delay)
    MOV     WORD [ESP], 0x037F
    FLDCW   [ESP]               ; Load new control word
    ADD     ESP, 4

    ; Clear any pending exceptions
    FNCLEX

    RET

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
sse_init:
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
get_cr0:
    MOV     EAX, CR0
    RET

; =============================================================================
; uint32_t get_cr2(void)
;
; Returns the value of control register CR2 (page fault linear address).
; =============================================================================
get_cr2:
    MOV     EAX, CR2
    RET

; =============================================================================
; uint32_t get_cr3(void)
;
; Returns the value of control register CR3 (page directory base).
; =============================================================================
get_cr3:
    MOV     EAX, CR3
    RET

; =============================================================================
; uint32_t get_cr4(void)
;
; Returns the value of control register CR4.
; =============================================================================
get_cr4:
    MOV     EAX, CR4
    RET

; =============================================================================
; void set_cr0(uint32_t val)
;
; Sets control register CR0 to the given value.
;
; Stack: [EBP+8]=val
; =============================================================================
set_cr0:
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
set_cr3:
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
get_eflags:
    PUSHFD
    POP     EAX
    RET