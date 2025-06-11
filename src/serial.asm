[BITS 32]
section .text

global _debug_serial_init
_debug_serial_init:
    push ebp
    mov ebp, esp
    
    ; Enable DLAB
    mov dx, 0x2FB
    mov al, 0x80
    out dx, al

    ; Set divisor to 1 (MSB 0 LSB 1) 
    mov dx, 0x2F8 ; 0x2F8 is LSB in DLAB
    mov al, 0x01
    out dx, al
    
    mov dx, 0x2F9 ; 0x2F9 is MSB in DLAB
    mov al, 0x00
    out dx, al
    
    ; Clear DLAB and set 8N1
    mov dx, 0x2FB
    mov al, 0x03
    out dx, al
    
    ; Enable FIFO and clear TX/RX
    mov dx, 0x2FA
    mov al, 0xC7
    out dx, al
    
    mov esp, ebp
    pop ebp
    ret

global _debug_serial_putchar
_debug_serial_putchar:
    push ebp
    mov ebp, esp
    
.wait_loop:
    mov dx, 0x2FD
    in al, dx
    test al, 0x20   ; Test if transmitter is ready
    jz .wait_loop   ; Jump if not ready
    
    mov dx, 0x2F8
    mov al, [ebp+8]
    out dx, al      ; Transmit
    
    mov esp, ebp
    pop ebp
    ret
