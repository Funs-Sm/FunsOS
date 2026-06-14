[BITS 16]
[ORG 0x7C00]

start:
    CLI
    MOV [boot_drive], DL
    XOR AX, AX
    MOV DS, AX
    MOV ES, AX
    MOV FS, AX
    MOV GS, AX
    MOV SS, AX
    MOV SP, 0x7C00
    STI

    MOV SI, msg_loading
    CALL print_string_16

    MOV BYTE [retry_count], 3
reset_disk:
    CMP BYTE [retry_count], 0
    JE disk_error_halt
    MOV DL, [boot_drive]
    XOR AH, AH
    INT 0x13
    JC reset_retry
    JMP load_sectors

reset_retry:
    DEC BYTE [retry_count]
    JMP reset_disk

disk_error_halt:
    MOV SI, msg_disk_err
    CALL print_string_16
    MOV AL, [error_code]
    CALL print_hex_byte
    CLI
    HLT

load_sectors:
    MOV WORD [current_lba], 2
    MOV WORD [current_lba + 2], 0
    MOV WORD [dest_offset], 0x1000
    MOV WORD [dest_segment], 0x0000
    MOV WORD [sectors_remaining], 20

load_loop:
    CMP WORD [sectors_remaining], 0
    JE load_done

    MOV CX, [sectors_remaining]
    CMP CX, 18
    JBE .set_chunk
    MOV CX, 18

.set_chunk:
    MOV [chunk_size], CX

    MOV BYTE [retry_count], 3

read_retry:
    CMP BYTE [retry_count], 0
    JE disk_error_halt

    MOVZX EAX, WORD [current_lba]
    XOR EDX, EDX
    MOV EBX, 1008
    DIV EBX
    MOV [chs_cylinder], EAX

    MOV EAX, EDX
    XOR EDX, EDX
    MOV EBX, 63
    DIV EBX
    MOV [chs_head], EAX
    MOV [chs_sector], EDX
    INC BYTE [chs_sector]

    MOV AH, 0x02
    MOV AL, [chunk_size]
    MOV DL, [boot_drive]
    MOV DH, BYTE [chs_head]
    MOV CH, BYTE [chs_cylinder]
    MOV CL, BYTE [chs_sector]
    SHL BYTE [chs_cylinder + 1], 6
    OR CL, BYTE [chs_cylinder + 1]
    MOV BX, [dest_offset]
    MOV ES, [dest_segment]
    INT 0x13
    JNC read_success

    MOV [error_code], AH
    DEC BYTE [retry_count]
    JMP read_retry

read_success:
    MOVZX CX, BYTE [chunk_size]
    SUB [sectors_remaining], CX
    ADD [current_lba], CX
    MOVZX EAX, CX
    SHL EAX, 9
    ADD [dest_offset], AX
    JMP load_loop

load_done:
    MOV SI, msg_ok
    CALL print_string_16
    JMP 0x1000

print_string_16:
    LODSB
    OR AL, AL
    JZ .done
    MOV AH, 0x0E
    MOV BH, 0x00
    MOV BL, 0x07
    INT 0x10
    JMP print_string_16
.done:
    RET

print_hex_byte:
    MOV AH, AL
    SHR AL, 4
    ADD AL, '0'
    CMP AL, '9'
    JBE .first
    ADD AL, 7
.first:
    MOV AH, 0x0E
    MOV BH, 0x00
    MOV BL, 0x07
    INT 0x10
    MOV AL, AH
    AND AL, 0x0F
    ADD AL, '0'
    CMP AL, '9'
    JBE .second
    ADD AL, 7
.second:
    MOV AH, 0x0E
    MOV BH, 0x00
    MOV BL, 0x07
    INT 0x10
    RET

boot_drive:        DB 0
retry_count:       DB 0
error_code:        DB 0
current_lba:       DW 0
current_lba_hi:    DW 0
dest_offset:       DW 0
dest_segment:      DW 0
sectors_remaining: DW 0
chunk_size:        DB 0
chs_cylinder:      DD 0
chs_head:          DD 0
chs_sector:        DD 0

msg_loading: DB 'MBR: Loading...', 0x0D, 0x0A, 0
msg_ok:      DB 'MBR: OK', 0x0D, 0x0A, 0
msg_disk_err: DB 'MBR: Disk Error 0x', 0

TIMES 510 - ($ - $$) DB 0
DW 0xAA55
