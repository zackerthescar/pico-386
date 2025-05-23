#include "serial.h"

void debug_serial_init(int p) {
    _asm {
        mov ax, 0x00E3
        mov dx, p
        int 0x14
    }
}

void debug_serial_putchar(int p, char c) {
    _asm {
        mov ah, 0x01
        mov al, c
        mov dx, p
        int 0x14
    }
}

void debug_serial_print(int p, const char* str) {
    while (*str) {
        debug_serial_putchar(p, *str);
        ++str;
    }
}