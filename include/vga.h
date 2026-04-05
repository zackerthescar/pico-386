#ifndef _VGA_H
#define _VGA_H

extern void vga_init(void);
extern void vga_ret(void);
extern void vga_blit(const void *screen_buf);
extern void vga_flip(void);
extern void vga_set_palette(const void *rgb6_table, unsigned int count);

extern const unsigned char p8_palette_rgb6[96];

#endif
