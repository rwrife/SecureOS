; SecureOS IDT exception stubs — x86-64 long mode
;
; Purpose:
;   Provides the low-level ISR (Interrupt Service Routine) entry points for
;   CPU exceptions 0-31. Each stub pushes a uniform frame (error code + vector
;   number) and calls the C handler `idt_exception_handler`.
;
; Interactions:
;   - idt.c: the C handler `idt_exception_handler` is called from here.
;   - entry.asm: these are in the same compilation unit style (nasm elf64).
;   - fault_recover: the C handler checks if recovery is armed and acts.
;
; Frame pushed by CPU on exception (ring-0 to ring-0, no privilege change):
;   [RSP+32] RFLAGS
;   [RSP+24] CS
;   [RSP+16] RIP
;   [RSP+8]  Error code (if exception provides one, else we push 0)
;   [RSP+0]  Vector number (pushed by our stub)

section .text
[bits 64]

extern idt_exception_handler

; Macro for exceptions WITHOUT an error code (CPU doesn't push one)
%macro ISR_NOERR 1
global isr_stub_%1
isr_stub_%1:
    push qword 0          ; dummy error code
    push qword %1         ; vector number
    jmp isr_common
%endmacro

; Macro for exceptions WITH an error code (CPU already pushed it)
%macro ISR_ERR 1
global isr_stub_%1
isr_stub_%1:
    push qword %1         ; vector number (error code already on stack)
    jmp isr_common
%endmacro

; --- Exception stubs (vectors 0–31) ---
ISR_NOERR 0    ; #DE Divide Error
ISR_NOERR 1    ; #DB Debug
ISR_NOERR 2    ; NMI
ISR_NOERR 3    ; #BP Breakpoint
ISR_NOERR 4    ; #OF Overflow
ISR_NOERR 5    ; #BR Bound Range Exceeded
ISR_NOERR 6    ; #UD Invalid Opcode
ISR_NOERR 7    ; #NM Device Not Available
ISR_ERR   8    ; #DF Double Fault
ISR_NOERR 9    ; Coprocessor Segment Overrun (legacy)
ISR_ERR   10   ; #TS Invalid TSS
ISR_ERR   11   ; #NP Segment Not Present
ISR_ERR   12   ; #SS Stack-Segment Fault
ISR_ERR   13   ; #GP General Protection Fault
ISR_ERR   14   ; #PF Page Fault
ISR_NOERR 15   ; Reserved
ISR_NOERR 16   ; #MF x87 FPU Error
ISR_ERR   17   ; #AC Alignment Check
ISR_NOERR 18   ; #MC Machine Check
ISR_NOERR 19   ; #XM SIMD Exception
ISR_NOERR 20   ; #VE Virtualization Exception
ISR_ERR   21   ; #CP Control Protection
ISR_NOERR 22   ; Reserved
ISR_NOERR 23   ; Reserved
ISR_NOERR 24   ; Reserved
ISR_NOERR 25   ; Reserved
ISR_NOERR 26   ; Reserved
ISR_NOERR 27   ; Reserved
ISR_NOERR 28   ; Reserved
ISR_NOERR 29   ; Reserved
ISR_NOERR 30   ; Reserved
ISR_NOERR 31   ; Reserved

; --- Common ISR handler ---
; Stack at this point:
;   [RSP+0]  vector number
;   [RSP+8]  error code (real or dummy 0)
;   [RSP+16] RIP (pushed by CPU)
;   [RSP+24] CS  (pushed by CPU)
;   [RSP+32] RFLAGS (pushed by CPU)
;   [RSP+40] RSP (pushed by CPU — only if privilege change, not for ring-0→ring-0)
;   Note: in long mode ring-0→ring-0, CPU pushes SS:RSP unconditionally
isr_common:
    ; Save all general-purpose registers
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; Pass pointer to the saved frame as first argument (RDI)
    mov rdi, rsp
    call idt_exception_handler

    ; If handler returns (no recovery jump), restore and iretq
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    ; Remove vector number and error code
    add rsp, 16
    iretq

; --- Fault recovery implementation ---
; fault_recover_set: saves callee-saved regs + RSP + return address
; Returns 0 on first call, non-zero when recovered
;
; Recovery buffer layout (fault_recover_buf):
;   [0]  RBX
;   [8]  RBP
;   [16] R12
;   [24] R13
;   [32] R14
;   [40] R15
;   [48] RSP (the caller's RSP after return)
;   [56] RIP (return address)

section .data
global fault_recover_buf
global fault_recover_armed

fault_recover_armed: dq 0
fault_recover_buf:   times 8 dq 0

section .text

global fault_recover_set
fault_recover_set:
    ; Save callee-saved registers into fault_recover_buf
    lea rax, [rel fault_recover_buf]
    mov [rax + 0],  rbx
    mov [rax + 8],  rbp
    mov [rax + 16], r12
    mov [rax + 24], r13
    mov [rax + 32], r14
    mov [rax + 40], r15
    ; Save the caller's RSP (after we return, RSP will be RSP+8 because
    ; the call pushed return addr)
    lea rcx, [rsp + 8]
    mov [rax + 48], rcx
    ; Save return address
    mov rcx, [rsp]
    mov [rax + 56], rcx

    ; Arm recovery
    mov qword [rel fault_recover_armed], 1

    ; Return 0 (normal path)
    xor eax, eax
    ret

global fault_recover_clear
fault_recover_clear:
    mov qword [rel fault_recover_armed], 0
    ret

global fault_recover_jump
; fault_recover_jump(int return_value)
;   Called from the C exception handler to jump back to the save point.
;   RDI = return value (will be returned from fault_recover_set)
fault_recover_jump:
    ; Disarm
    mov qword [rel fault_recover_armed], 0

    ; Restore callee-saved registers
    lea rax, [rel fault_recover_buf]
    mov rbx, [rax + 0]
    mov rbp, [rax + 8]
    mov r12, [rax + 16]
    mov r13, [rax + 24]
    mov r14, [rax + 32]
    mov r15, [rax + 40]
    ; Restore RSP
    mov rsp, [rax + 48]
    ; Push the return address so 'ret' goes to the right place
    mov rcx, [rax + 56]
    push rcx

    ; Return value from RDI
    mov rax, rdi
    ret
