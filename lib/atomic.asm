; =============================================================================
; lib/atomic.asm - Atomic Operations for FunsOS
;
; Provides lock-free atomic operations for synchronization primitives.
; All functions use LOCK prefix for SMP safety.
; Functions follow cdecl calling convention.
;
; Each function takes a pointer to the target memory location and
; (where applicable) a value operand. Returns the old value.
; =============================================================================

[BITS 32]

[SECTION .text]

[GLOBAL atomic_add]
[GLOBAL atomic_sub]
[GLOBAL atomic_inc]
[GLOBAL atomic_dec]
[GLOBAL atomic_xchg]
[GLOBAL atomic_cmpxchg]
[GLOBAL atomic_test_and_set]
[GLOBAL atomic_test_and_clear]
[GLOBAL atomic_or]
[GLOBAL atomic_and]
[GLOBAL atomic_xor]
[GLOBAL memory_barrier]
[GLOBAL memory_barrier_load]
[GLOBAL memory_barrier_store]

; =============================================================================
; int atomic_add(int* ptr, int val)
;
; Atomically adds val to *ptr. Returns the OLD value at *ptr.
; Uses LOCK XADD.
;
; Stack: [EBP+8]=ptr, [EBP+12]=val
; =============================================================================
atomic_add:
    PUSH    EBP
    MOV     EBP, ESP

    MOV     ECX, [EBP + 8]      ; ECX = ptr
    MOV     EAX, [EBP + 12]     ; EAX = val

    ; NULL pointer check
    TEST    ECX, ECX
    JZ      .done

    LOCK XADD [ECX], EAX        ; Exchange [ECX] with [ECX]+EAX, EAX = old [ECX]

.done:
    POP     EBP
    RET

; =============================================================================
; int atomic_sub(int* ptr, int val)
;
; Atomically subtracts val from *ptr. Returns the OLD value at *ptr.
; Uses LOCK XADD with negation.
;
; Stack: [EBP+8]=ptr, [EBP+12]=val
; =============================================================================
atomic_sub:
    PUSH    EBP
    MOV     EBP, ESP

    MOV     ECX, [EBP + 8]      ; ECX = ptr
    MOV     EAX, [EBP + 12]     ; EAX = val

    TEST    ECX, ECX
    JZ      .done

    NEG     EAX                 ; EAX = -val
    LOCK XADD [ECX], EAX        ; EAX = old [ECX], [ECX] = old - val

.done:
    POP     EBP
    RET

; =============================================================================
; int atomic_inc(int* ptr)
;
; Atomically increments *ptr by 1. Returns the NEW value.
; Uses LOCK INC.
;
; Stack: [EBP+8]=ptr
; =============================================================================
atomic_inc:
    PUSH    EBP
    MOV     EBP, ESP

    MOV     ECX, [EBP + 8]      ; ECX = ptr

    TEST    ECX, ECX
    JZ      .done

    LOCK INC DWORD [ECX]        ; Increment *ptr
    MOV     EAX, [ECX]          ; Return new value (locked INC sets flags but
                                ; we need the value, so read it back)

.done:
    POP     EBP
    RET

; =============================================================================
; int atomic_dec(int* ptr)
;
; Atomically decrements *ptr by 1. Returns the NEW value.
; Uses LOCK DEC.
;
; Stack: [EBP+8]=ptr
; =============================================================================
atomic_dec:
    PUSH    EBP
    MOV     EBP, ESP

    MOV     ECX, [EBP + 8]      ; ECX = ptr

    TEST    ECX, ECX
    JZ      .done

    LOCK DEC DWORD [ECX]        ; Decrement *ptr
    MOV     EAX, [ECX]          ; Return new value

.done:
    POP     EBP
    RET

; =============================================================================
; int atomic_xchg(int* ptr, int val)
;
; Atomically exchanges *ptr with val. Returns the OLD value at *ptr.
; Uses LOCK XCHG.
;
; Stack: [EBP+8]=ptr, [EBP+12]=val
; =============================================================================
atomic_xchg:
    PUSH    EBP
    MOV     EBP, ESP

    MOV     ECX, [EBP + 8]      ; ECX = ptr
    MOV     EAX, [EBP + 12]     ; EAX = val

    TEST    ECX, ECX
    JZ      .done

    XCHG [ECX], EAX            ; Exchange: EAX = old [ECX], [ECX] = val
                                ; (XCHG is implicitly atomic, no LOCK needed)

.done:
    POP     EBP
    RET

; =============================================================================
; int atomic_cmpxchg(int* ptr, int old_val, int new_val)
;
; Atomically compares *ptr with old_val. If equal, stores new_val.
; Returns the ORIGINAL value at *ptr (before the operation).
; Uses LOCK CMPXCHG.
;
; Stack: [EBP+8]=ptr, [EBP+12]=old_val, [EBP+16]=new_val
; =============================================================================
atomic_cmpxchg:
    PUSH    EBP
    MOV     EBP, ESP

    MOV     ECX, [EBP + 8]      ; ECX = ptr
    MOV     EAX, [EBP + 12]     ; EAX = old_val (expected)
    MOV     EDX, [EBP + 16]     ; EDX = new_val

    TEST    ECX, ECX
    JZ      .done

    ; CMPXCHG: if [ECX] == EAX, then [ECX] = EDX, else EAX = [ECX]
    LOCK CMPXCHG [ECX], EDX
    ; EAX now holds the original value at [ECX]

