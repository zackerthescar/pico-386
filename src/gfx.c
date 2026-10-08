#include <string.h>
#include "gfx.h"
#include "mem.h"
#include "p8_font.h"

#define SHEET_W    128
#define SHEET_H    128
#define MAP_W      128
#define MAP_H      64
#define ROW_BYTES  64                   /* 128 px at 4 bpp */

/* Map rows 0-31 at 0x2000; rows 32-63 share 0x1000 with sprites 128-255. */
#define MAP_LO     0x2000
#define MAP_HI     0x1000

static uint8_t *map_cell(int32_t x, int32_t y) {
    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return 0;
    if (y < 32) return &p8_ram.raw[MAP_LO + y * MAP_W + x];
    return &p8_ram.raw[MAP_HI + (y - 32) * MAP_W + x];
}

static void put_pixel(int32_t x, int32_t y, uint8_t c) {
    uint8_t *d = &p8_ram.mem.screen[y * ROW_BYTES + (x >> 1)];
    if (x & 1) *d = (uint8_t)((*d & 0x0F) | (c << 4));
    else       *d = (uint8_t)((*d & 0xF0) | c);
}

static uint8_t sheet_pixel(int32_t x, int32_t y) {
    uint8_t b = p8_ram.mem.gfx[y * ROW_BYTES + (x >> 1)];
    return (x & 1) ? (uint8_t)(b >> 4) : (uint8_t)(b & 0x0F);
}

/* ── screen ─────────────────────────────────────────────────────────── */

void gfx_cls(uint8_t color) {
    P8DrawState *ds = &p8_ram.mem.draw;
    memset(p8_ram.mem.screen, (color & 0x0F) * 0x11, sizeof(p8_ram.mem.screen));
    /* cls() also resets the clip rectangle and the print cursor. */
    ds->clip_x0 = 0;
    ds->clip_y0 = 0;
    ds->clip_x1 = 128;
    ds->clip_y1 = 128;
    ds->cursor_x = 0;
    ds->cursor_y = 0;
}

/* ── pixels ─────────────────────────────────────────────────────────── */

/*
 * Pen drawing (pset, line, rect, circ, oval). A color byte 0xXY draws Y,
 * or X where the fill pattern (0x5F31, 4x4, bit 15 = top-left) has a 1 bit;
 * with the pattern's transparency flag (0x5F33 bit 0) those pixels are
 * left alone. Both colors go through the draw palette; palt does not
 * apply. Coordinates are in screen space after the camera.
 */
typedef struct {
    uint8_t c0, c1;         /* palette-mapped primary / secondary color */
    uint16_t pat;           /* fill pattern, 0 = solid */
    uint8_t transparent;    /* pattern 1 bits are not drawn */
} Pen;

static void pen_init(Pen *p, uint8_t color) {
    P8DrawState *ds = &p8_ram.mem.draw;
    p->c0 = (uint8_t)(ds->draw_pal[color & 0x0F] & 0x0F);
    p->c1 = (uint8_t)(ds->draw_pal[(color >> 4) & 0x0F] & 0x0F);
    p->pat = ds->fillp;
    p->transparent = (uint8_t)(ds->fillp_flags & 1);
}

static int in_clip(int32_t x, int32_t y) {
    P8DrawState *ds = &p8_ram.mem.draw;
    return x >= ds->clip_x0 && y >= ds->clip_y0 && x < ds->clip_x1 && y < ds->clip_y1;
}

/* One pixel in screen space, already clipped. */
static void pen_put(const Pen *p, int32_t x, int32_t y) {
    uint8_t c = p->c0;
    if (p->pat && ((p->pat >> (15 - ((y & 3) * 4 + (x & 3)))) & 1)) {
        if (p->transparent) return;
        c = p->c1;
    }
    put_pixel(x, y, c);
}

static void pen_point(const Pen *p, int32_t x, int32_t y) {
    if (in_clip(x, y)) pen_put(p, x, y);
}

