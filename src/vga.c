#include "vga.h"

#define WIDTH   320
#define HEIGHT  200

void vga_init() {
    _asm {
        mov ax, 0x0013
        int 0x10
    }
}

void vga_ret() {
    _asm {
        mov ax, 0x0003
        int 0x10
    }
}

void vga_set(unsigned short x, unsigned short y, unsigned short v) {
    unsigned char *VGA = (unsigned char *) 0xA0000000L;
    unsigned short offset = x + WIDTH * y;
    VGA[offset] = v;
}