.done:
    POP     EBP
    RET

; =============================================================================
; int atomic_test_and_set(int* ptr, int bit)
;
; Atomically sets the specified bit in *ptr to 1.
; Returns the PREVIOUS value of the bit (0 or 1).
; Uses LOCK BTS.
;
; Stack: [EBP+8]=ptr, [EBP+12]=bit
; =============================================================================
atomic_test_and_set:
    PUSH    EBP
    MOV     EBP, ESP

    MOV     ECX, [EBP + 8]      ; ECX = ptr
    MOV     EDX, [EBP + 12]     ; EDX = bit index

    TEST    ECX, ECX
    JZ      .done

    ; BTS: set bit and store old bit in CF
    LOCK BTS [ECX], EDX
    SETC    AL                  ; AL = 1 if old bit was set, 0 otherwise
    MOVZX   EAX, AL

    JMP     .done

.done:
    POP     EBP
    RET

; =============================================================================
; int atomic_test_and_clear(int* ptr, int bit)
;
; Atomically clears the specified bit in *ptr to 0.
; Returns the PREVIOUS value of the bit (0 or 1).
; Uses LOCK BTR.
;
; Stack: [EBP+8]=ptr, [EBP+12]=bit
; =============================================================================
atomic_test_and_clear:
    PUSH    EBP
    MOV     EBP, ESP

    MOV     ECX, [EBP + 8]      ; ECX = ptr
    MOV     EDX, [EBP + 12]     ; EDX = bit index

    TEST    ECX, ECX
    JZ      .done

    ; BTR: clear bit and store old bit in CF
    LOCK BTR [ECX], EDX
    SETC    AL
    MOVZX   EAX, AL

    JMP     .done

.done:
    POP     EBP
    RET

; =============================================================================
; int atomic_or(int* ptr, int val)
;
; Atomically ORs val into *ptr. Returns the OLD value.
; Uses a CAS loop for LOCK OR emulation (since x86 has no LOCK OR).
;
; Stack: [EBP+8]=ptr, [EBP+12]=val
; =============================================================================
atomic_or:
    PUSH    EBP
    MOV     EBP, ESP
    PUSH    EBX

    MOV     ECX, [EBP + 8]      ; ECX = ptr
    MOV     EBX, [EBP + 12]     ; EBX = val

    TEST    ECX, ECX
    JZ      .done

    ; CAS loop: read, OR, try to write
    MOV     EAX, [ECX]          ; EAX = old value
.retry_or:
    MOV     EDX, EAX
    OR      EDX, EBX            ; EDX = new value = old | val
    LOCK CMPXCHG [ECX], EDX     ; if [ECX]==EAX, [ECX]=EDX, else EAX=[ECX]
    JNZ     .retry_or           ; if CAS failed, retry with new old value

.done:
    POP     EBX
    POP     EBP
    RET

; =============================================================================
; int atomic_and(int* ptr, int val)
;
; Atomically ANDs val into *ptr. Returns the OLD value.
; Uses a CAS loop.
;
; Stack: [EBP+8]=ptr, [EBP+12]=val
; =============================================================================
atomic_and:
    PUSH    EBP
    MOV     EBP, ESP
    PUSH    EBX

    MOV     ECX, [EBP + 8]      ; ECX = ptr
    MOV     EBX, [EBP + 12]     ; EBX = val

    TEST    ECX, ECX
    JZ      .done

    ; CAS loop
    MOV     EAX, [ECX]
.retry_and:
    MOV     EDX, EAX
    AND     EDX, EBX
    LOCK CMPXCHG [ECX], EDX
    JNZ     .retry_and

.done:
    POP     EBX
    POP     EBP
    RET

; =============================================================================
; int atomic_xor(int* ptr, int val)
;
; Atomically XORs val into *ptr. Returns the OLD value.
; Uses a CAS loop.
;
; Stack: [EBP+8]=ptr, [EBP+12]=val
; =============================================================================
atomic_xor:
    PUSH    EBP
    MOV     EBP, ESP
    PUSH    EBX

    MOV     ECX, [EBP + 8]      ; ECX = ptr
    MOV     EBX, [EBP + 12]     ; EBX = val

    TEST    ECX, ECX
    JZ      .done

    ; CAS loop
    MOV     EAX, [ECX]
.retry_xor:
    MOV     EDX, EAX
    XOR     EDX, EBX
    LOCK CMPXCHG [ECX], EDX
    JNZ     .retry_xor

.done:
    POP     EBX
    POP     EBP
    RET

; =============================================================================
; void memory_barrier(void)
;
; Full memory barrier: ensures all previous loads and stores are
; globally visible before any subsequent loads or stores.
; Uses MFENCE.
; =============================================================================
memory_barrier:
    MFENCE
    RET

; =============================================================================
; void memory_barrier_load(void)
;
; Load fence: ensures all previous loads are globally visible before
; any subsequent loads. Uses LFENCE.
; =============================================================================
memory_barrier_load:
    LFENCE
    RET

; =============================================================================
; void memory_barrier_store(void)
;
; Store fence: ensures all previous stores are globally visible before
; any subsequent stores. Uses SFENCE.
; =============================================================================
memory_barrier_store:
    SFENCE
    RET