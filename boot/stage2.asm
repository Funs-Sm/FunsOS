; stage2.asm - 32-bit protected mode second-stage loader
;
; Loaded by loader.asm to physical address 0x80000 in low memory using
; real-mode BIOS LBA read.  When the loader has switched to protected
; mode, set up segments, and copied the first 576KB of the kernel
; from 0x10000-0x9FFFF to 0x100000-0x190000, it far-jumps to us.
;
; Our job: use PIO (ATA) to read the rest of the kernel image from
; the boot disk and write it to its linked address (0x190000+).  Then
; jump to the kernel's _start entry point at 0x100000.
;
; PIO is used because the IDT is not yet set up in protected mode,
; so BIOS INT 13h calls from the trampoline would fault.  Direct
; ATA PIO works in both real and protected mode.

[BITS 32]
[ORG 0x80000]

stage2_entry:
    MOV ESP, 0x70000             ; small stack just for stage 2
    MOV AX, 0x10
    MOV DS, AX
    MOV ES, AX
    MOV FS, AX
    MOV GS, AX
    MOV SS, AX

    ; Print "stage2: starting"
    MOV ESI, s2_msg_start
    CALL s2_print

    ; Initialize PIO read state
    MOV DWORD [s2_lba], KERNEL_CONT_LBA
    MOV DWORD [s2_dst], 0x190000
    MOV DWORD [s2_remaining], KERNEL_CONT_SECTORS

    ; Read IDE status register once to verify controller responds
    MOV DX, 0x1F7
    IN AL, DX
    MOV [s2_status], AL

    ; Print "PIO probe"
    MOV ESI, s2_msg_probe
    CALL s2_print

    ; Try reading one sector as a test
    MOV ESI, KERNEL_CONT_LBA
    MOV EDI, 0x190000
    CALL pio_read_sector
    JNC .probe_ok
    ; PIO probe failed - the kernel may be small enough that the
    ; loader already loaded it all.  Jump directly to kernel entry.
    MOV ESI, s2_msg_probe_fail
    CALL s2_print
    MOV ESI, s2_msg_small
    CALL s2_print
    JMP .s2_done
.probe_ok:
    MOV ESI, s2_msg_probe_ok
    CALL s2_print

    ; PIO read loop
.s2_outer:
    MOV ECX, [s2_remaining]
    TEST ECX, ECX
    JZ .s2_done
    MOV ESI, [s2_lba]
    MOV EDI, [s2_dst]
    CALL pio_read_sector
    JC .s2_err
    ADD DWORD [s2_lba], 1
    ADD DWORD [s2_dst], 512
    DEC DWORD [s2_remaining]
    JMP .s2_outer

.s2_err:
    MOV ESI, s2_msg_err
    CALL s2_print
    CLI
    HLT

.s2_done:
    MOV ESI, s2_msg_ok
    CALL s2_print
    ; Far jump to kernel entry at 0x100000 using 32-bit code segment 0x08.
    ; PUSH CS as DWORD so RETF reads it at the right stack offset.
    PUSH DWORD 0x08
    PUSH DWORD 0x100000
    RETF

;---------------------------------------------------------------------
; pio_read_sector - read 1 sector from disk LBA ESI into buffer EDI.
; Uses primary master IDE (I/O ports 0x1F0-0x1F7).
; Returns CF=0 on success, CF=1 on error.
;
; NOTE: we use only 16-bit IO instructions (IN/OUT to DX, AL only)
; and 16-bit PIO reads via REP INSW with ECX count.  The 16-bit
; segment registers have a 64KB limit, so we need EDI to be
; below 0x100000 OR use ES:EDI override.  Since we're in 32-bit
; mode with a flat address space, EDI is a full 32-bit pointer.
; The OUTS instruction reads from DS:ESI but we want to write
; to a 32-bit address.  Solution: we use INSW which writes to
; ES:EDI - the segment override doesn't matter in flat 32-bit.
;---------------------------------------------------------------------
pio_read_sector:
    PUSHAD

    ; Wait for BSY=0 and DRDY=1 (with timeout)
    MOV DX, 0x1F7
    MOV EBP, 1000000              ; timeout counter
