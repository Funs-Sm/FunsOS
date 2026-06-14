section .text
global _start
extern main

_start:
    mov esp, 0x08048000
    sub esp, 4096
    mov ebp, esp
    push 0
    push 0
    call main
    add esp, 8
    push eax
    mov eax, 1
    pop ebx
    int 0x80
