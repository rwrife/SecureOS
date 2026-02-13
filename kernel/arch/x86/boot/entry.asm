[bits 32]
section .text

global _start
extern kmain

_start:
    cli
    mov esp, stack_top
    call kmain

.hang:
    hlt
    jmp .hang

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:
