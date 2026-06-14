; loader.asm - Real-mode loader
;
; Loaded by boot.asm from disk sector 2 onward, into 0x0000:0x1000.
; Runs entirely in 16-bit real mode (we only briefly flip into
; protected mode at the very end to copy and jump to the kernel).
;
; Responsibilities:
;   1. Detect memory map (INT 15h E820 / E801 / 88h)
;   2. Enable the A20 line
;   3. Set a VESA video mode (best effort, fall back to VGA mode 13h)
;   4. Load the kernel image from disk using LBA (INT 13h AH=42h)
;      into low memory 0x10000-0x9FFFF (576 KB)
;   5. Load stage2.bin (a tiny 32-bit loader) to 0x80000 in low memory
;   6. Switch to 32-bit protected mode
;   7. Copy 0x10000-0x9FFFF to 0x100000-0x190000 (kernel dest)
;   8. Far jump to stage2 at 0x80000, which finishes loading the rest
;      of the kernel and jumps into it.

[BITS 16]
[ORG 0x1000]

loader_start:
    CLI
    MOV AX, CS
    MOV DS, AX
    MOV ES, AX
    MOV SS, AX
    MOV SP, 0x2000
    STI

    MOV SI, msg_mem_detect
    CALL print_string_16

    MOV DI, 0x0500 + 12
    XOR BX, BX
    MOV WORD [0x0500], 0
    MOV WORD [0x0502], 0
    MOV WORD [0x0504], 0
    MOV WORD [0x0506], 0
    MOV WORD [0x0508], 0
    MOV WORD [0x050A], 0

e820_loop:
    MOV AX, 0xE820
    MOV CX, 24
    MOV DX, 0x534D
    MOV [DI + 20], DX
    INT 0x15
    JC e820_done
    CMP AX, 0x534D
    JNE e820_done
    TEST BL, BL
    JZ e820_done

    INC WORD [0x0508]
    ADD WORD [0x0504], 24
    ADD DI, 24

    JMP e820_loop

e820_done:
    CMP WORD [0x0508], 0
    JNE a20_enable
    ; No e820 entries: try e801 fallback
    MOV AX, 0xE801
    INT 0x15
    JC e881_fallback
    MOV [0x050C], AX
    MOV [0x050E], BX
    JMP a20_enable

e881_fallback:
    MOV AH, 0x88
    INT 0x15
    JC a20_enable
    MOV [0x050C], AX
    MOV WORD [0x050E], 0
    JMP a20_enable

a20_enable:
    MOV SI, msg_a20
    CALL print_string_16

    IN AL, 0x92
    OR AL, 0x02
    AND AL, 0xFE
    OUT 0x92, AL

    CALL verify_a20
    CMP AL, 1
    JE a20_done

    MOV AL, 0xD1
    OUT 0x64, AL
a20_wait_kbd1:
    IN AL, 0x64
    TEST AL, 0x02
    JNZ a20_wait_kbd1
    MOV AL, 0xDF
    OUT 0x60, AL

    CALL verify_a20
    CMP AL, 1
    JE a20_done

    MOV AX, 0x2401
    INT 0x15

    CALL verify_a20
    CMP AL, 1
    JE a20_done

    MOV SI, msg_a20_fail
    CALL print_string_16
    CLI
    HLT

a20_done:
    MOV SI, msg_vesa
    CALL print_string_16

    ; Try VESA modes: 640x480x32 first (most stable)
    MOV AX, 0x4F02
    MOV BX, 0x4112
    INT 10h
    CMP AX, 0x004F
    JE vesa_ok

    ; 800x600x32
    MOV AX, 0x4F02
    MOV BX, 0x4115
    INT 10h
    CMP AX, 0x004F
    JE vesa_ok

    ; 1024x768x32
    MOV AX, 0x4F02
    MOV BX, 0x4118
    INT 10h
    CMP AX, 0x004F
    JE vesa_ok

    ; 1280x1024x32
    MOV AX, 0x4F02
    MOV BX, 0x411B
    INT 10h
    CMP AX, 0x004F
    JE vesa_ok

    ; Non-32bpp fallbacks
    MOV AX, 0x4F02
    MOV BX, 0x4117
    INT 10h
    CMP AX, 0x004F
    JE vesa_ok

    MOV AX, 0x4F02
    MOV BX, 0x4103
    INT 10h
    CMP AX, 0x004F
    JE vesa_ok

    MOV AX, 0x0013
    INT 0x10

