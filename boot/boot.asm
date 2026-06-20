; boot.asm - MBR (Master Boot Record)
;
; Loaded by the BIOS at 0x0000:0x7C00 (or 0x07C0:0x0000).
; Loads the loader (20 sectors from LBA 2) to 0x0000:0x1000.
;
; Uses INT 13h extensions (LBA) if available, falls back to CHS.

[BITS 16]
[ORG 0x7C00]

start:
    CLI
    XOR AX, AX
    MOV DS, AX
    MOV ES, AX
    MOV FS, AX
    MOV GS, AX
    MOV SS, AX
    MOV SP, 0x7C00          ; stack grows down from 0x7C00

    ; Now DS=0, safe to access variables
    MOV [boot_drive], DL
    STI

    ; Tell serial port we're alive
    MOV AL, 'B'
    CALL ser_putc

    MOV SI, msg_loading
    CALL print_str

    ; Disk reset with retry
    MOV BYTE [retry], 3
.reset:
    DEC BYTE [retry]
    JZ disk_error
    MOV DL, [boot_drive]
    XOR AH, AH
    INT 0x13
    JC .reset

    ; --- Check for INT 13h extensions ---
    MOV AH, 0x41
    MOV BX, 0x55AA
    MOV DL, [boot_drive]
    INT 0x13
    JC .use_chs
    CMP BX, 0xAA55
    JNE .use_chs

    ; LBA extensions available - use extended read
    MOV AL, 'L'
    CALL ser_putc

    ; Set up DAP
    MOV BYTE [dap_size], 0x10
    MOV BYTE [dap_zero], 0
    MOV WORD [dap_count], 20     ; read all 20 sectors at once
    MOV WORD [dap_off], 0x1000
    MOV WORD [dap_seg], 0x0000
    MOV DWORD [dap_lba], 2
    MOV DWORD [dap_lba+4], 0

    MOV BYTE [retry], 3
.lba_retry:
    DEC BYTE [retry]
    JZ disk_error
    MOV AH, 0x42
    MOV DL, [boot_drive]
    MOV SI, dap_packet
    INT 0x13
    JC .lba_retry

    JMP load_done

    ; --- CHS fallback ---
.use_chs:
    MOV AL, 'C'
    CALL ser_putc

    MOV WORD [lba_cur], 2
    MOV WORD [lba_cur+2], 0
    MOV WORD [dest_off], 0x1000
    MOV WORD [sectors_left], 20

.chs_loop:
    CMP WORD [sectors_left], 0
    JE load_done

    ; Max 18 sectors per CHS read (one track)
    MOV CX, [sectors_left]
    CMP CX, 18
    JBE .chs_n
    MOV CX, 18
.chs_n:
    MOV [chunk], CL

    ; LBA -> CHS: cylinder = LBA / (heads * sectors_per_track)
    ;              head     = (LBA % (heads * sectors_per_track)) / sectors_per_track
    ;              sector   = (LBA % sectors_per_track) + 1
    ; We use 16 heads * 63 SPT = 1008 sectors/cylinder
    MOVZX EAX, WORD [lba_cur]
    XOR EDX, EDX
    MOV EBX, 1008
    DIV EBX                     ; EAX = cylinder, EDX = remainder
    MOV [cyl], AX
    MOV EAX, EDX
    XOR EDX, EDX
    MOV EBX, 63
    DIV EBX                     ; EAX = head, EDX = sector-1
    MOV [head], AL
    INC DL                      ; sector = (remainder) + 1
    MOV [sector], DL

    MOV BYTE [retry], 3
.chs_retry:
    DEC BYTE [retry]
    JZ disk_error
    MOV AH, 0x02
    MOV AL, [chunk]
    MOV DL, [boot_drive]
    MOV DH, [head]
    MOV CH, [cyl]               ; cylinder low 8 bits
    MOV CL, [sector]            ; sector in bits 0-5
    MOV BH, [cyl+1]             ; cylinder bits 8-9
    SHL BH, 6
    OR CL, BH                   ; CL bits 6-7 = cylinder bits 8-9
    MOV BX, [dest_off]
    MOV ES, [dest_seg]
    INT 0x13
    JC .chs_retry

    ; Advance
    MOVZX CX, BYTE [chunk]
    SUB [sectors_left], CX
    ADD WORD [lba_cur], CX
    MOVZX EAX, CX
    SHL EAX, 9                  ; * 512
    ADD [dest_off], AX
    JMP .chs_loop

load_done:
    MOV AL, 'O'
    CALL ser_putc
    MOV SI, msg_ok
    CALL print_str

    ; Jump to loader at 0x0000:0x1000
    JMP 0x0000:0x1000

disk_error:
    MOV AL, 'E'
    CALL ser_putc
    MOV SI, msg_err
    CALL print_str
    CLI
    HLT

; --- Serial output helper ---
ser_putc:
    PUSH AX
    PUSH DX
    MOV DX, 0x3FD
.wait:
    IN AL, DX
    TEST AL, 0x20
    JZ .wait
    POP DX
    POP AX
    MOV DX, 0x3F8
    OUT DX, AL
    RET

; --- Print string via BIOS TTY ---
print_str:
    LODSB
    OR AL, AL
    JZ .done
    MOV AH, 0x0E
    MOV BH, 0x00
    MOV BL, 0x07
    INT 0x10
    JMP print_str
.done:
    RET

; --- Data ---
boot_drive:   DB 0
retry:        DB 0
chunk:        DB 0
sectors_left: DW 0
dest_off:     DW 0
dest_seg:     DW 0x0000
lba_cur:      DD 0

; CHS temps
cyl:          DW 0
head:         DB 0
sector:       DB 0

; DAP (Disk Address Packet)
dap_packet:
dap_size:     DB 0x10
dap_zero:     DB 0
dap_count:    DW 0
dap_off:      DW 0
dap_seg:      DW 0
dap_lba:      DQ 0

msg_loading:  DB 'MBR: Loading...', 13, 10, 0
msg_ok:       DB 'MBR: OK', 13, 10, 0
msg_err:      DB 'MBR: Error', 13, 10, 0

TIMES 510 - ($ - $$) DB 0
DW 0xAA55