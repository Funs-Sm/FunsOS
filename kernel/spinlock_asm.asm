; =============================================================================
; kernel/spinlock_asm.asm - Spinlock Primitives for FunsOS
;
; Provides low-level spinlock implementations using atomic operations.
; Variants: simple spinlock, ticket lock, read/write lock.
;
; All functions follow cdecl calling convention.
; The lock is a uint32_t (or uint32_t[2] for ticket locks).
; =============================================================================

[BITS 32]

[SECTION .text]

[GLOBAL spin_lock_asm]
[GLOBAL spin_unlock_asm]
[GLOBAL spin_trylock_asm]
[GLOBAL read_lock_asm]
[GLOBAL write_lock_asm]
[GLOBAL ticket_lock_asm]
[GLOBAL ticket_unlock_asm]

; =============================================================================
; void spin_lock_asm(uint32_t* lock)
;
; Acquires a spinlock. Busy-waits with PAUSE and exponential backoff.
; Uses LOCK BTS (Bit Test and Set) for atomic test-and-set.
;
; lock: 0 = unlocked, 1 = locked
;
; Exponential backoff algorithm:
;   - Start with 1 PAUSE iteration
;   - Double the wait on each failed attempt
;   - Cap at 4096 iterations
;
; Stack: [EBP+8]=lock
; =============================================================================
spin_lock_asm:
    PUSH    EBP
    MOV     EBP, ESP
    PUSH    EBX

    MOV     EBX, [EBP + 8]      ; EBX = lock pointer

    TEST    EBX, EBX
    JZ      .done

    ; Exponential backoff counter
    MOV     ECX, 1              ; ECX = initial backoff count

.try_lock:
    ; Atomically test and set bit 0 of the lock
    LOCK BTS DWORD [EBX], 0
    JNC     .locked             ; If CF=0, we acquired the lock

    ; Lock not acquired - spin with backoff
    ; Use PAUSE to reduce power consumption and avoid memory ordering issues
    PUSH    ECX                 ; Save backoff count
    MOV     EDX, ECX

.spin_loop:
    PAUSE
    DEC     EDX
    JNZ     .spin_loop

    POP     ECX

    ; Exponential backoff: double the wait, cap at 4096
    SHL     ECX, 1
    CMP     ECX, 4096
    JBE     .try_lock
    MOV     ECX, 4096
    JMP     .try_lock

.locked:
    ; Memory barrier to ensure lock acquisition is visible
    ; before any subsequent loads/stores in the critical section
    ; (LOCK BTS already provides a full barrier on x86)

.done:
    POP     EBX
    POP     EBP
    RET

; =============================================================================
; void spin_unlock_asm(uint32_t* lock)
;
; Releases a spinlock. Uses a simple MOV (which is atomic for aligned
; DWORD writes on x86). The store is implicitly ordered.
;
; Stack: [EBP+8]=lock
; =============================================================================
spin_unlock_asm:
    PUSH    EBP
    MOV     EBP, ESP

    MOV     EAX, [EBP + 8]      ; EAX = lock pointer

    TEST    EAX, EAX
    JZ      .done

    ; Release lock: store 0
    ; On x86, aligned DWORD stores are atomic. A store-release
    ; barrier is implicit in the store buffer ordering.
    MOV     DWORD [EAX], 0

.done:
    POP     EBP
    RET

; =============================================================================
; int spin_trylock_asm(uint32_t* lock)
;
; Attempts to acquire a spinlock without waiting.
; Returns 1 if the lock was acquired, 0 otherwise.
;
; Stack: [EBP+8]=lock
; =============================================================================
spin_trylock_asm:
    PUSH    EBP
    MOV     EBP, ESP

    MOV     ECX, [EBP + 8]      ; ECX = lock pointer

    TEST    ECX, ECX
    JZ      .fail

    ; Try to atomically set bit 0
    LOCK BTS DWORD [ECX], 0
    JNC     .success

.fail:
    XOR     EAX, EAX            ; return 0
    JMP     .done

.success:
    MOV     EAX, 1              ; return 1

.done:
    POP     EBP
    RET

; =============================================================================
; void read_lock_asm(uint32_t* lock)
;
; Acquires a reader lock. Readers can share the lock.
; The lock structure:
;   bit 0:     writer flag (1 = writer active)
;   bits 1-31: reader count
;
; Waits if a writer holds the lock, then atomically increments
; the reader count.
;
; Stack: [EBP+8]=lock
; =============================================================================
read_lock_asm:
    PUSH    EBP
    MOV     EBP, ESP
    PUSH    EBX
    PUSH    ESI

    MOV     EBX, [EBP + 8]      ; EBX = lock pointer

    TEST    EBX, EBX
    JZ      .done

