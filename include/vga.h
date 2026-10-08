#ifndef _VGA_H
#define _VGA_H

#include <stdint.h>

/*
 * Display: Mode X, 400 lines at 70 Hz, each row scanned 3 times (320 x 133
 * rows). The 128x128 PICO-8 screen shows as 256 x 384 (2 pixels wide, 3
 * scanlines tall): about square pixels on a 4:3 monitor.
 *
 * Three pages rotate (triple buffering): vga_blit() writes the next page,
 * vga_flip() makes it the displayed page at the next retrace. Neither waits
 * for retrace unless frames come faster than the display refreshes.
 */

#define VGA_PAGE_SIZE 10720u            /* 80 bytes x 134 rows (400 / 3) */
#define VGA_PAGES     3

extern void vga_init(void);
extern void vga_ret(void);
extern void vga_blit(const void *screen_buf);
extern void vga_flip(void);
extern void vga_set_palette(const void *rgb6_table, unsigned int count);

extern const unsigned char p8_palette_rgb6[96];

/* Show colors 0-15 through the PICO-8 screen palette (0x5F10). Cheap when
 * nothing changed. */
void vga_apply_screen_pal(const uint8_t screen_pal[16]);
void vga_screen_pal_to_rgb6(const uint8_t screen_pal[16], uint8_t out[16 * 3]);

/* Hardware primitives (vga.asm), used by vga_page.c. */
extern void vga_mode_init(void);
extern void vga_blit_page(const void *screen_buf, uint32_t page_offset);
extern void vga_show_page(uint32_t page_offset);
extern int vga_in_retrace(void);

#endif
