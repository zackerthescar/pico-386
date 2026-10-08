#ifndef _GFX_H
#define _GFX_H

#include <stdint.h>

/*
 * PICO-8 drawing on p8_ram (no VM types; the builtins parse arguments and
 * call these). Coordinates are integers that have already been floored.
 *
 * Draw state lives in RAM like in PICO-8:
 *   0x5F00-0x5F0F draw palette: low nibble = mapped color,
 *                 bit 4 = transparent (palt; spr/sspr/map only)
 *   0x5F10-0x5F1F screen palette (applied by the display)
 *   0x5F20-0x5F23 clip rectangle x0,y0,x1,y1 (x1/y1 exclusive)
 *   0x5F28/0x5F2A camera x/y (int16)
 */

#define GFX_TRANSPARENT 0x10

/* Fill the screen with color; reset the clip rectangle and the cursor. */
void gfx_cls(uint8_t color);

/* Pen drawing: camera, clip, draw palette and fill pattern apply (no
 * palt). color 0xXY: Y = color, X = color for fill pattern 1 bits.
 * Rectangles and ovals take corners in any order, inclusive. */
void gfx_pset(int32_t x, int32_t y, uint8_t color);
void gfx_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t color);
void gfx_rect(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t color, int fill);
void gfx_circ(int32_t cx, int32_t cy, int32_t r, uint8_t color, int fill);
void gfx_oval(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t color, int fill);
uint8_t gfx_pget(int32_t x, int32_t y);

/* Draw text at (x, y) with the PICO-8 font (camera, clip, draw palette).
 * Handles \n \r \t \b and \f<hex>. Returns the x after the last char
 * and the number of lines in *lines. */
int32_t gfx_print(const uint8_t *str, uint32_t len, int32_t x, int32_t y,
                  uint8_t color, int32_t *lines);

/* Scroll the screen up by rows; the bottom is filled with color 0. */
void gfx_scroll_up(int32_t rows);

/* Sprite sheet pixels (raw: no camera, clip or palette). 0 outside. */
uint8_t gfx_sget(int32_t x, int32_t y);
void gfx_sset(int32_t x, int32_t y, uint8_t color);

/* Map cells (128x64; rows 32-63 share memory with the sprite sheet). */
uint8_t gfx_mget(int32_t x, int32_t y);
void gfx_mset(int32_t x, int32_t y, uint8_t tile);

/* Sprite n at (x, y), w x h pixels, optionally flipped. */
void gfx_spr(int32_t n, int32_t x, int32_t y, int32_t w, int32_t h,
             int flip_x, int flip_y);

/* Stretch sheet rect (sx,sy,sw,sh) to screen rect (dx,dy,dw,dh).
 * Negative dw/dh flip that axis. */
void gfx_sspr(int32_t sx, int32_t sy, int32_t sw, int32_t sh,
              int32_t dx, int32_t dy, int32_t dw, int32_t dh,
              int flip_x, int flip_y);

/* Draw map cells (cx,cy,cw,ch) at screen (sx,sy). Tile 0 is never drawn.
 * layers != 0 draws only tiles with any of those sprite flags. */
void gfx_map(int32_t cx, int32_t cy, int32_t sx, int32_t sy,
             int32_t cw, int32_t ch, uint8_t layers);

/* pal() / palt() with no arguments. */
void gfx_reset_pal(void);
void gfx_reset_palt(void);

#endif