/* Horizontal span [x0, x1] (inclusive, any order) on row y. */
static void pen_span(const Pen *p, int32_t x0, int32_t x1, int32_t y) {
    P8DrawState *ds = &p8_ram.mem.draw;
    int32_t x;
    if (x0 > x1) { x = x0; x0 = x1; x1 = x; }
    if (y < ds->clip_y0 || y >= ds->clip_y1) return;
    if (x0 < ds->clip_x0) x0 = ds->clip_x0;
    if (x1 >= ds->clip_x1) x1 = ds->clip_x1 - 1;
    if (x0 > x1) return;
    if (p->pat) {
        for (x = x0; x <= x1; x++) pen_put(p, x, y);
        return;
    }
    /* Solid: odd/even edge nibbles, whole bytes in between. */
    if (x0 & 1) put_pixel(x0++, y, p->c0);
    if (x0 <= x1 && !(x1 & 1)) put_pixel(x1--, y, p->c0);
    if (x0 < x1) {
        memset(&p8_ram.mem.screen[y * ROW_BYTES + (x0 >> 1)], p->c0 * 0x11,
               (size_t)((x1 - x0 + 1) >> 1));
    }
}

void gfx_pset(int32_t x, int32_t y, uint8_t color) {
    P8DrawState *ds = &p8_ram.mem.draw;
    Pen p;
    pen_init(&p, color);
    pen_point(&p, x - ds->camera_x, y - ds->camera_y);
}