.try_read_lock:
    ; Read current lock value
    MOV     EAX, [EBX]

    ; Check if writer is active (bit 0 set)
    TEST    EAX, 1
    JNZ     .spin_read_lock     ; Writer active, must wait

    ; Try to atomically increment reader count
    ; CAS: if [EBX] == EAX, then [EBX] = EAX + 2 (add one reader)
    LEA     EDX, [EAX + 2]      ; EDX = new value (reader count + 1)
    LOCK CMPXCHG [EBX], EDX
    JNZ     .try_read_lock      ; CAS failed, retry

    JMP     .done

.spin_read_lock:
    ; Spin with PAUSE until writer releases
    PAUSE
    MOV     EAX, [EBX]
    TEST    EAX, 1
    JNZ     .spin_read_lock
    JMP     .try_read_lock

.done:
    POP     ESI
    POP     EBX
    POP     EBP
    RET

; =============================================================================
; void write_lock_asm(uint32_t* lock)
;
; Acquires a writer lock (exclusive). Waits until no readers and no
; other writer hold the lock, then sets the writer flag.
;
; Stack: [EBP+8]=lock
; =============================================================================
write_lock_asm:
    PUSH    EBP
    MOV     EBP, ESP
    PUSH    EBX

    MOV     EBX, [EBP + 8]      ; EBX = lock pointer

    TEST    EBX, EBX
    JZ      .done

    ; Exponential backoff for write lock
    MOV     ECX, 1

.try_write_lock:
    ; Try to acquire write lock: lock must be 0
    MOV     EAX, 0
    MOV     EDX, 1              ; Set writer flag
    LOCK CMPXCHG [EBX], EDX
    JZ      .done               ; Acquired!

    ; Not acquired - spin with backoff
    PUSH    ECX
    MOV     EDX, ECX
.write_spin:
    PAUSE
    DEC     EDX
    JNZ     .write_spin
    POP     ECX

    SHL     ECX, 1
    CMP     ECX, 4096
    JBE     .try_write_lock
    MOV     ECX, 4096
    JMP     .try_write_lock

.done:
    POP     EBX
    POP     EBP
    RET

; =============================================================================
; void ticket_lock_asm(uint32_t lock[2])
;
; Fair ticket-based spinlock.
; lock[0] = next_ticket (incremented by each arriving thread)
; lock[1] = now_serving (incremented when lock is released)
;
; Algorithm:
;   1. Atomically fetch and increment next_ticket to get our ticket
;   2. Spin until now_serving == our ticket
;
; Stack: [EBP+8]=lock (pointer to uint32_t[2])
; =============================================================================
ticket_lock_asm:
    PUSH    EBP
    MOV     EBP, ESP
    PUSH    EBX
    PUSH    ESI

    MOV     EBX, [EBP + 8]      ; EBX = lock[0] address

    TEST    EBX, EBX
    JZ      .done

    ; Atomically fetch and increment next_ticket
    ; LOCK XADD: EAX = old [EBX], [EBX] = old + 1
    MOV     EAX, 1
    LOCK XADD [EBX], EAX        ; EAX = our ticket

    ; Now spin until now_serving == our ticket
    ; now_serving is at lock[1] = EBX + 4
    LEA     ESI, [EBX + 4]      ; ESI = &lock[1] (now_serving)

.wait_turn:
    PAUSE
    CMP     [ESI], EAX          ; Compare now_serving with our ticket
    JNE     .wait_turn

    ; Memory barrier: lock acquisition acts as acquire barrier
    ; LOCK XADD already provided a full barrier

.done:
    POP     ESI
    POP     EBX
    POP     EBP
    RET

; =============================================================================
; void ticket_unlock_asm(uint32_t lock[2])
;
; Releases a ticket-based spinlock.
; Atomically increments now_serving.
;
; Stack: [EBP+8]=lock (pointer to uint32_t[2])
; =============================================================================
ticket_unlock_asm:
    PUSH    EBP
    MOV     EBP, ESP

    MOV     EAX, [EBP + 8]      ; EAX = lock[0] address

    TEST    EAX, EAX
    JZ      .done

    ; Increment now_serving (lock[1])
    ; LOCK INC is sufficient for release semantics
    LOCK INC DWORD [EAX + 4]

.done:
    POP     EBP
    RET