vesa_ok:
    MOV [vesa_mode], BX

    ; ---- Store VBE mode info at 0x700 for kernel ----
    ; First, get VBE mode info block via INT 10h AX=4F01h
    ; We use buffer at 0x0900 (256 bytes) for the mode info block
    MOV CX, BX
    AND CX, 0x3FFF           ; Remove LFB/clear display flags
    MOV AX, 0x4F01
    PUSH ES
    PUSH 0
    POP ES                    ; ES = 0
    MOV DI, 0x0900            ; ES:DI = 0x0900
    INT 10h

    ; Check if VBE info was returned successfully
    ; NOTE: ES is still 0 here (POP ES moved after reads)
    CMP AX, 0x004F
    JNE .vesa_no_info

    ; Extract VBE info from mode info block at ES:0x0900.
    ; ES is still 0 (POP ES deferred), so ES:0x0900 = linear 0x0900.
    MOV DWORD [ES:0x700], 0xB007F1E0

    MOVZX EAX, CX
    MOV [ES:0x704], EAX          ; vbe_mode

    MOV EAX, [ES:0x0900 + 0x28]  ; PhysBasePtr
    MOV [ES:0x708], EAX          ; fb_addr

    MOVZX EAX, WORD [ES:0x0900 + 0x12]  ; XResolution
    MOV [ES:0x70C], EAX          ; fb_width

    MOVZX EAX, WORD [ES:0x0900 + 0x14]  ; YResolution
    MOV [ES:0x710], EAX          ; fb_height

    MOVZX EAX, BYTE [ES:0x0900 + 0x19]  ; BitsPerPixel
    MOV [ES:0x714], EAX          ; fb_bpp

    MOVZX EAX, WORD [ES:0x0900 + 0x10]  ; BytesPerScanLine
    MOV [ES:0x718], EAX          ; fb_pitch

    POP ES                        ; restore ES after all VBE reads done
    JMP .vesa_info_done

.vesa_no_info:
    POP ES                        ; restore ES on error path too

.vesa_info_done:

    MOV SI, msg_load_kernel
    CALL print_string_16

    ; Check LBA support
    MOV AH, 0x41
    MOV BX, 0x55AA
    MOV DL, [boot_drive]
    INT 0x13
    JC load_kernel_chs
    CMP BX, 0xAA55
    JNE load_kernel_chs

;---------------------------------------------------------------------
; LBA load: read KERNEL_TOTAL_SECTORS sectors starting at
; KERNEL_LBA into 0x10000-0x9FFFF (576 KB), in chunks of CHUNK_SECS.
;---------------------------------------------------------------------
load_kernel_lba:
    MOV AX, KERNEL_TOTAL_SECTORS
    MOV [kernel_sectors_left], AX
    MOV WORD [kernel_lba], KERNEL_LBA
    MOV WORD [kernel_lba + 2], 0
    MOV WORD [kernel_dest_off], 0x0000
    MOV WORD [kernel_dest_seg], 0x1000

lba_chunk_loop:
    MOV AX, [kernel_sectors_left]
    TEST AX, AX
    JZ kernel_lba_done

    MOV CX, AX
    CMP CX, CHUNK_SECS
    JBE .lba_set_chunk
    MOV CX, CHUNK_SECS
