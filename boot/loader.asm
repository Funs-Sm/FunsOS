; loader.asm - Real-mode loader
;
; Loaded by boot.asm from disk sector 2 onward, into 0x0000:0x1000.
; Runs in 16-bit real mode, briefly switching to protected mode for
; each chunk copy to move kernel data above 1 MiB.
;
; Responsibilities:
;   1. Detect memory map (INT 15h E820 / E801 / 88h)
;   2. Enable the A20 line
;   3. Set a VESA video mode (best effort, fall back to VGA mode 13h)
;   4. Read kernel info from sector 1 (sector count, total bytes)
;   5. Load kernel image from disk using LBA (INT 13h AH=42h) in
;      chunks to a bounce buffer at 0x10000, then switch to PM to
;      copy each chunk to its linked address 0x100000+.
;   6. After all kernel data is loaded, enter PM and jump to the
;      kernel entry at 0x100000.

[BITS 16]
[ORG 0x1000]

loader_start:
    CLI
    ; Direct serial output to verify loader is executing
    MOV DX, 0x3F8
    MOV AL, 'X'
    OUT DX, AL
    MOV AL, 13
    OUT DX, AL
    MOV AL, 10
    OUT DX, AL

    MOV AX, CS
    MOV DS, AX
    MOV ES, AX
    MOV SS, AX
    MOV SP, 0x9000          ; safe stack below EBDA, above memory map/VBE scratch
    STI

    MOV SI, msg_mem_detect
    CALL print_string_16

    MOV AL, 0x31
    CALL debug_char

    MOV AL, '['
    CALL print_char_16

    ; boot_info_t at 0x0500 (kernel reads mem_upper at 0x50C)
    MOV DI, 0x0A00          ; E820 entries placed at 0x0A00
    XOR EBX, EBX            ; E820 continuation = 0
    MOV DWORD [0x0500], 0
    MOV DWORD [0x0504], 0
    MOV DWORD [0x0508], 0
    MOV DWORD [0x050C], 0
    MOV DWORD [0x0510], 0
    MOV DWORD [0x0514], 0

e820_loop:
    CMP DI, 0x0A00
    JNE .skip_e820_dbg
    MOV AL, 0x32
    CALL debug_char
.skip_e820_dbg:
    MOV EAX, 0x0000E820
    MOV ECX, 24
    MOV EDX, 0x534D4150     ; 'SMAP'
    INT 0x15
    JC e820_done
    CMP EAX, 0x534D4150
    JNE e820_done
    TEST EBX, EBX
    JZ e820_done

    INC WORD [0x0508]       ; entry count
    ADD WORD [0x0514], 24   ; mmap_length
    ADD DI, 24

    JMP e820_loop

e820_done:
    MOV AL, 0x33
    CALL debug_char

    MOV AL, ']'
    CALL print_char_16

    ; Try INT 15h E801 to obtain usable memory above 1 MB.
    ; AX = KB between 1 MB and 16 MB, BX = 64 KB blocks above 16 MB.
    MOV AX, 0xE801
    INT 0x15
    JC .e88_fallback
    MOVZX EAX, AX
    MOVZX EBX, BX
    SHL EBX, 6              ; convert 64 KB blocks -> KB
    ADD EAX, EBX
    JMP .store_upper

.e88_fallback:
    MOV AH, 0x88
    INT 0x15
    JC .default_mem
    MOVZX EAX, AX
    JMP .store_upper

.default_mem:
    MOV EAX, 31 * 1024      ; fallback: assume ~32 MB

.store_upper:
    MOV [0x050C], EAX       ; boot_info.mem_upper (KB)
    MOV DWORD [0x0504], 640 ; boot_info.mem_lower (KB)
    MOV WORD [0x0510], 0x0A00 ; boot_info.mmap_addr

    JMP a20_enable

