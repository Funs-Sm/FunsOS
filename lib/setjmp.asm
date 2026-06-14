section .text

global setjmp
global longjmp

setjmp:
    mov eax, [esp+4]
    mov [eax+0], ebx
    mov [eax+4], esi
    mov [eax+8], edi
    mov [eax+12], ebp
    mov ecx, [esp]
    mov [eax+16], ecx
    lea ecx, [esp+4]
    mov [eax+20], ecx
    xor eax, eax
    ret

longjmp:
    mov eax, [esp+4]
    mov ebx, [eax+0]
    mov esi, [eax+4]
    mov edi, [eax+8]
    mov ebp, [eax+12]
    mov ecx, [eax+16]
    mov esp, [eax+20]
    mov eax, [esp+8]
    test eax, eax
    jnz .done
    inc eax
.done:
    jmp ecx