.lba_set_chunk:
    MOV [dap_count], CX
    MOV AX, [kernel_dest_off]
    MOV [dap_offset], AX
    MOV AX, [kernel_dest_seg]
    MOV [dap_segment], AX
    MOV AX, [kernel_lba]
    MOV [dap_lba], AX
    MOV AX, [kernel_lba + 2]
    MOV [dap_lba + 2], AX
    MOV SI, dap_packet
    MOV AH, 0x42
    MOV DL, [boot_drive]
    PUSH CX
    INT 0x13
    POP CX
    JC load_kernel_chs

    ; Advance destination.  We added (CX * 512) bytes to the
    ; current seg:off.  Compute the new seg:off correctly in 32-bit
    ; form (current linear + delta), then convert back to seg:off.
    ; Old: off += (CX << 9) and increment seg on carry - broken when
    ; SHL overflows (e.g. 128 * 512 = 0x10000 -> low 16 bits = 0, no
    ; carry from ADD so segment never advances).
    PUSH CX                     ; save chunk size for LBA advance
    MOVZX EAX, CX               ; EAX = CX
    SHL EAX, 9                  ; EAX = CX * 512 (32-bit)
    MOVZX EBX, WORD [kernel_dest_seg]
    SHL EBX, 4                  ; EBX = seg * 16
    MOVZX ECX, WORD [kernel_dest_off]
    ADD EBX, ECX                ; EBX = current linear address
    ADD EAX, EBX                ; EAX = new linear address
    SHR EAX, 4
    MOV [kernel_dest_seg], AX
    AND EAX, 0xF
    MOV [kernel_dest_off], AX
    POP CX                      ; restore chunk size

    ; Advance LBA
    MOV AX, [kernel_lba]
    ADD AX, CX
    MOV [kernel_lba], AX
    JNC .lba_no_lba_wrap
    ADD WORD [kernel_lba + 2], 1
.lba_no_lba_wrap:

    ; Decrement remaining
    SUB [kernel_sectors_left], CX
    JMP lba_chunk_loop

kernel_lba_done:
    MOV SI, msg_kernel_lba_ok
    CALL print_string_16
    JMP load_stage2

;---------------------------------------------------------------------
; CHS fallback.  Loads in chunks of 18 sectors.
;---------------------------------------------------------------------
load_kernel_chs:
    MOV SI, msg_using_chs
    CALL print_string_16
    MOV WORD [kernel_lba], KERNEL_LBA
    MOV WORD [kernel_lba + 2], 0
    MOV WORD [kernel_dest_off], 0x0000
    MOV WORD [kernel_dest_seg], 0x1000
    MOV WORD [kernel_sectors_left], KERNEL_TOTAL_SECTORS

kernel_chs_loop:
    CMP WORD [kernel_sectors_left], 0
    JE kernel_chs_done

    MOV CX, [kernel_sectors_left]
    CMP CX, 18
    JBE kset_chunk
    MOV CX, 18

kset_chunk:
    MOV [kernel_chunk], CX

    MOV AX, [kernel_lba]
    XOR DX, DX
    MOV BX, 1008
    DIV BX
    MOV [kchs_cyl], AX

    MOV AX, DX
    XOR DX, DX
    MOV BX, 63
    DIV BX
    MOV [kchs_head], AX
    MOV [kchs_sec], DX
    INC BYTE [kchs_sec]

    MOV AH, 0x02
    MOV AL, BYTE [kernel_chunk]
    MOV DL, [boot_drive]
    MOV DH, BYTE [kchs_head]
    MOV CH, BYTE [kchs_cyl]
    MOV CL, BYTE [kchs_sec]
    SHL BYTE [kchs_cyl + 1], 6
    OR CL, BYTE [kchs_cyl + 1]
    MOV BX, [kernel_dest_off]
    MOV ES, [kernel_dest_seg]
    PUSH CX
    INT 0x13
    POP CX
    JC chs_err

    MOVZX EAX, CL
    SHL EAX, 9                  ; EAX = bytes
    MOVZX EBX, WORD [kernel_dest_off]
    ADD EAX, EBX                ; EAX = new off
    MOV [kernel_dest_off], AX
    ; Note: CHS uses 18-sector chunks (0x2400 bytes), so off will
    ; never overflow 64KB before we wrap it.  Reset off to 0 and
    ; bump segment if it crossed 0x10000 (carry set).
    JNC kchs_no_seg_wrap
    XOR AX, AX
    MOV [kernel_dest_off], AX
    ADD WORD [kernel_dest_seg], 0x1000
kchs_no_seg_wrap:

    MOV AX, [kernel_lba]
    ADD AX, CX
    MOV [kernel_lba], AX
    JNC kchs_no_lba_wrap
    ADD WORD [kernel_lba + 2], 1
kchs_no_lba_wrap:

    SUB [kernel_sectors_left], CX
    JMP kernel_chs_loop

chs_err:
    MOV SI, msg_chs_err
    CALL print_string_16
    CLI
    HLT

kernel_chs_done:
    MOV SI, msg_kernel_lba_ok
    CALL print_string_16
    JMP load_stage2

