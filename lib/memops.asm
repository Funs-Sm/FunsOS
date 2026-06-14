; =============================================================================
; lib/memops.asm - Optimized Memory Operations for FunsOS
;
; Provides optimized implementations of standard memory operations using
; SSE2 (MOVDQA), DWORD (MOVSD), and BYTE (MOVSB) for best performance.
;
; All functions follow cdecl calling convention (caller cleans stack).
; Parameters are passed on the stack, return value in EAX.
; Preserves: EBX, ESI, EDI, EBP, ESP
; =============================================================================

[BITS 32]

[SECTION .text]

; ---- Exported symbols ----
[GLOBAL memcpy_asm]
[GLOBAL memset_asm]
[GLOBAL memcmp_asm]
[GLOBAL memmove_asm]
[GLOBAL bzero_asm]

; =============================================================================
; void* memcpy_asm(void* dest, const void* src, size_t n)
;
; Copies n bytes from src to dest. Returns dest.
; Path: SSE2 MOVDQA (aligned) / MOVDQU (unaligned) for 16-byte chunks,
; then MOVSD for 4-byte chunks, then MOVSB for remaining bytes.
;
; Stack: [EBP+8]=dest, [EBP+12]=src, [EBP+16]=n
; =============================================================================
memcpy_asm:
    PUSH    EBP
    MOV     EBP, ESP
    PUSH    ESI
    PUSH    EDI
    PUSH    EBX

    MOV     EDI, [EBP + 8]      ; EDI = dest
    MOV     ESI, [EBP + 12]     ; ESI = src
    MOV     ECX, [EBP + 16]     ; ECX = n
    MOV     EAX, EDI            ; return value = dest

    TEST    ECX, ECX
    JZ      .done

    ; Small copy: n < 64 -> skip SSE overhead
    CMP     ECX, 64
    JB      .copy_dword

    ; Check if both src and dest are 16-byte aligned
    MOV     EDX, EDI
    OR      EDX, ESI
    TEST    EDX, 0x0F
    JNZ     .copy_unaligned

    ; ---- Aligned SSE2 copy (MOVDQA) ----
    MOV     EDX, ECX
    SHR     EDX, 4              ; EDX = n / 16
    JZ      .copy_dword_rem

.aligned_sse_loop:
    MOVDQA  XMM0, [ESI]
    MOVDQA  [EDI], XMM0
    ADD     ESI, 16
    ADD     EDI, 16
    DEC     EDX
    JNZ     .aligned_sse_loop

    AND     ECX, 0x0F           ; remainder
    JZ      .done
    JMP     .copy_dword_rem

    ; ---- Unaligned copy: align dest first, then MOVDQU ----
.copy_unaligned:
    MOV     EBX, EDI
    AND     EBX, 0x0F
    JZ      .unaligned_sse      ; already aligned
    ; bytes_to_align = 16 - (EDI & 0xF)
    NEG     EBX
    ADD     EBX, 16
    CMP     EBX, ECX
    JBE     .align_head
    MOV     EBX, ECX
.align_head:
    SUB     ECX, EBX
    XCHG    ECX, EBX
    REP MOVSB
    MOV     ECX, EBX
    TEST    ECX, ECX
    JZ      .done

.unaligned_sse:
    CMP     ECX, 16
    JB      .copy_dword_rem

    MOV     EDX, ECX
    SHR     EDX, 4
.unaligned_sse_loop:
    MOVDQU  XMM0, [ESI]
    MOVDQU  [EDI], XMM0
    ADD     ESI, 16
    ADD     EDI, 16
    DEC     EDX
    JNZ     .unaligned_sse_loop

    AND     ECX, 0x0F
    JZ      .done

    ; ---- DWORD copy (4-byte chunks) ----
.copy_dword_rem:
.copy_dword:
    CMP     ECX, 4
    JB      .copy_byte

    MOV     EDX, ECX
    SHR     EDX, 2
    MOV     EBX, ECX
    AND     EBX, 0x03
    MOV     ECX, EDX
    REP MOVSD
    MOV     ECX, EBX
    TEST    ECX, ECX
    JZ      .done

    ; ---- Byte copy (remaining) ----
.copy_byte:
    REP MOVSB

.done:
    POP     EBX
    POP     EDI
    POP     ESI
    POP     EBP
    RET