a20_enable:
    MOV SI, msg_a20
    CALL print_string_16

    MOV AL, 0x34
    CALL debug_char

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
    MOV AL, 0x35
    CALL debug_char
    MOV SI, msg_vesa
    CALL print_string_16

    MOV AX, 0x4F02
    MOV BX, 0x4112
    INT 10h
    CMP AX, 0x004F
    JE vesa_ok

    MOV AX, 0x4F02
    MOV BX, 0x4115
    INT 10h
    CMP AX, 0x004F
    JE vesa_ok

    MOV AX, 0x4F02
    MOV BX, 0x4118
    INT 10h
    CMP AX, 0x004F
    JE vesa_ok

    MOV AX, 0x4F02
    MOV BX, 0x411B
    INT 10h
    CMP AX, 0x004F
    JE vesa_ok

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
    MOV AL, 0x36
    CALL debug_char
    MOV [vesa_mode], BX

    MOV CX, BX
    AND CX, 0x3FFF
    MOV AX, 0x4F01
    PUSH ES
    PUSH 0
    POP ES
    MOV DI, 0x0900
    INT 10h

    CMP AX, 0x004F
    JNE .vesa_no_info

    MOV DWORD [ES:0x700], 0xB007F1E0
    MOVZX EAX, CX
    MOV [ES:0x704], EAX
    MOV EAX, [ES:0x0900 + 0x28]
    MOV [ES:0x708], EAX
    MOVZX EAX, WORD [ES:0x0900 + 0x12]
    MOV [ES:0x70C], EAX
    MOVZX EAX, WORD [ES:0x0900 + 0x14]
    MOV [ES:0x710], EAX
    MOVZX EAX, BYTE [ES:0x0900 + 0x19]
    MOV [ES:0x714], EAX
    MOVZX EAX, WORD [ES:0x0900 + 0x10]
    MOV [ES:0x718], EAX
    POP ES
    JMP .vesa_info_done

.vesa_no_info:
    POP ES

.vesa_info_done:

    MOV AL, 0x37
    CALL debug_char

;------------------------------------------------------------------
; Load 8x16 ROM font into VGA character generator RAM (plane 2).
; Must be done in real mode via BIOS INT 10h AH=11h before we
; switch to protected mode.  Without this, the CGRAM contains
; garbage from the VBE graphics mode and text output is garbled.
;------------------------------------------------------------------
    MOV AX, 0x1112          ; INT 10h AH=11h AL=12h: load 8x16 ROM font
    MOV BL, 0              ; block 0 of character generator
    MOV BH, 16             ; 16 bytes per character (8x16 font)
    INT 10h

;------------------------------------------------------------------
; Switch to VGA text mode 3 (80x25) before entering protected mode.
; This ensures:
;   - CRTC registers are correctly programmed for text output
;   - Standard 8x16 ROM font is loaded into CGRAM (plane 2)
;   - Text buffer at 0xB8000 is cleared and ready
;   - All timing/sequencer registers are in known-good state
;
; The kernel will use this text-mode display when VBE is unavailable.
;------------------------------------------------------------------
    MOV AX, 0x0003          ; INT 10h: set video mode = text mode 3 (80x25)
    INT 10h

;------------------------------------------------------------------
; Read kernel info block from sector 1 (written by mkimg.py)
;------------------------------------------------------------------
    MOV SI, msg_kern_info
    CALL print_string_16

    MOV WORD [dap_count], 1
    MOV WORD [dap_offset], 0x0600
    MOV WORD [dap_segment], 0x0000
    MOV DWORD [dap_lba], 1
    MOV DWORD [dap_lba + 4], 0
    MOV SI, dap_packet
    MOV AH, 0x42
    MOV DL, [boot_drive]
    INT 0x13
    JNC .got_info_blk

    MOV DWORD [kern_total_sectors], KERNEL_FALLBACK_SECTORS
    JMP .info_done

.got_info_blk:
    CMP DWORD [0x0600], 0xB007F00D
    JNE .fallback_info
    MOV EAX, [0x0604]
    MOV [kern_total_sectors], EAX
    JMP .info_done

