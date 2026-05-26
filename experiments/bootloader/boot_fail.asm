[org 0x7C00]
[bits 16]

%define COM1 0x3F8
%define DEBUG_EXIT 0xF4
%define EXIT_FAIL 0x11

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    call serial_init

    mov si, marker_start
    call serial_print

    mov si, marker_fail
    call serial_print

    mov al, EXIT_FAIL
    call debug_exit

serial_init:
    mov dx, COM1 + 1
    mov al, 0x00
    out dx, al

    mov dx, COM1 + 3
    mov al, 0x80
    out dx, al

    mov dx, COM1 + 0
    mov al, 0x03
    out dx, al
    mov dx, COM1 + 1
    mov al, 0x00
    out dx, al

    mov dx, COM1 + 3
    mov al, 0x03
    out dx, al

    mov dx, COM1 + 2
    mov al, 0xC7
    out dx, al

    mov dx, COM1 + 4
    mov al, 0x0B
    out dx, al
    ret

serial_print:
.next:
    lodsb
    test al, al
    jz .done
    call serial_out
    jmp .next
.done:
    ret

serial_out:
    push ax
.wait:
    mov dx, COM1 + 5
    in al, dx
    test al, 0x20
    jz .wait

    pop ax
    mov dx, COM1
    out dx, al
    ret

debug_exit:
    mov dx, DEBUG_EXIT
    out dx, al
    cli
.hang:
    hlt
    jmp .hang

marker_start db 'TEST:START:hello_boot_fail', 0x0D, 0x0A, 0
marker_fail db 'TEST:FAIL:hello_boot_fail:intentional-fixture', 0x0D, 0x0A, 0

times 510 - ($ - $$) db 0
dw 0xAA55
