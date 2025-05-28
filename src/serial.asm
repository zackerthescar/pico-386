BITS 16
SECTION .text

global _debug_serial_init

_debug_serial_init:
    mov dx, 0x2F8 + 3
    mov al, 0x80
    out dx, al

    mov dx, 0x2F8
    mov al, 0x01
    out dx, al

    mov dx, 0x2F9
    mov al, 0x00
    out dx, al

    mov dx, 0x2F8 + 3
    mov al, 0x02
    out dx, al

    ret

global _debug_serial_putchar

_debug_serial_putchar:
    push ax

    mov dx, 0x2F8 + 5
.wait:
    in al, dx
    test al, 0x20
    jz .wait

    pop ax
    mov dx, 0x2F8
    out dx, al
    ret
