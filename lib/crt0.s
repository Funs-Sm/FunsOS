; crt0.s - C 运行时启动代码
; 用户态程序入口点，设置栈和参数后调用 main()

[section .text]
[global _start]

_start:
    ; 设置栈 (用户栈顶部 0xBFFFF000)
    mov esp, 0xBFFFF000 - 4096

    ; argc = 0, argv = NULL (简化版)
    push 0          ; envp = NULL
    push 0          ; argv[0] = NULL
    push 0          ; argc = 0

    call main

    ; main 返回后通过系统调用退出
    mov ebx, eax    ; 返回值
    mov eax, 1      ; SYS_EXIT
    int 0x80

    ; 如果 int 0x80 返回（不应该）
.halt:
    hlt
    jmp .halt