;---------------------------------------------------------------------
; Load stage2.bin (4 sectors from LBA STAGE2_LBA) to 0x80000 in low
; memory.  Stage 2 is a tiny 32-bit loader that finishes loading the
; rest of the kernel using PIO once we are in protected mode.
;---------------------------------------------------------------------
load_stage2:
    MOV SI, msg_load_stage2
    CALL print_string_16

    MOV [dap_count], STAGE2_SECTORS
    MOV [dap_offset], 0x0000
    MOV WORD [dap_segment], 0x8000          ; 0x8000:0x0000 = 0x80000
    MOV WORD [dap_lba], STAGE2_LBA
    MOV WORD [dap_lba + 2], 0
    MOV SI, dap_packet
    MOV AH, 0x42
    MOV DL, [boot_drive]
    INT 0x13
    JNC stage2_loaded
    MOV SI, msg_stage2_err
    CALL print_string_16
    CLI
    HLT

stage2_loaded:
    MOV SI, msg_stage2_ok
    CALL print_string_16

    MOV SI, msg_pm
    CALL print_string_16

    CLI                     ; Disable interrupts before entering PM

    LGDT [gdt_descriptor]

    ; Enable protected mode
    DB 0x0F, 0x20, 0xC0     ; MOV EAX, CR0
    DB 0x66, 0x0D           ; OR EAX, imm32
    DD 0x00000001           ; PE bit
    DB 0x0F, 0x22, 0xC0     ; MOV CR0, EAX

    ; Far jump to 32-bit pm_entry code
    DB 0x66                  ; operand size prefix for 32-bit offset
    DB 0xEA                  ; JMP far
    DD pm_entry              ; offset (32-bit)
    DW 0x0008                ; selector (code segment)

verify_a20:
    PUSH ES
    PUSH DS
    PUSH DI
    PUSH SI
    PUSHF
    CLI
    XOR AX, AX
    MOV DS, AX
    NOT AX
    MOV ES, AX
    MOV SI, 0x7DF0
    MOV DI, 0x7E0F
    MOV AL, [DS:SI]
    MOV AH, [ES:DI]
    MOV BYTE [DS:SI], 0xAA
    CMP BYTE [ES:DI], 0xAA
    JNE a20_on
    MOV BYTE [DS:SI], 0x55
    CMP BYTE [ES:DI], 0x55
    JNE a20_on
    MOV [DS:SI], AL
    MOV AL, 0
    JMP a20_done_ret
a20_on:
    MOV [DS:SI], AL
    MOV AL, 1
a20_done_ret:
    POPF
    POP SI
    POP DI
    POP DS
    POP ES
    RET

print_string_16:
    LODSB
    OR AL, AL
    JZ ps16_done
    MOV AH, 0x0E
    MOV BH, 0x00
    MOV BL, 0x07
    INT 0x10
    ; Also write to COM1 (0x3F8) so we can capture via -serial file:
    PUSH DX
    PUSH AX
    MOV DX, 0x3FD     ; Line Status Register
.wait_tx:
    IN AL, DX
    TEST AL, 0x20
    JZ .wait_tx
    POP AX
    MOV DX, 0x3F8
    OUT DX, AL
    POP DX
    JMP print_string_16
ps16_done:
    RET

[BITS 32]

;---------------------------------------------------------------------
; pm_entry - protected mode entry point.
;  - Copy 0x10000-0x9FFFF (the kernel image we already read in real
;    mode) to its linked address 0x100000-0x190000.
;  - Far jump to stage2 at 0x80000, which finishes loading the rest
;    of the kernel using PIO and then jumps into it.
;---------------------------------------------------------------------
pm_entry:
    MOV AX, 0x10
    MOV DS, AX
    MOV ES, AX
    MOV FS, AX
    MOV GS, AX
    MOV SS, AX
    MOV ESP, 0x90000

    ; Copy KERNEL_TOTAL_SECTORS*512 bytes from 0x10000 to 0x100000.
    MOV ESI, 0x00010000
    MOV EDI, 0x00100000
    MOV ECX, KERNEL_TOTAL_DWORDS
    REP MOVSD

    ; Far jump to stage2 at linear address 0x80000 (32-bit code
    ; segment 0x08).  stage2.asm's entry point is the first thing in
    ; its .bin output, so its offset within stage2.bin is 0.
    ; PUSH DWORD for CS too: the original PUSH WORD 0x08 caused a
    ; stack-layout bug where EIP/CS ended up swapped (RETF pops
    ; 4 bytes for EIP then 2 for CS, so a 2-byte CS push leaves the
    ; dword reads misaligned).
    PUSH DWORD 0x08              ; CS = 0x08 (low 16 used, upper 16 ignored)
    PUSH DWORD 0x80000           ; EIP = 0x80000
    RETF

