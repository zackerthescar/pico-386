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
    unsigned char *VGA = (unsigned char *) 0xA0000L;
    unsigned short offset = (y << 8) + (y << 6) + x;
    VGA[offset] = v;
}

// Draw a white VGA box that will contain the actual game frame.
// It's a little small, but whatever. We can come back to scaling
// later, once we finish everything else.
void draw_bound_box() {
    int x = 0;
    int y = 0;
    for (x = 0; x < 129; x++) {
        vga_set(96 + x, 36, 0x0F);
    }
    for (x = 0; x < 129; x++) {
        vga_set(96 + x, 165, 0x0F);
    } 
    for (y = 0; y < 129; y++) {
        vga_set(96, 36 + y, 0x0F);
    }
    for (y = 0; y < 129; y++) {
        vga_set(225, 36 + y, 0x0F);
    }
}