; =============================================================================
; void* memset_asm(void* dest, int val, size_t n)
;
; Sets n bytes at dest to val (low byte). Returns dest.
; Uses SSE2 MOVDQA for aligned 16-byte fill, STOSD for 4-byte, STOSB for byte.
;
; Stack: [EBP+8]=dest, [EBP+12]=val, [EBP+16]=n
; =============================================================================
memset_asm:
    PUSH    EBP
    MOV     EBP, ESP
    PUSH    EDI
    PUSH    EBX

    MOV     EDI, [EBP + 8]      ; EDI = dest
    MOVZX   EAX, BYTE [EBP + 12] ; AL = fill byte
    MOV     ECX, [EBP + 16]     ; ECX = n
    MOV     EDX, EDI            ; return value = dest

    TEST    ECX, ECX
    JZ      .done

    ; Small fill: n < 64 -> skip SSE overhead
    CMP     ECX, 64
    JB      .fill_dword

    ; ---- Build 16-byte pattern in XMM0 ----
    ; Broadcast AL across all 16 bytes of XMM0
    MOV     AH, AL
    MOV     BX, AX
    SHL     EBX, 16
    MOV     BX, AX              ; EBX = AL repeated 4 times
    MOVD    XMM0, EBX
    PUNPCKLBW XMM0, XMM0
    PUNPCKLDQ XMM0, XMM0        ; XMM0 = AL * 16

    ; Align destination to 16-byte boundary
    TEST    EDI, 0x0F
    JZ      .fill_sse_aligned

    MOV     EBX, EDI
    AND     EBX, 0x0F
    NEG     EBX
    ADD     EBX, 16             ; bytes to align
    CMP     EBX, ECX
    JBE     .fill_align
    MOV     EBX, ECX
.fill_align:
    SUB     ECX, EBX
    XCHG    ECX, EBX
    REP STOSB
    MOV     ECX, EBX
    TEST    ECX, ECX
    JZ      .done

.fill_sse_aligned:
    ; Fill 16-byte aligned chunks
    MOV     EBX, ECX
    SHR     EBX, 4
    JZ      .fill_dword

.fill_sse_loop:
    MOVDQA  [EDI], XMM0
    ADD     EDI, 16
    DEC     EBX
    JNZ     .fill_sse_loop

    AND     ECX, 0x0F
    JZ      .done

    ; ---- DWORD fill (4-byte chunks) ----
.fill_dword:
    CMP     ECX, 4
    JB      .fill_byte

    ; Build 4-byte pattern in EAX for STOSD
    MOV     AH, AL
    MOV     BX, AX
    SHL     EBX, 16
    MOV     BX, AX              ; EBX = AL repeated 4 times
    MOV     EAX, EBX

    MOV     EBX, ECX
    SHR     EBX, 2
    MOV     EDX, ECX
    AND     EDX, 0x03
    MOV     ECX, EBX
    REP STOSD
    MOV     ECX, EDX
    TEST    ECX, ECX
    JZ      .done

    ; ---- Byte fill (remaining) ----
.fill_byte:
    REP STOSB

.done:
    MOV     EAX, [EBP + 8]      ; return dest
    POP     EBX
    POP     EDI
    POP     EBP
    RET

; =============================================================================
; int memcmp_asm(const void* s1, const void* s2, size_t n)
;
; Compares n bytes. Returns <0, 0, or >0.
; Uses REPE CMPSD for DWORD-aligned regions, then REPE CMPSB.
;
; Stack: [EBP+8]=s1, [EBP+12]=s2, [EBP+16]=n
; =============================================================================
memcmp_asm:
    PUSH    EBP
    MOV     EBP, ESP
    PUSH    ESI
    PUSH    EDI

    MOV     ESI, [EBP + 8]      ; ESI = s1
    MOV     EDI, [EBP + 12]     ; EDI = s2
    MOV     ECX, [EBP + 16]     ; ECX = n

    TEST    ECX, ECX
    JZ      .return_zero

    ; Fast path: if both DWORD-aligned, compare DWORDs first
    MOV     EAX, ESI
    OR      EAX, EDI
    TEST    EAX, 0x03
    JNZ     .byte_compare

    MOV     EDX, ECX
    SHR     EDX, 2              ; n / 4
    JZ      .byte_compare

    PUSH    ECX
    MOV     ECX, EDX
    REPE CMPSD
    JNE     .dword_mismatch

    POP     ECX
    AND     ECX, 0x03
    JZ      .return_zero

    REPE CMPSB
    JNE     .byte_mismatch
    JMP     .return_zero

.dword_mismatch:
    ; ESI/EDI point 4 bytes past the mismatched DWORD
    SUB     ESI, 4
    SUB     EDI, 4
    MOV     EAX, [ESI]
    MOV     EDX, [EDI]

    CMP     AL, DL
    JNE     .calc_diff
    CMP     AH, DH
    JNE     .calc_diff_ah
    SHR     EAX, 16
    SHR     EDX, 16
    CMP     AL, DL
    JNE     .calc_diff
    ; Must be the 4th byte (AH vs DH after shift)
    MOVZX   EAX, AH
    MOVZX   EDX, DH
    SUB     EAX, EDX
    ADD     ESP, 4
    JMP     .done

.calc_diff_ah:
    MOVZX   EAX, AH
    MOVZX   EDX, DH
    SUB     EAX, EDX
    ADD     ESP, 4
    JMP     .done

.calc_diff:
    MOVZX   EAX, AL
    MOVZX   EDX, DL
    SUB     EAX, EDX
    ADD     ESP, 4
    JMP     .done