.fallback_info:
    MOV DWORD [kern_total_sectors], KERNEL_FALLBACK_SECTORS

.info_done:
    MOV AL, 0x38
    CALL debug_char
    MOV SI, msg_load_kernel
    CALL print_string_16

    MOV DWORD [kern_lba], KERNEL_LBA
    MOV DWORD [kern_dst], 0x100000
    MOV EAX, [kern_total_sectors]
    MOV [kern_remaining], EAX

    MOV AH, 0x41
    MOV BX, 0x55AA
    MOV DL, [boot_drive]
    INT 0x13
    JC .kern_chs_load
    CMP BX, 0xAA55
    JNE .kern_chs_load

;---------------------------------------------------------------------
; LBA kernel loading loop — load chunk to 0x10000, PM copy to
; kern_dst, switch back to RM, repeat.
;---------------------------------------------------------------------
.kern_lba_load:
    MOV AL, 0x39
    CALL debug_char
    CMP DWORD [kern_remaining], 0
    JE .kern_loaded

    MOV EAX, [kern_remaining]
    CMP EAX, CHUNK_SECS
    JBE .lba_size_ok
    MOV EAX, CHUNK_SECS
.lba_size_ok:
    MOV [dap_count], AX
    MOV WORD [dap_offset], 0x0000
    MOV WORD [dap_segment], 0x1000
    MOV EAX, [kern_lba]
    MOV [dap_lba], EAX
    MOV DWORD [dap_lba + 4], 0

    MOV SI, dap_packet
    MOV AH, 0x42
    MOV DL, [boot_drive]
    INT 0x13
    JC .kern_chs_load

    CLI
    LGDT [gdt_descriptor]

    MOV EAX, CR0
    OR AL, 1
    MOV CR0, EAX

    DB 0x66
    DB 0xEA
    DD .pm_copy
    DW 0x08

[BITS 32]
.pm_copy:
    MOV AX, 0x10
    MOV DS, AX
    MOV ES, AX
    MOV FS, AX
    MOV GS, AX
    MOV SS, AX
    MOV ESP, 0x90000

    MOVZX ECX, WORD [dap_count]
    SHL ECX, 7
    MOV ESI, 0x10000
    MOV EDI, [kern_dst]
    REP MOVSD

    MOVZX EAX, WORD [dap_count]
    SHL EAX, 9
    ADD [kern_dst], EAX

    JMP 0x18:.pm_to_rm

[BITS 16]
.pm_to_rm:
    MOV EAX, CR0
    AND AL, ~1
    MOV CR0, EAX

    JMP 0x0000:.rm_cont

.rm_cont:
    XOR AX, AX
    MOV DS, AX
    MOV ES, AX
    MOV FS, AX
    MOV GS, AX
    MOV SS, AX
    MOV SP, 0x2000
    STI

    MOVZX EAX, WORD [dap_count]
    ADD DWORD [kern_lba], EAX
    SUB DWORD [kern_remaining], EAX

    JMP .kern_lba_load

.kern_chs_load:
    MOV SI, msg_using_chs
    CALL print_string_16
    CLI
    HLT

.kern_loaded:
    MOV AL, 0x41
    CALL debug_char
    MOV SI, msg_kern_loaded
    CALL print_string_16

    MOV SI, msg_pm
    CALL print_string_16

    CLI
    LGDT [gdt_descriptor]

    MOV EAX, CR0
    OR AL, 1
    MOV CR0, EAX

    DB 0x66
    DB 0xEA
    DD pm_jump_kernel
    DW 0x08

[BITS 32]
pm_jump_kernel:
    MOV AX, 0x10
    MOV DS, AX
    MOV ES, AX
    MOV FS, AX
    MOV GS, AX
    MOV SS, AX
    MOV ESP, 0x90000

    PUSH DWORD 0x08
    PUSH DWORD 0x100000
    RETF

; All remaining code runs in real mode - ensure 16-bit encoding
[BITS 16]