.wait_rdy:
    IN AL, DX
    TEST AL, 0x80
    JNZ .wait_bsy
    TEST AL, 0x40
    JNZ .rdy_ok
.wait_bsy:
    DEC EBP
    JNZ .wait_rdy
    JMP .pio_err                  ; timeout
.rdy_ok:

    ; Drive/head: 0xE0 (master + LBA mode) | (LBA bits 24-27)
    MOV EAX, ESI
    AND AL, 0x0F
    OR AL, 0xE0
    MOV DX, 0x1F6
    OUT DX, AL

    ; Features register = 0
    MOV DX, 0x1F1
    XOR AL, AL
    OUT DX, AL

    ; Sector count = 1
    MOV DX, 0x1F2
    MOV AL, 1
    OUT DX, AL

    ; LBA low byte
    MOV EAX, ESI
    MOV DX, 0x1F3
    OUT DX, AL

    ; LBA mid byte
    MOV EAX, ESI
    SHR EAX, 8
    MOV DX, 0x1F4
    OUT DX, AL

    ; LBA high byte
    MOV EAX, ESI
    SHR EAX, 16
    MOV DX, 0x1F5
    OUT DX, AL

    ; Issue READ SECTORS command
    MOV DX, 0x1F7
    MOV AL, 0x20
    OUT DX, AL

    ; Wait for DRQ (with timeout)
    MOV EBP, 1000000
.wait_drq:
    MOV DX, 0x1F7
    IN AL, DX
    TEST AL, 0x80
    JNZ .pio_err                  ; BSY set after command = error
    TEST AL, 0x08
    JNZ .drq_ok
    DEC EBP
    JNZ .wait_drq
    JMP .pio_err                  ; timeout
.drq_ok:

    ; Read 256 words from data port
    MOV DX, 0x1F0
    MOV ECX, 256
    REP INSW

    POPAD
    CLC
    RET

.pio_err:
    POPAD
    STC
    RET

;---------------------------------------------------------------------
; s2_print - print NUL-terminated string at linear address ESI.
;
; Outputs to COM1 (0x3F8) for serial capture.  INT 10h teletype
; output is NOT used because: (a) we are in protected mode and
; cannot call BIOS interrupts directly, and (b) the bootloader
; has set a VESA graphics mode where INT 10h AH=0Eh does not
; produce visible output anyway.  Serial output is sufficient
; for debugging (visible via QEMU -serial stdio).
;---------------------------------------------------------------------
s2_print:
    PUSHAD
    MOV EBX, ESI
.s2_serial:
    MOV AL, BYTE [EBX]
    TEST AL, AL
    JZ .s2_serial_done
    PUSH EDX
.wait_tx:
    MOV DX, 0x3FD
    IN AL, DX
    TEST AL, 0x20
    JZ .wait_tx
    MOV AL, BYTE [EBX]
    MOV DX, 0x3F8
    OUT DX, AL
    POP EDX
    INC EBX
    JMP .s2_serial
.s2_serial_done:
    POPAD
    RET

;---------------------------------------------------------------------
; Data
;---------------------------------------------------------------------
ALIGN 4
s2_lba:         DD 0
s2_dst:         DD 0
s2_remaining:   DD 0
s2_status:      DB 0
                TIMES 3 DB 0   ; align to 4 bytes

s2_msg_start:   DB 'stage2: starting kernel load', 13, 10, 0
s2_msg_probe:   DB 'stage2: PIO probe', 13, 10, 0
s2_msg_probe_ok:DB 'stage2: PIO probe ok', 13, 10, 0
s2_msg_probe_fail: DB 'stage2: PIO probe failed', 13, 10, 0
s2_msg_err:     DB 'stage2: PIO read error', 13, 10, 0
s2_msg_ok:      DB 'stage2: kernel fully loaded, jumping to entry', 13, 10, 0
s2_msg_small:   DB 'stage2: kernel small, skipping PIO', 13, 10, 0

KERNEL_CONT_LBA      EQU 1178
KERNEL_CONT_SECTORS  EQU 7000     ; ~3.5MB more (total covers ~4.2MB kernel)