.byte_mismatch:
    MOVZX   EAX, BYTE [ESI - 1]
    MOVZX   EDX, BYTE [EDI - 1]
    SUB     EAX, EDX
    JMP     .done

.byte_compare:
    REPE CMPSB
    JNE     .byte_mismatch

.return_zero:
    XOR     EAX, EAX

.done:
    POP     EDI
    POP     ESI
    POP     EBP
    RET

; =============================================================================
; void* memmove_asm(void* dest, const void* src, size_t n)
;
; Safe copy handling overlapping regions. Returns dest.
; If dest < src: forward copy (safe)
; If dest > src and dest < src + n: backward copy (overlap)
; Otherwise: forward copy
;
; Stack: [EBP+8]=dest, [EBP+12]=src, [EBP+16]=n
; =============================================================================
memmove_asm:
    PUSH    EBP
    MOV     EBP, ESP
    PUSH    ESI
    PUSH    EDI

    MOV     EDI, [EBP + 8]      ; EDI = dest
    MOV     ESI, [EBP + 12]     ; ESI = src
    MOV     ECX, [EBP + 16]     ; ECX = n
    MOV     EAX, EDI            ; return value

    TEST    ECX, ECX
    JZ      .done

    CMP     EDI, ESI
    JE      .done               ; same pointer
    JB      .forward            ; dest < src: forward safe

    ; dest > src: check for overlap
    MOV     EDX, ESI
    ADD     EDX, ECX
    CMP     EDI, EDX
    JAE     .forward            ; dest >= src + n: no overlap

    ; ---- Backward copy (overlap: dest in (src, src+n)) ----
.backward:
    STD
    ADD     ESI, ECX
    DEC     ESI                 ; ESI = &src[n-1]
    ADD     EDI, ECX
    DEC     EDI                 ; EDI = &dest[n-1]

    CMP     ECX, 64
    JB      .backward_small

    ; Copy trailing bytes to align to DWORD at end
    MOV     EDX, EDI
    INC     EDX
    AND     EDX, 0x03
    JZ      .backward_dword

    SUB     ECX, EDX
    REP MOVSB

.backward_dword:
    MOV     EBX, ECX
    SHR     EBX, 2
    JZ      .backward_byte

    ; Adjust: ESI and EDI currently point to last byte.
    ; For backward MOVSD, they need to point to 3 bytes before the
    ; last DWORD start (since MOVSD moves 4 bytes and decrements by 4).
    SUB     ESI, 3
    SUB     EDI, 3
    MOV     ECX, EBX
    REP MOVSD
    ADD     ESI, 3
    ADD     EDI, 3

    MOV     ECX, [EBP + 16]
    AND     ECX, 0x03

.backward_byte:
    REP MOVSB
    CLD
    JMP     .done

.backward_small:
    REP MOVSB
    CLD
    JMP     .done

    ; ---- Forward copy (safe) ----
.forward:
    ; Use same logic as memcpy_asm
    CMP     ECX, 64
    JB      .forward_dword

    ; Align dest to 16 bytes
    MOV     EDX, EDI
    AND     EDX, 0x0F
    JZ      .forward_sse

    MOV     EBX, 16
    SUB     EBX, EDX
    CMP     EBX, ECX
    JBE     .forward_align
    MOV     EBX, ECX
.forward_align:
    SUB     ECX, EBX
    XCHG    ECX, EBX
    REP MOVSB
    MOV     ECX, EBX
    TEST    ECX, ECX
    JZ      .done

.forward_sse:
    CMP     ECX, 16
    JB      .forward_dword

    MOV     EBX, ECX
    SHR     EBX, 4
.forward_sse_loop:
    MOVDQU  XMM0, [ESI]
    MOVDQU  [EDI], XMM0
    ADD     ESI, 16
    ADD     EDI, 16
    DEC     EBX
    JNZ     .forward_sse_loop

    AND     ECX, 0x0F
    JZ      .done

.forward_dword:
    CMP     ECX, 4
    JB      .forward_byte

    MOV     EBX, ECX
    SHR     EBX, 2
    MOV     EDX, ECX
    AND     EDX, 0x03
    MOV     ECX, EBX
    REP MOVSD
    MOV     ECX, EDX
    TEST    ECX, ECX
    JZ      .done

.forward_byte:
    REP MOVSB

.done:
    POP     EDI
    POP     ESI
    POP     EBP
    RET

; =============================================================================
; void bzero_asm(void* dest, size_t n)
;
; Zeros a memory region. Calls memset_asm(dest, 0, n).
;
; Stack: [EBP+8]=dest, [EBP+12]=n
; =============================================================================
bzero_asm:
    PUSH    EBP
    MOV     EBP, ESP

    PUSH    DWORD [EBP + 12]    ; n
    PUSH    DWORD 0             ; val = 0
    PUSH    DWORD [EBP + 8]     ; dest
    CALL    memset_asm
    ADD     ESP, 12

    POP     EBP
    RET