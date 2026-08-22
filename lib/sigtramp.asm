; lib/sigtramp.asm - default user-space signal-return trampoline.
;
; When a handler is registered without SA_RESTORER, FunsCore installs
; the address of this trampoline into the saved return slot of the
; signal frame.  After the handler returns, execution lands here,
; which performs `int $0x80` with EAX = SYS_SIGRETURN.  The kernel's
; sys_sigreturn restores the original registers and resumes the
; pre-signal instruction pointer.
;
; This trampoline is intentionally minimal so the kernel can place it
; anywhere in the lower 4 GiB user-mapped region.

global _funsos_default_sigreturn_trampoline
_funsos_default_sigreturn_trampoline:
    mov eax, 48         ; SYS_SIGRETURN
    int 0x80
    ; Should not return.  Halt the CPU if it does.
.halt:
    hlt
    jmp .halt