; ---- Data section ----
[BITS 16]

gdt_start:
    DD 0x00000000
    DD 0x00000000

    DW 0xFFFF
    DW 0x0000
    DB 0x00
    DB 0x9A
    DB 0xCF
    DB 0x00

    DW 0xFFFF
    DW 0x0000
    DB 0x00
    DB 0x92
    DB 0xCF
    DB 0x00

    DW 0xFFFF
    DW 0x0000
    DB 0x00
    DB 0x9A
    DB 0x00
    DB 0x00

    DW 0xFFFF
    DW 0x0000
    DB 0x00
    DB 0x89
    DB 0x00
    DB 0x00

    DW 0xFFFF
    DW 0x0000
    DB 0x00
    DB 0xFA
    DB 0xCF
    DB 0x00

    DW 0xFFFF
    DW 0x0000
    DB 0x00
    DB 0xF2
    DB 0xCF
    DB 0x00

gdt_end:

gdt_descriptor:
    DW gdt_end - gdt_start - 1
    DD gdt_start

boot_drive:         DB 0x80
vesa_mode:          DW 0

msg_mem_detect:     DB 'Loader: Detecting memory...', 0x0D, 0x0A, 0
msg_a20:            DB 'Loader: Enabling A20...', 0x0D, 0x0A, 0
msg_a20_fail:       DB 'Loader: A20 enable FAILED', 0x0D, 0x0A, 0
msg_vesa:           DB 'Loader: Setting VESA mode...', 0x0D, 0x0A, 0
msg_load_kernel:    DB 'Loader: Loading kernel...', 0x0D, 0x0A, 0
msg_kernel_lba_ok:  DB 'Loader: Kernel loaded into low memory', 0x0D, 0x0A, 0
msg_load_stage2:    DB 'Loader: Loading stage 2...', 0x0D, 0x0A, 0
msg_stage2_ok:      DB 'Loader: Stage 2 loaded', 0x0D, 0x0A, 0
msg_stage2_err:     DB 'Loader: Stage 2 load FAILED', 0x0D, 0x0A, 0
msg_pm:             DB 'Loader: Entering protected mode...', 0x0D, 0x0A, 0
msg_using_chs:      DB 'Loader: LBA not available, using CHS', 0x0D, 0x0A, 0
msg_chs_err:        DB 'Loader: CHS read error', 0x0D, 0x0A, 0

dap_packet:
    DB 0x10
    DB 0x00
dap_count:          DW 0
dap_offset:         DW 0
dap_segment:        DW 0
dap_lba:            DD 0
                    DD 0

kernel_lba:         DW 0
kernel_lba_hi:      DW 0
kernel_dest_off:    DW 0
kernel_dest_seg:    DW 0
kernel_sectors_left:DW 0
kernel_chunk:       DW 0
kchs_cyl:           DW 0
                    DW 0
kchs_head:          DW 0
                    DW 0
kchs_sec:           DW 0
                    DW 0

; Layout constants (must match mkimg.py)
;
; Sector 0:   boot.bin   (MBR)
; Sector 1:   reserved
; Sectors 2-21: loader.bin  (20 sectors, 10 KB)  <- this file
; Sectors 22-25: stage2.bin  (4 sectors, 2 KB)
; Sectors 26+:  kernel.bin
;
KERNEL_LBA           EQU 26
KERNEL_TOTAL_SECTORS EQU 1152     ; 1152 * 512 = 576 KB loaded by loader
KERNEL_TOTAL_DWORDS  EQU (KERNEL_TOTAL_SECTORS * 512) / 4
CHUNK_SECS           EQU 128     ; up to 128 sectors per LBA call (64 KB)
STAGE2_LBA           EQU 22
STAGE2_SECTORS       EQU 4
