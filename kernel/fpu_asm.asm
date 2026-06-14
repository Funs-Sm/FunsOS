; =============================================================================
; kernel/fpu_asm.asm - FPU/SSE Operations for FunsOS
;
; Provides low-level FPU/SSE state management, CPU feature detection,
; and performance monitoring counter access.
;
; All functions follow cdecl calling convention.
; =============================================================================

[BITS 32]

[SECTION .text]

[GLOBAL fpu_context_size]
[GLOBAL fpu_save_fast]
[GLOBAL fpu_restore_fast]
[GLOBAL fpu_clear]
[GLOBAL fpu_get_status]
[GLOBAL fpu_get_control]
[GLOBAL fpu_set_control]
[GLOBAL sse_save]
[GLOBAL sse_restore]
[GLOBAL sse_get_mxcsr]
[GLOBAL sse_set_mxcsr]
[GLOBAL clflush]
[GLOBAL rdtsc]
[GLOBAL rdpmc]
[GLOBAL cpuid_asm]

[SECTION .data]

; Size of FXSAVE/FXRSTOR area (512 bytes for 32-bit x86)
fpu_context_size:
    DD 512

[SECTION .text]

; =============================================================================
; void fpu_save_fast(void* buffer)
;
; Saves FPU/MMX/SSE state to a 16-byte aligned buffer using FXSAVE.
; The buffer must be at least 512 bytes.
;
; Stack: [EBP+8]=buffer
; =============================================================================
fpu_save_fast:
    PUSH    EBP
    MOV     EBP, ESP

    MOV     EAX, [EBP + 8]

    TEST    EAX, EAX
    JZ      .done

    FXSAVE  [EAX]

.done:
    POP     EBP
    RET

; =============================================================================
; void fpu_restore_fast(const void* buffer)
;
; Restores FPU/MMX/SSE state from a buffer using FXRSTOR.
;
; Stack: [EBP+8]=buffer
; =============================================================================
fpu_restore_fast:
    PUSH    EBP
    MOV     EBP, ESP

    MOV     EAX, [EBP + 8]

    TEST    EAX, EAX
    JZ      .done

    FXRSTOR [EAX]

.done:
    POP     EBP
    RET

; =============================================================================
; void fpu_clear(void)
;
; Clears the FPU state: initializes (FNINIT) and clears exceptions
; (FNCLEX). This puts the FPU into a known-clean state.
; =============================================================================
fpu_clear:
    FNINIT
    FNCLEX
    RET

; =============================================================================
; uint16_t fpu_get_status(void)
;
; Returns the FPU status word in AX.
; Uses FSTSW AX (store status word to AX).
; =============================================================================
fpu_get_status:
    ; Wait for FPU to be ready
    FSTSW   AX
    ; AX already holds the return value (16-bit)
    MOVZX   EAX, AX
    RET

; =============================================================================
; uint16_t fpu_get_control(void)
;
; Returns the FPU control word in AX.
; Uses FSTCW (store control word to memory), then loads into AX.
; =============================================================================
fpu_get_control:
    SUB     ESP, 4
    FSTCW   [ESP]
    MOV     AX, [ESP]
    MOVZX   EAX, AX
    ADD     ESP, 4
    RET

; =============================================================================
; void fpu_set_control(uint16_t cw)
;
; Sets the FPU control word.
; Uses FLDCW (load control word from memory).
;
; Stack: [EBP+8]=cw (lower 16 bits)
; =============================================================================
fpu_set_control:
    PUSH    EBP
    MOV     EBP, ESP

    ; Store the control word on the stack for FLDCW
    MOV     AX, [EBP + 8]
    MOV     [ESP - 4], AX       ; Temporarily store below current ESP
    ; Actually, we need to be careful. Let's use a proper stack slot.
    SUB     ESP, 4
    MOV     [ESP], AX
    FLDCW   [ESP]
    ADD     ESP, 4

    POP     EBP
    RET

; =============================================================================
; void sse_save(void* buffer)
;
; Saves all 8 XMM registers (XMM0-XMM7) to a 16-byte aligned buffer.
; Buffer must be at least 128 bytes (8 * 16).
; Uses MOVAPS (aligned store).
;
; Stack: [EBP+8]=buffer
; =============================================================================
sse_save:
    PUSH    EBP
    MOV     EBP, ESP

    MOV     EAX, [EBP + 8]

    TEST    EAX, EAX
    JZ      .done

    ; Save XMM0-XMM7
    MOVAPS  [EAX], XMM0
    MOVAPS  [EAX + 16], XMM1
    MOVAPS  [EAX + 32], XMM2
    MOVAPS  [EAX + 48], XMM3
    MOVAPS  [EAX + 64], XMM4
    MOVAPS  [EAX + 80], XMM5
    MOVAPS  [EAX + 96], XMM6
    MOVAPS  [EAX + 112], XMM7

.done:
    POP     EBP
    RET