debug_char:
    PUSH AX
    PUSH DX
    PUSH CX
    MOV DX, 0x3FD
    MOV CX, 0xFFFF
.dc_wait:
    IN AL, DX
    TEST AL, 0x20
    JNZ .dc_ready
    LOOP .dc_wait
    JMP .dc_skip
.dc_ready:
    POP CX
    POP DX
    MOV DX, 0x3F8
    POP AX
    OUT DX, AL
    RET
.dc_skip:
    POP CX
    POP DX
    ADD SP, 2
    RET

verify_a20:
    PUSH ES
    PUSH DS
    PUSH DI
    PUSH SI
    PUSH AX
    PUSHF
    CLI
    XOR AX, AX
    MOV DS, AX
    MOV AX, 0xFFFF
    MOV ES, AX
    MOV SI, 0x0500
    MOV DI, 0x0510          ; 0xFFFF:0x0510 == 0x100500, wraps to 0x00500 if A20 off
    MOV AL, [DS:SI]
    MOV AH, [ES:DI]
    MOV BYTE [DS:SI], 0xAA
    MOV BYTE [ES:DI], 0x55
    CMP BYTE [DS:SI], 0x55
    JNE a20_on
    MOV [DS:SI], AL
    MOV AL, 0
    JMP a20_done_ret
a20_on:
    MOV [DS:SI], AL
    MOV [ES:DI], AH
    MOV AL, 1
a20_done_ret:
    POPF
    POP AX
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
    PUSH DX
    PUSH AX
    PUSH CX
    MOV DX, 0x3FD
    MOV CX, 0xFFFF
.wait_tx:
    IN AL, DX
    TEST AL, 0x20
    JNZ .tx_ready
    LOOP .wait_tx
    JMP .tx_skip
.tx_ready:
    POP CX
    POP AX
    MOV DX, 0x3F8
    OUT DX, AL
    POP DX
    JMP print_string_16
.tx_skip:
    POP CX
    POP AX
    POP DX
    JMP print_string_16
ps16_done:
    RET

; Print a single character in AL via BIOS TTY and COM1 (no polling)
print_char_16:
    PUSH AX
    PUSH BX
    PUSH DX
    MOV AH, 0x0E
    MOV BH, 0x00
    MOV BL, 0x07
    INT 0x10
    MOV DX, 0x3F8
    OUT DX, AL
    POP DX
    POP BX
    POP AX
    RET

; ---- GDT ----
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

; ---- Data section ----
boot_drive:         DB 0x80
vesa_mode:          DW 0

msg_mem_detect:     DB 'Loader: Detecting memory...', 0x0D, 0x0A, 0
msg_a20:            DB 'Loader: Enabling A20...', 0x0D, 0x0A, 0
msg_a20_fail:       DB 'Loader: A20 enable FAILED', 0x0D, 0x0A, 0
msg_vesa:           DB 'Loader: Setting VESA mode...', 0x0D, 0x0A, 0
msg_kern_info:      DB 'Loader: Reading kernel info...', 0x0D, 0x0A, 0
msg_load_kernel:    DB 'Loader: Loading kernel...', 0x0D, 0x0A, 0
msg_kern_loaded:    DB 'Loader: Kernel fully loaded', 0x0D, 0x0A, 0
msg_pm:             DB 'Loader: Entering protected mode...', 0x0D, 0x0A, 0
msg_using_chs:      DB 'Loader: LBA required, CHS not supported', 0x0D, 0x0A, 0

dap_packet:
    DB 0x10
    DB 0x00
dap_count:          DW 0
dap_offset:         DW 0
dap_segment:        DW 0
dap_lba:            DD 0
                    DD 0

kern_total_sectors: DD 0
kern_lba:           DD 0
kern_dst:           DD 0
kern_remaining:     DD 0

KERNEL_LBA            EQU 26
CHUNK_SECS            EQU 64
KERNEL_FALLBACK_SECTORS EQU 1216