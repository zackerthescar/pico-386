/*
 * test_vga_main.c - VGA Mode X blit test
 *
 * Fills PICO-8 RAM screen buffer with a deterministic color bar pattern,
 * blits to VGA via Mode X, then signals completion over serial.
 * An external harness captures the QEMU screendump and checks its hash.
 */
#include <string.h>
#include "pico386.h"
#include "vga.h"
#include "mem.h"

P8Ram p8_ram;

/*
 * Fill the 128x128 screen buffer (8192 bytes, 4bpp packed) with
 * 16 vertical color bars, 8 pixels wide each. Color i fills
 * columns [i*8 .. i*8+7]. Each byte holds 2 pixels: low nibble
 * is the left pixel, high nibble is the right.
 *
 * Since bar width (8) is even and aligned, each byte within a bar
 * has both nibbles set to the same color: (c << 4) | c.
 */
static void fill_color_bars(void) {
    int y, x;
    for (y = 0; y < 128; y++) {
        for (x = 0; x < 64; x++) {
            /* x is the byte index: pixels 2*x and 2*x+1 */
            unsigned char color = (x / 4) & 0x0F;  /* 8px wide bars */
            p8_ram.mem.screen[y * 64 + x] = (color << 4) | color;
        }
    }
}

void main(void) {
    debug_serial_init();
    debug_serial_print("VGA_TEST: starting\n");

    memset(&p8_ram, 0, sizeof(p8_ram));

    /* Set up default draw state */
    p8_ram.mem.draw.clip_x1 = 128;
    p8_ram.mem.draw.clip_y1 = 128;

    fill_color_bars();
    debug_serial_print("VGA_TEST: pattern filled\n");

    vga_init();
    debug_serial_print("VGA_TEST: mode x init done\n");

    vga_blit(p8_ram.mem.screen);
    vga_flip();
    debug_serial_print("VGA_TEST: blit+flip done\n");

    /* Signal that the frame is on screen and ready for capture */
    debug_serial_print("VGA_TEST: RENDER_COMPLETE\n");

    /* Hold the frame — the harness will screendump then quit QEMU */
    for (;;) {}
}