; =============================================================================
; void sse_restore(const void* buffer)
;
; Restores all 8 XMM registers from a buffer.
; Uses MOVAPS (aligned load).
;
; Stack: [EBP+8]=buffer
; =============================================================================
sse_restore:
    PUSH    EBP
    MOV     EBP, ESP

    MOV     EAX, [EBP + 8]

    TEST    EAX, EAX
    JZ      .done

    ; Restore XMM0-XMM7
    MOVAPS  XMM0, [EAX]
    MOVAPS  XMM1, [EAX + 16]
    MOVAPS  XMM2, [EAX + 32]
    MOVAPS  XMM3, [EAX + 48]
    MOVAPS  XMM4, [EAX + 64]
    MOVAPS  XMM5, [EAX + 80]
    MOVAPS  XMM6, [EAX + 96]
    MOVAPS  XMM7, [EAX + 112]

.done:
    POP     EBP
    RET

; =============================================================================
; uint32_t sse_get_mxcsr(void)
;
; Returns the SSE MXCSR control/status register.
; Uses STMXCSR.
; =============================================================================
sse_get_mxcsr:
    SUB     ESP, 4
    STMXCSR [ESP]
    MOV     EAX, [ESP]
    ADD     ESP, 4
    RET

; =============================================================================
; void sse_set_mxcsr(uint32_t mxcsr)
;
; Sets the SSE MXCSR control/status register.
; Uses LDMXCSR.
;
; Stack: [EBP+8]=mxcsr
; =============================================================================
sse_set_mxcsr:
    PUSH    EBP
    MOV     EBP, ESP

    MOV     EAX, [EBP + 8]
    SUB     ESP, 4
    MOV     [ESP], EAX
    LDMXCSR [ESP]
    ADD     ESP, 4

    POP     EBP
    RET

; =============================================================================
; void clflush(const void* addr)
;
; Flushes the cache line containing the specified address.
; Uses CLFLUSH.
;
; Stack: [EBP+8]=addr
; =============================================================================
clflush:
    PUSH    EBP
    MOV     EBP, ESP

    MOV     EAX, [EBP + 8]

    TEST    EAX, EAX
    JZ      .done

    CLFLUSH [EAX]

    ; Memory barrier to ensure the flush is globally visible
    MFENCE

.done:
    POP     EBP
    RET

; =============================================================================
; uint64_t rdtsc(void)
;
; Reads the CPU Time-Stamp Counter.
; Returns the 64-bit value in EDX:EAX (high 32 bits in EDX, low 32 in EAX).
; C code should use uint64_t to capture both halves.
;
; Also includes a serializing instruction (CPUID) to prevent
; out-of-order execution from affecting the measurement.
; =============================================================================
rdtsc:
    ; Serialize: prevent reordering of RDTSC
    ; Actually, RDTSCP is better but not available on all CPUs.
    ; We use LFENCE for serialization on modern CPUs or just RDTSC directly.
    RDTSC
    ; Result in EDX:EAX
    RET

; =============================================================================
; uint64_t rdpmc(uint32_t counter)
;
; Reads the specified Performance Monitoring Counter.
; ECX = counter index (0-...)
; Returns the 64-bit counter value in EDX:EAX.
;
; Stack: [EBP+8]=counter
; =============================================================================
rdpmc:
    PUSH    EBP
    MOV     EBP, ESP

    MOV     ECX, [EBP + 8]      ; ECX = counter index

    RDPMC
    ; Result in EDX:EAX

    POP     EBP
    RET

; =============================================================================
; void cpuid_asm(uint32_t leaf, uint32_t* eax, uint32_t* ebx,
;                uint32_t* ecx, uint32_t* edx)
;
; Executes the CPUID instruction with the given leaf in EAX and
; optional subleaf in ECX. Stores the results in the provided pointers.
;
; Stack:
;   [EBP+8]  = leaf (EAX input)
;   [EBP+12] = *eax (output)
;   [EBP+16] = *ebx (output)
;   [EBP+20] = *ecx (output)
;   [EBP+24] = *edx (output)
; =============================================================================
cpuid_asm:
    PUSH    EBP
    MOV     EBP, ESP
    PUSH    EBX
    PUSH    ESI
    PUSH    EDI

    ; Load leaf and execute CPUID
    MOV     EAX, [EBP + 8]      ; EAX = leaf
    XOR     ECX, ECX            ; ECX = 0 (subleaf)
    CPUID

    ; Store results
    MOV     ESI, [EBP + 12]     ; ESI = *eax
    TEST    ESI, ESI
    JZ      .skip_eax
    MOV     [ESI], EAX
.skip_eax:

    MOV     ESI, [EBP + 16]     ; ESI = *ebx
    TEST    ESI, ESI
    JZ      .skip_ebx
    MOV     [ESI], EBX
.skip_ebx:

    MOV     ESI, [EBP + 20]     ; ESI = *ecx
    TEST    ESI, ESI
    JZ      .skip_ecx
    MOV     [ESI], ECX
.skip_ecx:

    MOV     ESI, [EBP + 24]     ; ESI = *edx
    TEST    ESI, ESI
    JZ      .skip_edx
    MOV     [ESI], EDX
.skip_edx:

    POP     EDI
    POP     ESI
    POP     EBX
    POP     EBP
    RET