void gfx_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t color) {
    P8DrawState *ds = &p8_ram.mem.draw;
    int32_t dx, dy, sx, sy, err;
    Pen p;
    pen_init(&p, color);
    x0 -= ds->camera_x; x1 -= ds->camera_x;
    y0 -= ds->camera_y; y1 -= ds->camera_y;
    if (y0 == y1) { pen_span(&p, x0, x1, y0); return; }
    /* Bresenham, both endpoints included. Always step in the same direction
     * so line(a, b) and line(b, a) draw the same pixels. */
    if (x0 > x1 || (x0 == x1 && y0 > y1)) {
        int32_t t = x0; x0 = x1; x1 = t;
        t = y0; y0 = y1; y1 = t;
    }
    dx = x1 > x0 ? x1 - x0 : x0 - x1;
    dy = y1 > y0 ? y0 - y1 : y1 - y0;   /* negative */
    sx = x0 < x1 ? 1 : -1;
    sy = y0 < y1 ? 1 : -1;
    err = dx + dy;
    for (;;) {
        int32_t e2;
        pen_point(&p, x0, y0);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void gfx_rect(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t color, int fill) {
    P8DrawState *ds = &p8_ram.mem.draw;
    int32_t y, t;
    Pen p;
    pen_init(&p, color);
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
    x0 -= ds->camera_x; x1 -= ds->camera_x;
    y0 -= ds->camera_y; y1 -= ds->camera_y;
    if (fill) {
        int32_t ya = y0 > ds->clip_y0 ? y0 : ds->clip_y0;
        int32_t yb = y1 < ds->clip_y1 - 1 ? y1 : ds->clip_y1 - 1;
        for (y = ya; y <= yb; y++) pen_span(&p, x0, x1, y);
        return;
    }
    pen_span(&p, x0, x1, y0);
    if (y1 != y0) pen_span(&p, x0, x1, y1);
    for (y = y0 + 1; y < y1; y++) {
        pen_point(&p, x0, y);
        if (x1 != x0) pen_point(&p, x1, y);
    }
}

void gfx_circ(int32_t cx, int32_t cy, int32_t r, uint8_t color, int fill) {
    P8DrawState *ds = &p8_ram.mem.draw;
    int32_t x, y, d;
    Pen p;
    if (r < 0) return;
    pen_init(&p, color);
    cx -= ds->camera_x;
    cy -= ds->camera_y;
    /* Midpoint circle; each step covers all 8 octants. */
    x = r; y = 0; d = 1 - r;
    while (x >= y) {
        if (fill) {
            pen_span(&p, cx - x, cx + x, cy + y);
            if (y) pen_span(&p, cx - x, cx + x, cy - y);
        } else {
            pen_point(&p, cx + x, cy + y); pen_point(&p, cx - x, cy + y);
            pen_point(&p, cx + x, cy - y); pen_point(&p, cx - x, cy - y);
            pen_point(&p, cx + y, cy + x); pen_point(&p, cx - y, cy + x);
            pen_point(&p, cx + y, cy - x); pen_point(&p, cx - y, cy - x);
        }
        y++;
        if (d < 0) {
            d += 2 * y + 1;
        } else {
            /* x shrinks: the rows at +-x are final, fill them now */
            if (fill && x != y - 1) {
                pen_span(&p, cx - (y - 1), cx + (y - 1), cy + x);
                pen_span(&p, cx - (y - 1), cx + (y - 1), cy - x);
            }
            x--;
            d += 2 * (y - x) + 1;
        }
    }
}

/* Integer square root (floor). */
static uint32_t isqrt32(uint32_t n) {
    uint32_t r = 0, bit = 1UL << 30;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= r + bit) { n -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

/*
 * Left/right x of an ellipse row. The ellipse is inscribed in the box's
 * pixel edges (x0 .. x1+1), so it touches all four sides, and a pixel is
 * inside when its center is. Doubled coordinates keep everything integer:
 * x2c/y2c = doubled center, w2/h2 = full width/height (doubled semi-axes).
 */
static void oval_row(int32_t x2c, int32_t y2c, int32_t w2, int32_t h2, int32_t y,
                     int32_t *left, int32_t *right) {
    int32_t dy = 2 * y + 1 - y2c;       /* doubled offset of the row center */
    /* hw = w2 * sqrt(1 - dy^2 / h2^2), doubled half width */
    uint32_t q = (uint32_t)(((long long)(h2 * h2 - dy * dy) << 16) / (h2 * h2));
    int32_t hw = (int32_t)(((long long)w2 * isqrt32(q)) >> 8);
    *left = (x2c - hw) >> 1;            /* ceil((x2c - hw - 1) / 2) */
    *right = (x2c + hw - 1) >> 1;       /* floor((x2c + hw - 1) / 2) */
}

void gfx_oval(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t color, int fill) {
    P8DrawState *ds = &p8_ram.mem.draw;
    int32_t t, y, x2c, y2c, w2, h2, cy2;
    Pen p;
    pen_init(&p, color);
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
    x0 -= ds->camera_x; x1 -= ds->camera_x;
    y0 -= ds->camera_y; y1 -= ds->camera_y;
    if (x1 - x0 > 0x3FFF || y1 - y0 > 0x3FFF) return;
    x2c = x0 + x1 + 1; y2c = y0 + y1 + 1;
    w2 = x1 - x0 + 1; h2 = y1 - y0 + 1;
    cy2 = y0 + y1;                      /* 2 * center row */
    for (y = y0; y <= y1; y++) {
        int32_t l, r, nl, nr;
        if (y < ds->clip_y0 || y >= ds->clip_y1) continue;
        oval_row(x2c, y2c, w2, h2, y, &l, &r);
        if (fill) { pen_span(&p, l, r, y); continue; }
        /* Outline: the pixels of this row that the next row outward does not
         * cover (the whole row at the top and bottom), plus both ends. */
        if (y == y0 || y == y1) {
            pen_span(&p, l, r, y);
            continue;
        }
        oval_row(x2c, y2c, w2, h2, 2 * y < cy2 ? y - 1 : y + 1, &nl, &nr);
        pen_span(&p, l, nl - 1 > l ? nl - 1 : l, y);
        pen_span(&p, nr + 1 < r ? nr + 1 : r, r, y);
    }
}

uint8_t gfx_pget(int32_t x, int32_t y) {
    P8DrawState *ds = &p8_ram.mem.draw;
    uint8_t b;
    x -= ds->camera_x;
    y -= ds->camera_y;
    if (x < 0 || y < 0 || x >= 128 || y >= 128) return 0;
    b = p8_ram.mem.screen[y * ROW_BYTES + (x >> 1)];
    return (x & 1) ? (uint8_t)(b >> 4) : (uint8_t)(b & 0x0F);
}

uint8_t gfx_sget(int32_t x, int32_t y) {
    if (x < 0 || y < 0 || x >= SHEET_W || y >= SHEET_H) return 0;
    return sheet_pixel(x, y);
}

/* Raw write, as fake-08 (zepto8 maps through the draw palette; PICO-8's
 * behavior is unconfirmed). */
void gfx_sset(int32_t x, int32_t y, uint8_t color) {
    uint8_t *d;
    if (x < 0 || y < 0 || x >= SHEET_W || y >= SHEET_H) return;
    color &= 0x0F;
    d = &p8_ram.mem.gfx[y * ROW_BYTES + (x >> 1)];
    if (x & 1) *d = (uint8_t)((*d & 0x0F) | (color << 4));
    else       *d = (uint8_t)((*d & 0xF0) | color);
}

uint8_t gfx_mget(int32_t x, int32_t y) {
    uint8_t *c = map_cell(x, y);
    return c ? *c : 0;
}

void gfx_mset(int32_t x, int32_t y, uint8_t tile) {
    uint8_t *c = map_cell(x, y);
    if (c) *c = tile;
}

/* ── text ───────────────────────────────────────────────────────────── */

#define CHAR_W      4                   /* narrow glyph advance */
#define WIDE_W      8                   /* glyphs 0x80-0xFF */
#define LINE_H      6

static void draw_glyph(const Pen *p, uint8_t ch, int32_t x, int32_t y) {
    const unsigned char *rows;
    int r, b, w;
    if (ch >= 0x80) { rows = p8_font_wide[ch - 0x80]; w = 7; }
    else if (ch >= 0x20) { rows = p8_font_narrow[ch - 0x20]; w = 3; }
    else return;
    for (r = 0; r < 5; r++) {
        uint8_t bits = rows[r];
        for (b = 0; b < w && bits; b++, bits >>= 1) {
            if (bits & 1) pen_point(p, x + b, y + r);
        }
    }
}

static int hex_digit(uint8_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int32_t gfx_print(const uint8_t *str, uint32_t len, int32_t x, int32_t y,
                  uint8_t color, int32_t *lines) {
    P8DrawState *ds = &p8_ram.mem.draw;
    int32_t cx, cy, x0;
    uint32_t i;
    Pen p;
    pen_init(&p, color);
    p.pat = 0;                          /* print ignores the fill pattern */
    x0 = cx = x - ds->camera_x;
    cy = y - ds->camera_y;
    *lines = 1;
    for (i = 0; i < len; i++) {
        uint8_t ch = str[i];
        switch (ch) {
        case '\n': cx = x0; cy += LINE_H; (*lines)++; break;
        case '\r': cx = x0; break;
        case '\t': cx = x0 + ((cx - x0) / 16 + 1) * 16; break;
        case '\b': cx -= CHAR_W; break;
        case 0x0C:                      /* \f<hex>: foreground color */
            if (i + 1 < len && hex_digit(str[i + 1]) >= 0) {
                p.c0 = (uint8_t)(ds->draw_pal[hex_digit(str[++i])] & 0x0F);
            }
            break;
        default:
            if (ch < 0x10) break;       /* other control codes: not yet */
            draw_glyph(&p, ch, cx, cy);
            cx += ch >= 0x80 ? WIDE_W : CHAR_W;
            break;
        }
    }
    return cx + ds->camera_x;
}

void gfx_scroll_up(int32_t rows) {
    if (rows <= 0) return;
    if (rows > 128) rows = 128;
    memmove(p8_ram.mem.screen, &p8_ram.mem.screen[rows * ROW_BYTES],
            (size_t)((128 - rows) * ROW_BYTES));
    memset(&p8_ram.mem.screen[(128 - rows) * ROW_BYTES], 0, (size_t)(rows * ROW_BYTES));
}

/* ── sprite blit ────────────────────────────────────────────────────── */

/*
 * Byte tables for the unscaled fast path: for a source byte b (two pixels,
 * low nibble = left), pair_val[b] holds both colors after the draw palette
 * and pair_keep[b] masks the screen nibbles to keep (transparent pixels).
 * One screen byte is then `(d & pair_keep[b]) | pair_val[b]`.
 */
static uint8_t pair_val[256];
static uint8_t pair_keep[256];
static uint8_t pair_lut[16];            /* lut the tables were built for */
static int pair_valid;

static void build_pair_tables(const uint8_t lut[16]) {
    int b;
    if (pair_valid && memcmp(pair_lut, lut, 16) == 0) return;
    for (b = 0; b < 256; b++) {
        uint8_t lo = lut[b & 0x0F], hi = lut[b >> 4];
        uint8_t val = 0, keep = 0xFF;
        if (!(lo & GFX_TRANSPARENT)) { val |= lo; keep &= 0xF0; }
        if (!(hi & GFX_TRANSPARENT)) { val |= (uint8_t)(hi << 4); keep &= 0x0F; }
        pair_val[b] = val;
        pair_keep[b] = keep;
    }
    memcpy(pair_lut, lut, 16);
    pair_valid = 1;
}

/* Draw palette as used by sprites: low nibble = color, bit 4 = skip.
 * Also brings the fast-path byte tables up to date. */
static void sprite_lut(uint8_t lut[16]) {
    int i;
    for (i = 0; i < 16; i++) {
        lut[i] = (uint8_t)(p8_ram.mem.draw.draw_pal[i] & (0x0F | GFX_TRANSPARENT));
    }
    build_pair_tables(lut);
}

/* One pixel through the sprite lut (fast-path row ends). */
static void plot_lut(uint8_t *drow, int32_t x, const uint8_t *srow, int32_t u,
                     const uint8_t lut[16]) {
    uint8_t s = srow[u >> 1];
    uint8_t c = lut[(u & 1) ? (s >> 4) : (s & 0x0F)];
    uint8_t *d = &drow[x >> 1];
    if (c & GFX_TRANSPARENT) return;
    if (x & 1) *d = (uint8_t)((*d & 0x0F) | (c << 4));
    else       *d = (uint8_t)((*d & 0xF0) | c);
}

/*
 * Unscaled row: screen x in [x0,x1) from sheet row srow, starting at source
 * pixel u (u + 1 for each x). Whole screen bytes go through the pair tables;
 * if u is odd the source byte is assembled from two neighbors.
 */
static void row_copy(uint8_t *drow, const uint8_t *srow, int32_t x0, int32_t x1,
                     int32_t u, const uint8_t lut[16]) {
    uint8_t *d;
    const uint8_t *s;
    int32_t n;
    if (x0 & 1) plot_lut(drow, x0++, srow, u++, lut);
    n = (x1 - x0) >> 1;
    d = &drow[x0 >> 1];
    s = &srow[u >> 1];
    if (n > 0) {
        uint8_t *dend = d + n;
        if (!(u & 1)) {
            do {
                uint8_t b = *s++;
                *d = (uint8_t)((*d & pair_keep[b]) | pair_val[b]);
            } while (++d < dend);
        } else {
            do {
                uint8_t b = (uint8_t)((s[0] >> 4) | (s[1] << 4));
                s++;
                *d = (uint8_t)((*d & pair_keep[b]) | pair_val[b]);
            } while (++d < dend);
        }
    }
    if ((x1 - x0) & 1) plot_lut(drow, x1 - 1, srow, u + (x1 - 1 - x0), lut);
}

/* As row_copy, but flipped: source pixel u for x0, u - 1 for x0 + 1, ... */
static void row_copy_flip(uint8_t *drow, const uint8_t *srow, int32_t x0, int32_t x1,
                          int32_t u, const uint8_t lut[16]) {
    uint8_t *d;
    const uint8_t *s;
    int32_t n;
    if (x0 & 1) plot_lut(drow, x0++, srow, u--, lut);
    n = (x1 - x0) >> 1;
    d = &drow[x0 >> 1];
    s = &srow[u >> 1];
    if (n > 0) {
        uint8_t *dend = d + n;
        if (u & 1) {
            /* pixels u and u-1 share a byte: swap its nibbles */
            do {
                uint8_t c = *s--;
                uint8_t b = (uint8_t)((c >> 4) | (c << 4));
                *d = (uint8_t)((*d & pair_keep[b]) | pair_val[b]);
            } while (++d < dend);
        } else {
            /* pixel u = low nibble here, u-1 = high nibble of the byte before */
            do {
                uint8_t b = (uint8_t)((s[0] & 0x0F) | (s[-1] & 0xF0));
                s--;
                *d = (uint8_t)((*d & pair_keep[b]) | pair_val[b]);
            } while (++d < dend);
        }
    }
    if ((x1 - x0) & 1) plot_lut(drow, x1 - 1, srow, u - (x1 - 1 - x0), lut);
}

/* One screen byte from source byte b through the pair tables. */
#define PAIR(d, b) ((d) = (uint8_t)(((d) & pair_keep[b]) | pair_val[b]))

/*
 * 8x8 sprite at an even screen x, unflipped, fully inside the clip: the
 * common map-tile case. d and s point at the first byte of the top row
 * (screen and sheet rows are both 64 bytes).
 */
static void tile8_even(uint8_t *d, const uint8_t *s) {
    int r;
    for (r = 0; r < 8; r++, d += ROW_BYTES, s += ROW_BYTES) {
        PAIR(d[0], s[0]);
        PAIR(d[1], s[1]);
        PAIR(d[2], s[2]);
        PAIR(d[3], s[3]);
    }
}

/* Tile fast case: 8x8, x even, unflipped, inside the clip rect. */
static int tile8_fits(int32_t x, int32_t y) {
    P8DrawState *ds = &p8_ram.mem.draw;
    return !(x & 1) && x >= ds->clip_x0 && y >= ds->clip_y0 &&
           x + 8 <= ds->clip_x1 && y + 8 <= ds->clip_y1;
}

/* Exact source coordinate stepping: src = sw * i / dw (integer division,
 * as zepto8's sspr), kept as quotient + remainder so the loop needs no
 * division per pixel. */
typedef struct {
    int32_t q, r;           /* current sw*i/dw and remainder */
    int32_t step_q, step_r; /* sw/dw and sw%dw */
    int32_t den;
} Stepper;

/* num <= 0x7FFF; start >= 0. */
static void stepper_init(Stepper *s, int32_t num, int32_t den, int32_t start) {
    s->step_q = num / den;
    s->step_r = num % den;
    s->den = den;
    if (start <= 0x7FFF) {              /* num * start < 2^30: 32-bit is exact */
        s->q = num * start / den;
        s->r = num * start % den;
    } else {
        s->q = (int32_t)(((long long)num * start) / den);
        s->r = (int32_t)(((long long)num * start) % den);
    }
}

static void stepper_next(Stepper *s) {
    s->q += s->step_q;
    s->r += s->step_r;
    if (s->r >= s->den) {
        s->r -= s->den;
        s->q++;
    }
}

/*
 * Copy sheet rect (sx,sy,sw,sh) to screen rect (dx,dy,dw,dh), after the
 * camera. The destination is clipped once, then the row loop runs with no
 * bounds checks. Unscaled copies from inside the sheet (all of spr and map)
 * take the byte-pair fast path; the rest steps per pixel. Source pixels
 * outside the sheet read as color 0.
 */
static void blit(int32_t sx, int32_t sy, int32_t sw, int32_t sh,
                 int32_t dx, int32_t dy, int32_t dw, int32_t dh,
                 int flip_x, int flip_y, const uint8_t lut[16]) {
    P8DrawState *ds = &p8_ram.mem.draw;
    int32_t x0, y0, x1, y1, x, y;
    Stepper su0, sv;
    int src_inside;

    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    if (sw > 0x7FFF || sh > 0x7FFF) return;

    x0 = dx > ds->clip_x0 ? dx : ds->clip_x0;
    y0 = dy > ds->clip_y0 ? dy : ds->clip_y0;
    x1 = dx + dw < ds->clip_x1 ? dx + dw : ds->clip_x1;
    y1 = dy + dh < ds->clip_y1 ? dy + dh : ds->clip_y1;
    if (x0 >= x1 || y0 >= y1) return;

    src_inside = sx >= 0 && sy >= 0 && sx + sw <= SHEET_W && sy + sh <= SHEET_H;

    if (sw == dw && sh == dh && src_inside) {
        for (y = y0; y < y1; y++) {
            int32_t v = y - dy;
            uint8_t *drow = &p8_ram.mem.screen[y * ROW_BYTES];
            const uint8_t *srow;
            if (flip_y) v = sh - 1 - v;
            srow = &p8_ram.mem.gfx[(sy + v) * ROW_BYTES];
            if (flip_x) row_copy_flip(drow, srow, x0, x1, sx + sw - 1 - (x0 - dx), lut);
            else        row_copy(drow, srow, x0, x1, sx + (x0 - dx), lut);
        }
        return;
    }

    stepper_init(&su0, sw, dw, x0 - dx);
    stepper_init(&sv, sh, dh, y0 - dy);

    for (y = y0; y < y1; y++, stepper_next(&sv)) {
        int32_t v = sv.q;
        Stepper su = su0;
        uint8_t *row = &p8_ram.mem.screen[y * ROW_BYTES];
        if (flip_y) v = sh - 1 - v;
        v += sy;

        for (x = x0; x < x1; x++, stepper_next(&su)) {
            int32_t u = su.q;
            uint8_t c;
            if (flip_x) u = sw - 1 - u;
            u += sx;
            c = src_inside ? sheet_pixel(u, v) : gfx_sget(u, v);
            c = lut[c];
            if (c & GFX_TRANSPARENT) continue;
            if (x & 1) row[x >> 1] = (uint8_t)((row[x >> 1] & 0x0F) | (c << 4));
            else       row[x >> 1] = (uint8_t)((row[x >> 1] & 0xF0) | c);
        }
    }
}

void gfx_spr(int32_t n, int32_t x, int32_t y, int32_t w, int32_t h,
             int flip_x, int flip_y) {
    uint8_t lut[16];
    if (n < 0 || n > 255) return;
    sprite_lut(lut);
    x -= p8_ram.mem.draw.camera_x;
    y -= p8_ram.mem.draw.camera_y;
    if (w == 8 && h == 8 && !flip_x && !flip_y && tile8_fits(x, y)) {
        tile8_even(&p8_ram.mem.screen[y * ROW_BYTES + (x >> 1)],
                   &p8_ram.mem.gfx[(n >> 4) * 8 * ROW_BYTES + (n & 15) * 4]);
        return;
    }
    blit((n & 15) * 8, (n >> 4) * 8, w, h, x, y, w, h, flip_x, flip_y, lut);
}

void gfx_sspr(int32_t sx, int32_t sy, int32_t sw, int32_t sh,
              int32_t dx, int32_t dy, int32_t dw, int32_t dh,
              int flip_x, int flip_y) {
    uint8_t lut[16];
    /* Negative size flips; the pixel at dx/dy stays the far edge. */
    if (dw < 0) { dw = -dw; dx -= dw - 1; flip_x = !flip_x; }
    if (dh < 0) { dh = -dh; dy -= dh - 1; flip_y = !flip_y; }
    sprite_lut(lut);
    blit(sx, sy, sw, sh,
         dx - p8_ram.mem.draw.camera_x, dy - p8_ram.mem.draw.camera_y, dw, dh,
         flip_x, flip_y, lut);
}

/* ── map ────────────────────────────────────────────────────────────── */

/* A tile is drawn when any of its sprite flags is in layers (fake-08 and
 * zepto8 agree). */
static int layer_match(uint8_t tile, uint8_t layers) {
    return layers == 0 || (p8_ram.mem.gfx_flags[tile] & layers) != 0;
}

void gfx_map(int32_t cx, int32_t cy, int32_t sx, int32_t sy,
             int32_t cw, int32_t ch, uint8_t layers) {
    P8DrawState *ds = &p8_ram.mem.draw;
    uint8_t lut[16];
    int32_t i, j;

    sprite_lut(lut);
    sx -= ds->camera_x;
    sy -= ds->camera_y;
    for (j = 0; j < ch; j++) {
        int32_t ty = sy + j * 8;
        if (ty + 8 <= ds->clip_y0 || ty >= ds->clip_y1) continue;
        for (i = 0; i < cw; i++) {
            int32_t tx = sx + i * 8;
            uint8_t tile;
            if (tx + 8 <= ds->clip_x0 || tx >= ds->clip_x1) continue;
            tile = gfx_mget(cx + i, cy + j);
            if (tile == 0 || !layer_match(tile, layers)) continue;
            if (tile8_fits(tx, ty)) {
                tile8_even(&p8_ram.mem.screen[ty * ROW_BYTES + (tx >> 1)],
                           &p8_ram.mem.gfx[(tile >> 4) * 8 * ROW_BYTES + (tile & 15) * 4]);
            } else {
                blit((tile & 15) * 8, (tile >> 4) * 8, 8, 8, tx, ty, 8, 8, 0, 0, lut);
            }
        }
    }
}

/* ── palettes ───────────────────────────────────────────────────────── */

void gfx_reset_palt(void) {
    int i;
    for (i = 0; i < 16; i++) {
        p8_ram.mem.draw.draw_pal[i] &= 0x0F;
    }
    p8_ram.mem.draw.draw_pal[0] |= GFX_TRANSPARENT;
}

/* pal(): draw + screen palettes, transparency, secondary palette
 * (0x5F60-0x5F6F) and fill pattern, as zepto8. */
void gfx_reset_pal(void) {
    int i;
    for (i = 0; i < 16; i++) {
        p8_ram.mem.draw.draw_pal[i] = (uint8_t)i;
        p8_ram.mem.draw.screen_pal[i] = (uint8_t)i;
        p8_ram.raw[0x5F60 + i] = 0;
    }
    p8_ram.mem.draw.fillp = 0;
    p8_ram.mem.draw.fillp_flags = 0;
    gfx_reset_palt();
}
