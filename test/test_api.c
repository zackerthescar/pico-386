/*
 * Spec-based tests for the newer PICO-8 builtins (shapes, fill patterns,
 * print, type/unpack/pack/select/split, raw*, memcpy/memset, reload/cstore,
 * time/stat/flip). Included after test_gfx.c in the same translation unit.
 * Uses BNUM and the gfx_t_* helpers from there.
 */
#include "p8_font.h"
#include "p386_obj.h"

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

static void api_reset(void)
{
    gfx_t_reset();
    p8_ram.mem.draw.fillp = 0;
    p8_ram.mem.draw.fillp_flags = 0;
    p8_ram.mem.draw.cursor_x = 0;
    p8_ram.mem.draw.cursor_y = 0;
    p8_ram.mem.draw.pen_color = 7;
}

static void api_setnum(P386Value *a, int i, int n)
{
    a[i].value = (int32_t)((uint32_t)n << 16);
    a[i].tag = P386_TAG_NUM;
}

static void api_setstr(P386Value *a, int i, const char *s)
{
    P386String *p = p386_string_intern(s, (uint32_t)strlen(s));
    a[i].value = (int32_t)(uintptr_t)p;
    a[i].tag = P386_TAG_STR;
}

static void api_settab(P386Value *a, int i, P386Table *t)
{
    a[i].value = (int32_t)(uintptr_t)t;
    a[i].tag = P386_TAG_TAB;
}

static void api_setnil(P386Value *a, int i)
{
    a[i].value = 0;
    a[i].tag = P386_TAG_NIL;
}

/* 1 if a[i] is the interned string s */
static int api_is_str(const P386Value *v, const char *s)
{
    P386String *p;
    if (v->tag != P386_TAG_STR) return 0;
    p = p386_string_intern(s, (uint32_t)strlen(s));
    return v->value == (int32_t)(uintptr_t)p;
}

static int api_is_num(const P386Value *v, int n)
{
    return v->tag == P386_TAG_NUM && v->value == (int32_t)((uint32_t)n << 16);
}

static P386Table *api_tab_of(const P386Value *v)
{
    return (P386Table *)(uintptr_t)v->value;
}

static void api_tget_i(P386Table *t, int i, P386Value *out)
{
    P386Value k;
    api_setnum(&k, 0, i);
    p386_table_get(t, &k, out);
}

static void api_tget_s(P386Table *t, const char *s, P386Value *out)
{
    P386Value k;
    api_setstr(&k, 0, s);
    p386_table_get(t, &k, out);
}

static void api_tset_i(P386Table *t, int i, const P386Value *v)
{
    P386Value k;
    api_setnum(&k, 0, i);
    p386_table_set(t, &k, v);
}

/* Table of numbers 1..n with the given values */
static P386Table *api_mk_numtab(const int *vals, int n)
{
    P386Table *t = p386_table_new(0, 0);
    P386Value v;
    int i;
    for (i = 0; i < n; i++) {
        api_setnum(&v, 0, vals[i]);
        api_tset_i(t, i + 1, &v);
    }
    return t;
}

/* Call a builtin with up to 5 numeric arguments (first nargs used). */
static int api_run(P386CFunc f, P386VMState *vm, int nargs,
                   int v0, int v1, int v2, int v3, int v4)
{
    P386Value a[16];
    int vals[5];
    int i;
    vals[0] = v0; vals[1] = v1; vals[2] = v2; vals[3] = v3; vals[4] = v4;
    for (i = 0; i < nargs; i++) api_setnum(a, i, vals[i]);
    return f(vm, a, (uint8_t)nargs, 0);
}

/* Bounding box of all pixels of color c; returns pixel count. */
static int api_bbox(int c, int *bx0, int *by0, int *bx1, int *by1)
{
    int x, y, n = 0;
    *bx0 = 999; *by0 = 999; *bx1 = -1; *by1 = -1;
    for (y = 0; y < 128; y++)
        for (x = 0; x < 128; x++)
            if (gfx_t_scr(x, y) == c) {
                n++;
                if (x < *bx0) *bx0 = x;
                if (x > *bx1) *bx1 = x;
                if (y < *by0) *by0 = y;
                if (y > *by1) *by1 = y;
            }
    return n;
}

/* 1 if screen is mirror-symmetric: x <-> sx - x and y <-> sy - y */
static int api_symmetric(int sx, int sy)
{
    int x, y, xm, ym;
    for (y = 0; y < 128; y++)
        for (x = 0; x < 128; x++) {
            xm = sx - x;
            ym = sy - y;
            if (xm >= 0 && xm < 128 && gfx_t_scr(x, y) != gfx_t_scr(xm, y))
                return 0;
            if (ym >= 0 && ym < 128 && gfx_t_scr(x, y) != gfx_t_scr(x, ym))
                return 0;
        }
    return 1;
}

/* Draw one of 8 shape kinds in a fixed place. */
static void api_shape(int k, int c)
{
    switch (k) {
    case 0: gfx_pset(40, 40, (uint8_t)c); break;
    case 1: gfx_line(30, 30, 50, 35, (uint8_t)c); break;
    case 2: gfx_rect(30, 30, 50, 40, (uint8_t)c, 0); break;
    case 3: gfx_rect(30, 30, 50, 40, (uint8_t)c, 1); break;
    case 4: gfx_circ(64, 64, 8, (uint8_t)c, 0); break;
    case 5: gfx_circ(64, 64, 8, (uint8_t)c, 1); break;
    case 6: gfx_oval(20, 20, 60, 40, (uint8_t)c, 0); break;
    default: gfx_oval(20, 20, 60, 40, (uint8_t)c, 1); break;
    }
}

/*
 * Check a font glyph cell at (x,y): glyph pixels are color c, every other
 * pixel of the cell (4x6 narrow, 8x6 wide) is bg.
 */
static int api_glyph_ok(int code, int x, int y, int c, int bg)
{
    int wide = code >= 0x80;
    int cw = wide ? 8 : 4;
    int gw = wide ? 7 : 3;
    int r, b, bits, want;
    for (r = 0; r < 6; r++)
        for (b = 0; b < cw; b++) {
            bits = 0;
            if (r < 5 && b < gw)
                bits = wide ? p8_font_wide[code - 0x80][r]
                            : p8_font_narrow[code - 0x20][r];
            want = ((bits >> b) & 1) ? c : bg;
            if (b < gw && r < 5) {
                if (gfx_t_scr(x + b, y + r) != want) return 0;
            } else {
                if (gfx_t_scr(x + b, y + r) != bg) return 0;
            }
        }
    return 1;
}

/* Number of set glyph pixels (for sanity: glyph is not blank) */
static int api_glyph_pixels(int code)
{
    int wide = code >= 0x80;
    int gw = wide ? 7 : 3;
    int r, b, n = 0, bits;
    for (r = 0; r < 5; r++) {
        bits = wide ? p8_font_wide[code - 0x80][r]
                    : p8_font_narrow[code - 0x20][r];
        for (b = 0; b < gw; b++)
            if ((bits >> b) & 1) n++;
    }
    return n;
}

static void api_print_at(P386VMState *vm, const char *s, int x, int y, int c)
{
    P386Value a[8];
    api_setstr(a, 0, s);
    api_setnum(a, 1, x);
    api_setnum(a, 2, y);
    api_setnum(a, 3, c);
    p386_builtin_print(vm, a, 4, 0);
}

/* ------------------------------------------------------------------ */
/* shapes                                                             */
/* ------------------------------------------------------------------ */

TEST(api_line_horizontal_vertical) {
    api_reset();
    gfx_line(10, 20, 15, 20, 7);
    ASSERT_TRUE(gfx_t_rect(10, 20, 16, 21, 7));
    ASSERT_EQ(6, gfx_t_count(7));
    ASSERT_EQ(0, gfx_t_scr(9, 20));
    ASSERT_EQ(0, gfx_t_scr(16, 20));
    gfx_t_fill_scr(0);
    gfx_line(30, 50, 30, 41, 9);
    ASSERT_TRUE(gfx_t_rect(30, 41, 31, 51, 9));
    ASSERT_EQ(10, gfx_t_count(9));
    ASSERT_EQ(0, gfx_t_scr(30, 40));
    ASSERT_EQ(0, gfx_t_scr(30, 51));
    PASS();
}

TEST(api_line_diagonal_and_point) {
    int i;
    api_reset();
    gfx_line(5, 5, 9, 9, 7);
    for (i = 0; i < 5; i++) ASSERT_EQ(7, gfx_t_scr(5 + i, 5 + i));
    ASSERT_EQ(5, gfx_t_count(7));
    gfx_t_fill_scr(0);
    gfx_line(20, 10, 16, 14, 8);
    for (i = 0; i < 5; i++) ASSERT_EQ(8, gfx_t_scr(20 - i, 10 + i));
    ASSERT_EQ(5, gfx_t_count(8));
    gfx_t_fill_scr(0);
    gfx_line(3, 3, 3, 3, 6);
    ASSERT_EQ(1, gfx_t_count(6));
    ASSERT_EQ(6, gfx_t_scr(3, 3));
    PASS();
}

TEST(api_line_reversed_same_pixels) {
    unsigned char snap[0x2000];
    api_reset();
    gfx_line(10, 20, 40, 25, 7);
    memcpy(snap, p8_ram.mem.screen, 0x2000);
    ASSERT_EQ(1, gfx_t_scr(10, 20) == 7);
    ASSERT_EQ(1, gfx_t_scr(40, 25) == 7);
    gfx_t_fill_scr(0);
    gfx_line(40, 25, 10, 20, 7);
    ASSERT_EQ(0, memcmp(snap, p8_ram.mem.screen, 0x2000));
    PASS();
}

TEST(api_line_shallow_one_pixel_per_column) {
    int x, y, n;
    api_reset();
    gfx_line(0, 0, 10, 3, 7);
    ASSERT_EQ(7, gfx_t_scr(0, 0));
    ASSERT_EQ(7, gfx_t_scr(10, 3));
    ASSERT_EQ(11, gfx_t_count(7));
    for (x = 0; x <= 10; x++) {
        n = 0;
        for (y = 0; y < 128; y++)
            if (gfx_t_scr(x, y) == 7) n++;
        ASSERT_EQ(1, n);
    }
    PASS();
}

TEST(api_rect_outline) {
    api_reset();
    gfx_rect(10, 10, 19, 14, 7, 0);
    ASSERT_TRUE(gfx_t_rect(10, 10, 20, 11, 7));
    ASSERT_TRUE(gfx_t_rect(10, 14, 20, 15, 7));
    ASSERT_TRUE(gfx_t_rect(10, 10, 11, 15, 7));
    ASSERT_TRUE(gfx_t_rect(19, 10, 20, 15, 7));
    ASSERT_TRUE(gfx_t_rect(11, 11, 19, 14, 0));
    ASSERT_EQ(26, gfx_t_count(7));
    ASSERT_EQ(0, gfx_t_scr(9, 10));
    ASSERT_EQ(0, gfx_t_scr(20, 14));
    ASSERT_EQ(0, gfx_t_scr(10, 15));
    ASSERT_EQ(0, gfx_t_scr(19, 9));
    PASS();
}

TEST(api_rect_filled_any_order) {
    unsigned char snap[0x2000];
    api_reset();
    gfx_rect(10, 10, 19, 14, 7, 1);
    ASSERT_TRUE(gfx_t_rect(10, 10, 20, 15, 7));
    ASSERT_EQ(50, gfx_t_count(7));      /* 10 x 5 */
    memcpy(snap, p8_ram.mem.screen, 0x2000);
    gfx_t_fill_scr(0);
    gfx_rect(19, 14, 10, 10, 7, 1);
    ASSERT_EQ(0, memcmp(snap, p8_ram.mem.screen, 0x2000));
    gfx_t_fill_scr(0);
    gfx_rect(19, 10, 10, 14, 7, 0);
    ASSERT_EQ(26, gfx_t_count(7));
    PASS();
}

TEST(api_rect_degenerate) {
    api_reset();
    gfx_rect(5, 5, 5, 5, 7, 0);
    ASSERT_EQ(1, gfx_t_count(7));
    ASSERT_EQ(7, gfx_t_scr(5, 5));
    gfx_t_fill_scr(0);
    gfx_rect(5, 5, 5, 5, 7, 1);
    ASSERT_EQ(1, gfx_t_count(7));
    gfx_t_fill_scr(0);
    gfx_rect(5, 5, 9, 5, 7, 1);
    ASSERT_TRUE(gfx_t_rect(5, 5, 10, 6, 7));
    ASSERT_EQ(5, gfx_t_count(7));
    PASS();
}

TEST(api_circ_radius_zero) {
    api_reset();
    gfx_circ(50, 60, 0, 7, 0);
    ASSERT_EQ(7, gfx_t_scr(50, 60));
    ASSERT_EQ(1, gfx_t_count(7));
    gfx_t_fill_scr(0);
    gfx_circ(50, 60, 0, 7, 1);
    ASSERT_EQ(7, gfx_t_scr(50, 60));
    ASSERT_EQ(1, gfx_t_count(7));
    PASS();
}

TEST(api_circ_outline) {
    int x0, y0, x1, y1;
    api_reset();
    gfx_circ(64, 64, 10, 7, 0);
    ASSERT_EQ(7, gfx_t_scr(74, 64));
    ASSERT_EQ(7, gfx_t_scr(54, 64));
    ASSERT_EQ(7, gfx_t_scr(64, 74));
    ASSERT_EQ(7, gfx_t_scr(64, 54));
    ASSERT_EQ(0, gfx_t_scr(64, 64));
    ASSERT_EQ(0, gfx_t_scr(66, 66));
    ASSERT_TRUE(gfx_t_count(7) > 30);
    ASSERT_TRUE(api_bbox(7, &x0, &y0, &x1, &y1) > 0);
    ASSERT_EQ(54, x0);
    ASSERT_EQ(74, x1);
    ASSERT_EQ(54, y0);
    ASSERT_EQ(74, y1);
    ASSERT_TRUE(api_symmetric(128, 128));
    ASSERT_EQ(0, gfx_t_scr(74, 74));
    PASS();
}

TEST(api_circ_small_outline) {
    api_reset();
    gfx_circ(30, 30, 1, 7, 0);
    ASSERT_EQ(7, gfx_t_scr(31, 30));
    ASSERT_EQ(7, gfx_t_scr(29, 30));
    ASSERT_EQ(7, gfx_t_scr(30, 31));
    ASSERT_EQ(7, gfx_t_scr(30, 29));
    ASSERT_EQ(0, gfx_t_scr(30, 30));
    ASSERT_EQ(0, gfx_t_scr(32, 30));
    ASSERT_EQ(0, gfx_t_scr(30, 32));
    PASS();
}

TEST(api_circ_filled) {
    int x0, y0, x1, y1, n;
    api_reset();
    gfx_circ(64, 64, 10, 7, 1);
    ASSERT_EQ(7, gfx_t_scr(64, 64));
    ASSERT_EQ(7, gfx_t_scr(74, 64));
    ASSERT_EQ(7, gfx_t_scr(54, 64));
    ASSERT_EQ(7, gfx_t_scr(64, 74));
    ASSERT_EQ(7, gfx_t_scr(64, 54));
    ASSERT_EQ(7, gfx_t_scr(70, 70));
    ASSERT_EQ(7, gfx_t_scr(58, 58));
    ASSERT_EQ(0, gfx_t_scr(74, 74));
    ASSERT_EQ(0, gfx_t_scr(54, 54));
    n = api_bbox(7, &x0, &y0, &x1, &y1);
    ASSERT_EQ(54, x0);
    ASSERT_EQ(74, x1);
    ASSERT_EQ(54, y0);
    ASSERT_EQ(74, y1);
    ASSERT_TRUE(n > 250);
    ASSERT_TRUE(n < 400);
    ASSERT_TRUE(api_symmetric(128, 128));
    PASS();
}

TEST(api_oval_outline_odd_box) {
    int x0, y0, x1, y1;
    api_reset();
    gfx_oval(20, 30, 60, 50, 7, 0);
    ASSERT_EQ(7, gfx_t_scr(20, 40));
    ASSERT_EQ(7, gfx_t_scr(60, 40));
    ASSERT_EQ(7, gfx_t_scr(40, 30));
    ASSERT_EQ(7, gfx_t_scr(40, 50));
    ASSERT_EQ(0, gfx_t_scr(40, 40));
    ASSERT_EQ(0, gfx_t_scr(20, 30));
    ASSERT_EQ(0, gfx_t_scr(60, 50));
    ASSERT_TRUE(api_bbox(7, &x0, &y0, &x1, &y1) > 0);
    ASSERT_EQ(20, x0);
    ASSERT_EQ(60, x1);
    ASSERT_EQ(30, y0);
    ASSERT_EQ(50, y1);
    ASSERT_TRUE(api_symmetric(80, 80));
    PASS();
}

TEST(api_oval_outline_even_box) {
    int x0, y0, x1, y1;
    api_reset();
    gfx_oval(20, 30, 59, 49, 7, 0);
    ASSERT_TRUE(gfx_t_scr(20, 39) == 7 || gfx_t_scr(20, 40) == 7);
    ASSERT_TRUE(gfx_t_scr(59, 39) == 7 || gfx_t_scr(59, 40) == 7);
    ASSERT_TRUE(gfx_t_scr(39, 30) == 7 || gfx_t_scr(40, 30) == 7);
    ASSERT_TRUE(gfx_t_scr(39, 49) == 7 || gfx_t_scr(40, 49) == 7);
    ASSERT_TRUE(api_bbox(7, &x0, &y0, &x1, &y1) > 0);
    ASSERT_EQ(20, x0);
    ASSERT_EQ(59, x1);
    ASSERT_EQ(30, y0);
    ASSERT_EQ(49, y1);
    ASSERT_TRUE(api_symmetric(79, 79));
    PASS();
}

TEST(api_oval_filled) {
    int x0, y0, x1, y1, n;
    api_reset();
    gfx_oval(20, 30, 60, 50, 7, 1);
    ASSERT_EQ(7, gfx_t_scr(40, 40));
    ASSERT_TRUE(gfx_t_rect(20, 40, 61, 41, 7));
    ASSERT_TRUE(gfx_t_rect(40, 30, 41, 51, 7));
    ASSERT_EQ(0, gfx_t_scr(20, 30));
    ASSERT_EQ(0, gfx_t_scr(60, 50));
    ASSERT_EQ(0, gfx_t_scr(20, 50));
    ASSERT_EQ(0, gfx_t_scr(60, 30));
    n = api_bbox(7, &x0, &y0, &x1, &y1);
    ASSERT_EQ(20, x0);
    ASSERT_EQ(60, x1);
    ASSERT_EQ(30, y0);
    ASSERT_EQ(50, y1);
    ASSERT_TRUE(n > 550);
    ASSERT_TRUE(n < 800);
    ASSERT_TRUE(api_symmetric(80, 80));
    PASS();
}

TEST(api_oval_swapped_corners) {
    unsigned char snap[0x2000];
    api_reset();
    gfx_oval(20, 30, 60, 50, 7, 1);
    memcpy(snap, p8_ram.mem.screen, 0x2000);
    gfx_t_fill_scr(0);
    gfx_oval(60, 50, 20, 30, 7, 1);
    ASSERT_EQ(0, memcmp(snap, p8_ram.mem.screen, 0x2000));
    gfx_t_fill_scr(0);
    gfx_oval(20, 30, 60, 50, 7, 0);
    memcpy(snap, p8_ram.mem.screen, 0x2000);
    gfx_t_fill_scr(0);
    gfx_oval(60, 30, 20, 50, 7, 0);
    ASSERT_EQ(0, memcmp(snap, p8_ram.mem.screen, 0x2000));
    PASS();
}

TEST(api_shapes_camera) {
    api_reset();
    p8_ram.mem.draw.camera_x = 10;
    p8_ram.mem.draw.camera_y = 20;
    gfx_rect(15, 25, 16, 26, 7, 1);
    ASSERT_TRUE(gfx_t_rect(5, 5, 7, 7, 7));
    ASSERT_EQ(4, gfx_t_count(7));
    gfx_t_fill_scr(0);
    gfx_line(20, 30, 24, 30, 8);
    ASSERT_TRUE(gfx_t_rect(10, 10, 15, 11, 8));
    ASSERT_EQ(5, gfx_t_count(8));
    gfx_t_fill_scr(0);
    gfx_circ(74, 84, 0, 9, 1);
    ASSERT_EQ(9, gfx_t_scr(64, 64));
    ASSERT_EQ(1, gfx_t_count(9));
    gfx_t_fill_scr(0);
    gfx_oval(30, 40, 40, 50, 6, 1);
    ASSERT_EQ(6, gfx_t_scr(25, 25));
    ASSERT_EQ(0, gfx_t_scr(35, 45));
    PASS();
}

TEST(api_shapes_clip) {
    int x0, y0, x1, y1;
    api_reset();
    p8_ram.mem.draw.clip_x0 = 10;
    p8_ram.mem.draw.clip_y0 = 10;
    p8_ram.mem.draw.clip_x1 = 20;
    p8_ram.mem.draw.clip_y1 = 20;
    gfx_rect(0, 0, 40, 40, 7, 1);
    ASSERT_TRUE(gfx_t_rect(10, 10, 20, 20, 7));
    ASSERT_EQ(100, gfx_t_count(7));
    gfx_t_fill_scr(0);
    gfx_line(0, 15, 40, 15, 8);
    ASSERT_TRUE(gfx_t_rect(10, 15, 20, 16, 8));
    ASSERT_EQ(10, gfx_t_count(8));
    gfx_t_fill_scr(0);
    gfx_circ(15, 15, 30, 9, 1);
    ASSERT_EQ(100, gfx_t_count(9));
    gfx_t_fill_scr(0);
    gfx_oval(0, 0, 40, 40, 6, 1);
    ASSERT_TRUE(api_bbox(6, &x0, &y0, &x1, &y1) > 0);
    ASSERT_TRUE(x0 >= 10 && y0 >= 10 && x1 <= 19 && y1 <= 19);
    PASS();
}

TEST(api_shapes_palette_map) {
    api_reset();
    p8_ram.mem.draw.draw_pal[3] = (uint8_t)(9 | GFX_TRANSPARENT);
    gfx_line(0, 0, 4, 0, 3);
    ASSERT_TRUE(gfx_t_rect(0, 0, 5, 1, 9));
    gfx_rect(0, 10, 4, 14, 3, 1);
    ASSERT_TRUE(gfx_t_rect(0, 10, 5, 15, 9));
    gfx_circ(50, 50, 2, 3, 1);
    ASSERT_EQ(9, gfx_t_scr(50, 50));
    gfx_oval(70, 70, 80, 76, 3, 1);
    ASSERT_EQ(9, gfx_t_scr(75, 73));
    ASSERT_EQ(0, gfx_t_count(3));
    PASS();
}

TEST(api_shapes_ignore_transparency) {
    int k;
    api_reset();
    p8_ram.mem.draw.draw_pal[0] = GFX_TRANSPARENT;
    for (k = 0; k < 8; k++) {
        gfx_t_fill_scr(5);
        api_shape(k, 0);
        ASSERT_TRUE(gfx_t_count(0) > 0);
        ASSERT_TRUE(gfx_t_count(5) < 128 * 128);
    }
    gfx_t_fill_scr(5);
    gfx_rect(10, 10, 12, 12, 0, 1);
    ASSERT_TRUE(gfx_t_rect(10, 10, 13, 13, 0));
    ASSERT_EQ(9, gfx_t_count(0));
    PASS();
}

/* ------------------------------------------------------------------ */
/* fill patterns                                                      */
/* ------------------------------------------------------------------ */

TEST(api_fillp_bit_order_top_left) {
    int x, y;
    api_reset();
    p8_ram.mem.draw.fillp = 0x8000;
    gfx_rect(0, 0, 7, 7, 0x17, 1);
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++)
            ASSERT_EQ(((x & 3) == 0 && (y & 3) == 0) ? 1 : 7,
                      gfx_t_scr(x, y));
    ASSERT_EQ(4, gfx_t_count(1));
    PASS();
}

TEST(api_fillp_bit_order_bottom_right) {
    int x, y;
    api_reset();
    p8_ram.mem.draw.fillp = 0x0001;
    gfx_rect(0, 0, 7, 7, 0x17, 1);
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++)
            ASSERT_EQ(((x & 3) == 3 && (y & 3) == 3) ? 1 : 7,
                      gfx_t_scr(x, y));
    p8_ram.mem.draw.fillp = 0x0010; /* bit 4 -> index 11 -> (x=3,y=2) */
    gfx_t_fill_scr(0);
    gfx_rect(0, 0, 3, 3, 0x17, 1);
    ASSERT_EQ(1, gfx_t_scr(3, 2));
    ASSERT_EQ(1, gfx_t_count(1));
    PASS();
}

TEST(api_fillp_uses_screen_coords_with_camera) {
    api_reset();
    p8_ram.mem.draw.camera_x = 1;
    p8_ram.mem.draw.fillp = 0x8000;
    gfx_rect(1, 0, 8, 7, 0x17, 1);
    ASSERT_EQ(1, gfx_t_scr(0, 0));
    ASSERT_EQ(1, gfx_t_scr(4, 0));
    ASSERT_EQ(7, gfx_t_scr(1, 0));
    ASSERT_EQ(7, gfx_t_scr(3, 0));
    PASS();
}

TEST(api_fillp_transparent_flag) {
    api_reset();
    gfx_t_fill_scr(5);
    p8_ram.mem.draw.fillp = 0xA5A5;
    p8_ram.mem.draw.fillp_flags = 1;
    gfx_rect(0, 0, 7, 3, 0x17, 1);
    /* row 0 pattern 1010: x0,x2 set; row 1 pattern 0101: x1,x3 set */
    ASSERT_EQ(5, gfx_t_scr(0, 0));
    ASSERT_EQ(7, gfx_t_scr(1, 0));
    ASSERT_EQ(5, gfx_t_scr(2, 0));
    ASSERT_EQ(7, gfx_t_scr(3, 0));
    ASSERT_EQ(7, gfx_t_scr(0, 1));
    ASSERT_EQ(5, gfx_t_scr(1, 1));
    ASSERT_EQ(7, gfx_t_scr(2, 1));
    ASSERT_EQ(5, gfx_t_scr(3, 1));
    ASSERT_EQ(0, gfx_t_count(1));
    ASSERT_EQ(0, gfx_t_count(0));
    PASS();
}

TEST(api_fillp_all_shapes_follow_pattern) {
    int k;
    api_reset();
    p8_ram.mem.draw.fillp = 0xFFFF;
    for (k = 0; k < 8; k++) {
        gfx_t_fill_scr(0);
        api_shape(k, 0x17);
        ASSERT_TRUE(gfx_t_count(1) > 0);
        ASSERT_EQ(0, gfx_t_count(7));
    }
    p8_ram.mem.draw.fillp = 0x0000;
    for (k = 0; k < 8; k++) {
        gfx_t_fill_scr(0);
        api_shape(k, 0x17);
        ASSERT_TRUE(gfx_t_count(7) > 0);
        ASSERT_EQ(0, gfx_t_count(1));
    }
    PASS();
}

TEST(api_fillp_all_shapes_transparent) {
    int k;
    api_reset();
    p8_ram.mem.draw.fillp = 0xFFFF;
    p8_ram.mem.draw.fillp_flags = 1;
    for (k = 0; k < 8; k++) {
        gfx_t_fill_scr(5);
        api_shape(k, 0x17);
        ASSERT_EQ(128 * 128, gfx_t_count(5));
    }
    PASS();
}

TEST(api_builtin_fillp) {
    P386VMState vm;
    P386Value a[4];
    int n1, n2;
    int32_t r1, r2;
    uint16_t pat;
    int fl;
    api_reset();
    p386_vm_init(&vm);
    api_setnum(a, 0, 0x1234);
    a[0].value |= 0x8000;
    n1 = p386_builtin_fillp(&vm, a, 1, 0);
    r1 = a[0].value;
    pat = p8_ram.mem.draw.fillp;
    fl = p8_ram.mem.draw.fillp_flags & 1;
    ASSERT_EQ(1, n1);
    ASSERT_EQ(0, r1);
    ASSERT_EQ(0x1234, pat);
    ASSERT_EQ(1, fl);
    api_setnum(a, 0, 0);
    n2 = p386_builtin_fillp(&vm, a, 1, 0);
    r2 = a[0].value;
    ASSERT_EQ(1, n2);
    ASSERT_EQ(1, r2 == (int32_t)(0x12340000L | 0x8000L));
    ASSERT_EQ(0, p8_ram.mem.draw.fillp);
    ASSERT_EQ(0, p8_ram.mem.draw.fillp_flags & 1);
    PASS();
}

/* ------------------------------------------------------------------ */
/* builtin shapes                                                     */
/* ------------------------------------------------------------------ */

TEST(api_b_line_four_args) {
    P386VMState vm;
    api_reset();
    p386_vm_init(&vm);
    p8_ram.mem.draw.pen_color = 8;
    api_run(p386_builtin_line, &vm, 4, 0, 0, 5, 0, 0);
    ASSERT_TRUE(gfx_t_rect(0, 0, 6, 1, 8));
    ASSERT_EQ(6, gfx_t_count(8));
    api_run(p386_builtin_line, &vm, 5, 0, 10, 5, 10, 3);
    ASSERT_TRUE(gfx_t_rect(0, 10, 6, 11, 3));
    PASS();
}

TEST(api_b_line_continues_from_last_end) {
    P386VMState vm;
    api_reset();
    p386_vm_init(&vm);
    api_run(p386_builtin_line, &vm, 5, 10, 10, 15, 10, 7);
    api_run(p386_builtin_line, &vm, 3, 15, 15, 9, 0, 0);
    ASSERT_EQ(9, gfx_t_scr(15, 12));
    ASSERT_EQ(9, gfx_t_scr(15, 14));
    ASSERT_EQ(9, gfx_t_scr(15, 15));
    ASSERT_EQ(7, gfx_t_scr(12, 10));
    ASSERT_EQ(0, gfx_t_scr(14, 12));
    ASSERT_EQ(0, gfx_t_scr(16, 15));
    PASS();
}

TEST(api_b_line_no_args_resets_start) {
    P386VMState vm;
    P386Value a[4];
    api_reset();
    p386_vm_init(&vm);
    p8_ram.mem.draw.pen_color = 6;
    api_run(p386_builtin_line, &vm, 4, 10, 10, 15, 10, 0);
    ASSERT_EQ(6, gfx_t_count(6));
    p386_builtin_line(&vm, a, 0, 0);
    api_run(p386_builtin_line, &vm, 2, 30, 30, 0, 0, 0);
    ASSERT_EQ(6, gfx_t_count(6));
    ASSERT_EQ(0, gfx_t_scr(30, 30));
    api_run(p386_builtin_line, &vm, 2, 40, 30, 0, 0, 0);
    ASSERT_TRUE(gfx_t_rect(30, 30, 41, 31, 6));
    ASSERT_EQ(6 + 11, gfx_t_count(6));
    PASS();
}

TEST(api_b_shapes_default_pen_color) {
    P386VMState vm;
    api_reset();
    p386_vm_init(&vm);
    p8_ram.mem.draw.pen_color = 12;
    api_run(p386_builtin_rect, &vm, 4, 10, 10, 12, 12, 0);
    ASSERT_EQ(8, gfx_t_count(12));
    ASSERT_EQ(0, gfx_t_scr(11, 11));
    api_run(p386_builtin_rectfill, &vm, 4, 30, 10, 32, 12, 0);
    ASSERT_TRUE(gfx_t_rect(30, 10, 33, 13, 12));
    api_run(p386_builtin_circ, &vm, 3, 64, 64, 3, 0, 0);
    ASSERT_EQ(12, gfx_t_scr(67, 64));
    ASSERT_EQ(12, gfx_t_scr(64, 61));
    api_run(p386_builtin_circfill, &vm, 3, 100, 100, 3, 0, 0);
    ASSERT_EQ(12, gfx_t_scr(100, 100));
    ASSERT_EQ(12, gfx_t_scr(103, 100));
    api_run(p386_builtin_oval, &vm, 4, 10, 60, 20, 66, 0);
    ASSERT_EQ(12, gfx_t_scr(10, 63));
    ASSERT_EQ(12, gfx_t_scr(20, 63));
    ASSERT_EQ(0, gfx_t_scr(15, 63));
    api_run(p386_builtin_ovalfill, &vm, 4, 10, 80, 20, 86, 0);
    ASSERT_EQ(12, gfx_t_scr(15, 83));
    ASSERT_EQ(12, gfx_t_scr(10, 83));
    PASS();
}

TEST(api_b_shapes_color_sets_pen) {
    P386VMState vm;
    api_reset();
    p386_vm_init(&vm);
    p8_ram.mem.draw.pen_color = 1;
    api_run(p386_builtin_rect, &vm, 5, 10, 10, 12, 12, 9);
    ASSERT_EQ(9, p8_ram.mem.draw.pen_color);
    ASSERT_EQ(9, gfx_t_scr(10, 10));
    api_run(p386_builtin_rectfill, &vm, 5, 30, 10, 32, 12, 10);
    ASSERT_EQ(10, p8_ram.mem.draw.pen_color);
    api_run(p386_builtin_circ, &vm, 4, 64, 64, 3, 11, 0);
    ASSERT_EQ(11, p8_ram.mem.draw.pen_color);
    ASSERT_EQ(11, gfx_t_scr(67, 64));
    api_run(p386_builtin_circfill, &vm, 4, 100, 100, 3, 13, 0);
    ASSERT_EQ(13, p8_ram.mem.draw.pen_color);
    api_run(p386_builtin_oval, &vm, 5, 10, 60, 20, 66, 14);
    ASSERT_EQ(14, p8_ram.mem.draw.pen_color);
    api_run(p386_builtin_ovalfill, &vm, 5, 10, 80, 20, 86, 15);
    ASSERT_EQ(15, p8_ram.mem.draw.pen_color);
    ASSERT_EQ(15, gfx_t_scr(15, 83));
    PASS();
}

TEST(api_b_circ_default_radius) {
    P386VMState vm;
    api_reset();
    p386_vm_init(&vm);
    api_run(p386_builtin_circ, &vm, 2, 64, 64, 0, 0, 0);
    ASSERT_EQ(7, gfx_t_scr(68, 64));
    ASSERT_EQ(7, gfx_t_scr(60, 64));
    ASSERT_EQ(7, gfx_t_scr(64, 68));
    ASSERT_EQ(7, gfx_t_scr(64, 60));
    ASSERT_EQ(0, gfx_t_scr(69, 64));
    ASSERT_EQ(0, gfx_t_scr(64, 69));
    ASSERT_EQ(0, gfx_t_scr(64, 64));
    gfx_t_fill_scr(0);
    api_run(p386_builtin_circfill, &vm, 2, 64, 64, 0, 0, 0);
    ASSERT_EQ(7, gfx_t_scr(64, 64));
    ASSERT_EQ(7, gfx_t_scr(68, 64));
    ASSERT_EQ(0, gfx_t_scr(69, 64));
    PASS();
}

TEST(api_b_color) {
    P386VMState vm;
    P386Value a[4];
    int n1, n2;
    int32_t r1, r2;
    api_reset();
    p386_vm_init(&vm);
    p8_ram.mem.draw.pen_color = 5;
    api_setnum(a, 0, 9);
    n1 = p386_builtin_color(&vm, a, 1, 0);
    r1 = a[0].value;
    ASSERT_EQ(1, n1);
    ASSERT_EQ(P386_FP_INT(5), r1);
    ASSERT_EQ(9, p8_ram.mem.draw.pen_color);
    n2 = p386_builtin_color(&vm, a, 0, 0);
    r2 = a[0].value;
    ASSERT_EQ(1, n2);
    ASSERT_EQ(P386_FP_INT(9), r2);
    ASSERT_EQ(6, p8_ram.mem.draw.pen_color);
    PASS();
}

TEST(api_b_cursor) {
    P386VMState vm;
    P386Value a[8];
    int n;
    api_reset();
    p386_vm_init(&vm);
    p8_ram.mem.draw.cursor_x = 3;
    p8_ram.mem.draw.cursor_y = 4;
    p8_ram.mem.draw.pen_color = 7;
    api_setnum(a, 0, 10);
    api_setnum(a, 1, 20);
    n = p386_builtin_cursor(&vm, a, 2, 0);
    ASSERT_EQ(3, n);
    ASSERT_TRUE(api_is_num(&a[0], 3));
    ASSERT_TRUE(api_is_num(&a[1], 4));
    ASSERT_TRUE(api_is_num(&a[2], 7));
    ASSERT_EQ(10, p8_ram.mem.draw.cursor_x);
    ASSERT_EQ(20, p8_ram.mem.draw.cursor_y);
    ASSERT_EQ(7, p8_ram.mem.draw.pen_color);
    api_setnum(a, 0, 1);
    api_setnum(a, 1, 2);
    api_setnum(a, 2, 9);
    n = p386_builtin_cursor(&vm, a, 3, 0);
    ASSERT_EQ(3, n);
    ASSERT_TRUE(api_is_num(&a[0], 10));
    ASSERT_TRUE(api_is_num(&a[1], 20));
    ASSERT_TRUE(api_is_num(&a[2], 7));
    ASSERT_EQ(1, p8_ram.mem.draw.cursor_x);
    ASSERT_EQ(2, p8_ram.mem.draw.cursor_y);
    ASSERT_EQ(9, p8_ram.mem.draw.pen_color);
    PASS();
}

/* ------------------------------------------------------------------ */
/* print                                                              */
/* ------------------------------------------------------------------ */

TEST(api_print_narrow_glyph) {
    P386VMState vm;
    P386Value a[8];
    int n;
    api_reset();
    p386_vm_init(&vm);
    ASSERT_TRUE(api_glyph_pixels('a') > 3);
    gfx_t_fill_scr(1);
    api_setstr(a, 0, "a");
    api_setnum(a, 1, 10);
    api_setnum(a, 2, 20);
    api_setnum(a, 3, 7);
    n = p386_builtin_print(&vm, a, 4, 0);
    ASSERT_EQ(1, n);
    ASSERT_EQ(P386_FP_INT(14), a[0].value);
    ASSERT_EQ(P386_TAG_NUM, a[0].tag);
    ASSERT_TRUE(api_glyph_ok('a', 10, 20, 7, 1));
    ASSERT_EQ(1, gfx_t_scr(9, 20));
    ASSERT_EQ(1, gfx_t_scr(14, 20));
    ASSERT_EQ(1, gfx_t_scr(10, 19));
    ASSERT_EQ(1, gfx_t_scr(10, 26));
    PASS();
}

TEST(api_print_several_glyphs_advance) {
    P386VMState vm;
    P386Value a[8];
    api_reset();
    p386_vm_init(&vm);
    gfx_t_fill_scr(1);
    api_setstr(a, 0, "abc");
    api_setnum(a, 1, 0);
    api_setnum(a, 2, 0);
    api_setnum(a, 3, 7);
    p386_builtin_print(&vm, a, 4, 0);
    ASSERT_EQ(P386_FP_INT(12), a[0].value);
    ASSERT_TRUE(api_glyph_ok('a', 0, 0, 7, 1));
    ASSERT_TRUE(api_glyph_ok('b', 4, 0, 7, 1));
    ASSERT_TRUE(api_glyph_ok('c', 8, 0, 7, 1));
    ASSERT_EQ(1, gfx_t_scr(12, 0));
    PASS();
}

TEST(api_print_wide_glyph) {
    P386VMState vm;
    P386Value a[8];
    api_reset();
    p386_vm_init(&vm);
    ASSERT_TRUE(api_glyph_pixels(0x80) > 5);
    gfx_t_fill_scr(1);
    api_setstr(a, 0, "\x80");
    api_setnum(a, 1, 8);
    api_setnum(a, 2, 8);
    api_setnum(a, 3, 7);
    p386_builtin_print(&vm, a, 4, 0);
    ASSERT_EQ(P386_FP_INT(16), a[0].value);
    ASSERT_TRUE(api_glyph_ok(0x80, 8, 8, 7, 1));
    ASSERT_EQ(1, gfx_t_scr(16, 8));
    PASS();
}

TEST(api_print_mixed_narrow_wide_advance) {
    P386VMState vm;
    P386Value a[8];
    api_reset();
    p386_vm_init(&vm);
    gfx_t_fill_scr(1);
    api_setstr(a, 0, "a\x80" "a");
    api_setnum(a, 1, 0);
    api_setnum(a, 2, 0);
    api_setnum(a, 3, 7);
    p386_builtin_print(&vm, a, 4, 0);
    ASSERT_EQ(P386_FP_INT(16), a[0].value);
    ASSERT_TRUE(api_glyph_ok('a', 0, 0, 7, 1));
    ASSERT_TRUE(api_glyph_ok(0x80, 4, 0, 7, 1));
    ASSERT_TRUE(api_glyph_ok('a', 12, 0, 7, 1));
    PASS();
}

TEST(api_print_default_color_is_pen) {
    P386VMState vm;
    P386Value a[8];
    api_reset();
    p386_vm_init(&vm);
    p8_ram.mem.draw.pen_color = 11;
    api_setstr(a, 0, "a");
    api_setnum(a, 1, 40);
    api_setnum(a, 2, 40);
    p386_builtin_print(&vm, a, 3, 0);
    ASSERT_TRUE(api_glyph_ok('a', 40, 40, 11, 0));
    PASS();
}

TEST(api_print_xy_sets_cursor) {
    P386VMState vm;
    api_reset();
    p386_vm_init(&vm);
    api_print_at(&vm, "a", 20, 30, 7);
    ASSERT_EQ(20, p8_ram.mem.draw.cursor_x);
    ASSERT_EQ(36, p8_ram.mem.draw.cursor_y);
    PASS();
}

TEST(api_print_at_cursor) {
    P386VMState vm;
    P386Value a[8];
    int n;
    api_reset();
    p386_vm_init(&vm);
    p8_ram.mem.draw.cursor_x = 4;
    p8_ram.mem.draw.cursor_y = 10;
    p8_ram.mem.draw.pen_color = 7;
    api_setstr(a, 0, "a");
    n = p386_builtin_print(&vm, a, 1, 0);
    ASSERT_EQ(1, n);
    ASSERT_EQ(P386_FP_INT(8), a[0].value);
    ASSERT_TRUE(api_glyph_ok('a', 4, 10, 7, 0));
    ASSERT_EQ(16, p8_ram.mem.draw.cursor_y);
    ASSERT_EQ(4, p8_ram.mem.draw.cursor_x);
    api_setstr(a, 0, "b");
    api_setnum(a, 1, 9);
    p386_builtin_print(&vm, a, 2, 0);
    ASSERT_TRUE(api_glyph_ok('b', 4, 16, 9, 0));
    ASSERT_EQ(22, p8_ram.mem.draw.cursor_y);
    PASS();
}

TEST(api_print_newline_at_cursor) {
    P386VMState vm;
    P386Value a[8];
    api_reset();
    p386_vm_init(&vm);
    p8_ram.mem.draw.cursor_x = 2;
    p8_ram.mem.draw.cursor_y = 10;
    api_setstr(a, 0, "a\nb");
    p386_builtin_print(&vm, a, 1, 0);
    ASSERT_TRUE(api_glyph_ok('a', 2, 10, 7, 0));
    ASSERT_TRUE(api_glyph_ok('b', 2, 16, 7, 0));
    ASSERT_EQ(22, p8_ram.mem.draw.cursor_y);
    PASS();
}

TEST(api_print_camera_applies) {
    P386VMState vm;
    api_reset();
    p386_vm_init(&vm);
    p8_ram.mem.draw.camera_x = 5;
    p8_ram.mem.draw.camera_y = 5;
    api_print_at(&vm, "a", 15, 15, 7);
    ASSERT_TRUE(api_glyph_ok('a', 10, 10, 7, 0));
    PASS();
}

TEST(api_print_clip_applies) {
    P386VMState vm;
    int r, b, bits, want;
    api_reset();
    p386_vm_init(&vm);
    p8_ram.mem.draw.clip_x0 = 11;
    p8_ram.mem.draw.clip_y0 = 21;
    p8_ram.mem.draw.clip_x1 = 13;
    p8_ram.mem.draw.clip_y1 = 24;
    api_print_at(&vm, "a", 10, 20, 7);
    for (r = 0; r < 5; r++)
        for (b = 0; b < 3; b++) {
            bits = p8_font_narrow['a' - 0x20][r];
            want = (((bits >> b) & 1) && b + 10 >= 11 && b + 10 < 13 &&
                    r + 20 >= 21 && r + 20 < 24) ? 7 : 0;
            ASSERT_EQ(want, gfx_t_scr(10 + b, 20 + r));
        }
    PASS();
}

TEST(api_print_ignores_transparency_and_maps) {
    P386VMState vm;
    api_reset();
    p386_vm_init(&vm);
    p8_ram.mem.draw.draw_pal[0] = GFX_TRANSPARENT;
    gfx_t_fill_scr(5);
    api_print_at(&vm, "a", 10, 10, 0);
    ASSERT_TRUE(api_glyph_ok('a', 10, 10, 0, 5));
    p8_ram.mem.draw.draw_pal[7] = 9;
    gfx_t_fill_scr(5);
    api_print_at(&vm, "a", 10, 10, 7);
    ASSERT_TRUE(api_glyph_ok('a', 10, 10, 9, 5));
    PASS();
}

TEST(api_print_nil_and_number) {
    P386VMState vm;
    P386Value a[8];
    unsigned char snap[0x2000];
    api_reset();
    p386_vm_init(&vm);
    api_setstr(a, 0, "[nil]");
    api_setnum(a, 1, 0);
    api_setnum(a, 2, 0);
    api_setnum(a, 3, 7);
    p386_builtin_print(&vm, a, 4, 0);
    memcpy(snap, p8_ram.mem.screen, 0x2000);
    ASSERT_TRUE(gfx_t_count(7) > 0);
    gfx_t_fill_scr(0);
    api_setnil(a, 0);
    api_setnum(a, 1, 0);
    api_setnum(a, 2, 0);
    api_setnum(a, 3, 7);
    p386_builtin_print(&vm, a, 4, 0);
    ASSERT_EQ(P386_FP_INT(20), a[0].value);
    ASSERT_EQ(0, memcmp(snap, p8_ram.mem.screen, 0x2000));
    gfx_t_fill_scr(0);
    api_setnum(a, 0, 12);
    api_setnum(a, 1, 0);
    api_setnum(a, 2, 0);
    api_setnum(a, 3, 7);
    p386_builtin_print(&vm, a, 4, 0);
    ASSERT_EQ(P386_FP_INT(8), a[0].value);
    ASSERT_TRUE(api_glyph_ok('1', 0, 0, 7, 0));
    ASSERT_TRUE(api_glyph_ok('2', 4, 0, 7, 0));
    PASS();
}

TEST(api_print_color_escape) {
    P386VMState vm;
    P386Value a[8];
    api_reset();
    p386_vm_init(&vm);
    api_setstr(a, 0, "\f" "8a");
    api_setnum(a, 1, 0);
    api_setnum(a, 2, 0);
    api_setnum(a, 3, 7);
    p386_builtin_print(&vm, a, 4, 0);
    ASSERT_EQ(P386_FP_INT(4), a[0].value);
    ASSERT_TRUE(api_glyph_ok('a', 0, 0, 8, 0));
    gfx_t_fill_scr(0);
    api_setstr(a, 0, "a\f" "9a");
    api_setnum(a, 1, 0);
    api_setnum(a, 2, 0);
    api_setnum(a, 3, 7);
    p386_builtin_print(&vm, a, 4, 0);
    ASSERT_EQ(P386_FP_INT(8), a[0].value);
    ASSERT_TRUE(api_glyph_ok('a', 0, 0, 7, 0));
    ASSERT_TRUE(api_glyph_ok('a', 4, 0, 9, 0));
    PASS();
}

static void api_stripe_screen(void)
{
    int x, y;
    for (y = 0; y < 128; y++)
        for (x = 0; x < 128; x++)
            gfx_t_set_scr(x, y, (y & 7) + 1);
}

TEST(api_print_scrolls_when_low) {
    P386VMState vm;
    P386Value a[8];
    int r, b, bits;
    api_reset();
    p386_vm_init(&vm);
    api_stripe_screen();
    p8_ram.mem.draw.cursor_x = 0;
    p8_ram.mem.draw.cursor_y = 125;
    p8_ram.mem.draw.pen_color = 15;
    api_setstr(a, 0, "a");
    p386_builtin_print(&vm, a, 1, 0);
    /* shifted up by 3 rows */
    ASSERT_EQ(((3 & 7) + 1), gfx_t_scr(100, 0));
    ASSERT_EQ(((53 & 7) + 1), gfx_t_scr(100, 50));
    ASSERT_EQ(((122 & 7) + 1), gfx_t_scr(100, 119));
    ASSERT_EQ(((127 & 7) + 1), gfx_t_scr(100, 124));
    ASSERT_EQ(0, gfx_t_scr(100, 125));
    ASSERT_EQ(0, gfx_t_scr(100, 126));
    ASSERT_EQ(0, gfx_t_scr(100, 127));
    for (r = 0; r < 5; r++)
        for (b = 0; b < 3; b++) {
            bits = p8_font_narrow['a' - 0x20][r];
            if ((bits >> b) & 1) ASSERT_EQ(15, gfx_t_scr(b, 122 + r));
        }
    PASS();
}

TEST(api_print_no_scroll_at_122) {
    P386VMState vm;
    P386Value a[8];
    api_reset();
    p386_vm_init(&vm);
    api_stripe_screen();
    p8_ram.mem.draw.cursor_x = 0;
    p8_ram.mem.draw.cursor_y = 122;
    p8_ram.mem.draw.pen_color = 15;
    api_setstr(a, 0, "a");
    p386_builtin_print(&vm, a, 1, 0);
    ASSERT_EQ(1, gfx_t_scr(100, 0));
    ASSERT_EQ(((127 & 7) + 1), gfx_t_scr(100, 127));
    ASSERT_EQ(((50 & 7) + 1), gfx_t_scr(100, 50));
    PASS();
}

/* ------------------------------------------------------------------ */
/* type / unpack / pack / select / split / raw*                       */
/* ------------------------------------------------------------------ */

TEST(api_type_names) {
    P386VMState vm;
    P386Value a[4];
    int n;
    api_reset();
    p386_vm_init(&vm);
    api_setnil(a, 0);
    n = p386_builtin_type(&vm, a, 1, 0);
    ASSERT_EQ(1, n);
    ASSERT_TRUE(api_is_str(&a[0], "nil"));
    gfx_t_bool(a, 0, 1);
    p386_builtin_type(&vm, a, 1, 0);
    ASSERT_TRUE(api_is_str(&a[0], "boolean"));
    api_setnum(a, 0, 3);
    p386_builtin_type(&vm, a, 1, 0);
    ASSERT_TRUE(api_is_str(&a[0], "number"));
    api_setstr(a, 0, "x");
    p386_builtin_type(&vm, a, 1, 0);
    ASSERT_TRUE(api_is_str(&a[0], "string"));
    api_settab(a, 0, p386_table_new(0, 0));
    p386_builtin_type(&vm, a, 1, 0);
    ASSERT_TRUE(api_is_str(&a[0], "table"));
    a[0].value = 0;
    a[0].tag = P386_TAG_FUNC;
    p386_builtin_type(&vm, a, 1, 0);
    ASSERT_TRUE(api_is_str(&a[0], "function"));
    a[0].value = 0;
    a[0].tag = P386_TAG_CFUNC;
    p386_builtin_type(&vm, a, 1, 0);
    ASSERT_TRUE(api_is_str(&a[0], "function"));
    PASS();
}

TEST(api_unpack_default_range) {
    P386VMState vm;
    P386Value *a;
    static const int vals[3] = { 10, 20, 30 };
    int n;
    p386_vm_init(&vm);
    a = &vm.value_stack[10];
    api_settab(a, 0, api_mk_numtab(vals, 3));
    n = p386_builtin_unpack(&vm, a, 1, 0);
    ASSERT_EQ(3, n);
    ASSERT_TRUE(api_is_num(&a[0], 10));
    ASSERT_TRUE(api_is_num(&a[1], 20));
    ASSERT_TRUE(api_is_num(&a[2], 30));
    PASS();
}

TEST(api_unpack_ranges) {
    P386VMState vm;
    P386Value *a;
    static const int vals[3] = { 10, 20, 30 };
    P386Table *t;
    int n;
    p386_vm_init(&vm);
    a = &vm.value_stack[10];
    t = api_mk_numtab(vals, 3);
    api_settab(a, 0, t);
    api_setnum(a, 1, 2);
    n = p386_builtin_unpack(&vm, a, 2, 0);
    ASSERT_EQ(2, n);
    ASSERT_TRUE(api_is_num(&a[0], 20));
    ASSERT_TRUE(api_is_num(&a[1], 30));
    api_settab(a, 0, t);
    api_setnum(a, 1, 2);
    api_setnum(a, 2, 2);
    n = p386_builtin_unpack(&vm, a, 3, 0);
    ASSERT_EQ(1, n);
    ASSERT_TRUE(api_is_num(&a[0], 20));
    api_settab(a, 0, t);
    api_setnum(a, 1, 1);
    api_setnum(a, 2, 4);
    n = p386_builtin_unpack(&vm, a, 3, 0);
    ASSERT_EQ(4, n);
    ASSERT_TRUE(api_is_num(&a[2], 30));
    ASSERT_EQ(P386_TAG_NIL, a[3].tag);
    api_settab(a, 0, p386_table_new(0, 0));
    n = p386_builtin_unpack(&vm, a, 1, 0);
    ASSERT_EQ(0, n);
    PASS();
}

TEST(api_pack_basic) {
    P386VMState vm;
    P386Value a[8];
    P386Value v;
    P386Table *t;
    int n;
    p386_vm_init(&vm);
    api_setnum(a, 0, 5);
    api_setstr(a, 1, "x");
    api_setnum(a, 2, 7);
    n = p386_builtin_pack(&vm, a, 3, 0);
    ASSERT_EQ(1, n);
    ASSERT_EQ(P386_TAG_TAB, a[0].tag);
    t = api_tab_of(&a[0]);
    api_tget_i(t, 1, &v);
    ASSERT_TRUE(api_is_num(&v, 5));
    api_tget_i(t, 2, &v);
    ASSERT_TRUE(api_is_str(&v, "x"));
    api_tget_i(t, 3, &v);
    ASSERT_TRUE(api_is_num(&v, 7));
    api_tget_s(t, "n", &v);
    ASSERT_TRUE(api_is_num(&v, 3));
    ASSERT_EQ(3, (int)p386_table_len(t));
    PASS();
}

TEST(api_pack_empty_and_nil) {
    P386VMState vm;
    P386Value a[8];
    P386Value v;
    P386Table *t;
    p386_vm_init(&vm);
    p386_builtin_pack(&vm, a, 0, 0);
    ASSERT_EQ(P386_TAG_TAB, a[0].tag);
    t = api_tab_of(&a[0]);
    api_tget_s(t, "n", &v);
    ASSERT_TRUE(api_is_num(&v, 0));
    api_setnum(a, 0, 1);
    api_setnil(a, 1);
    api_setnum(a, 2, 3);
    p386_builtin_pack(&vm, a, 3, 0);
    t = api_tab_of(&a[0]);
    api_tget_s(t, "n", &v);
    ASSERT_TRUE(api_is_num(&v, 3));
    api_tget_i(t, 3, &v);
    ASSERT_TRUE(api_is_num(&v, 3));
    api_tget_i(t, 2, &v);
    ASSERT_EQ(P386_TAG_NIL, v.tag);
    PASS();
}

TEST(api_select_count) {
    P386VMState vm;
    P386Value *a;
    int n;
    p386_vm_init(&vm);
    a = &vm.value_stack[10];
    api_setstr(a, 0, "#");
    api_setnum(a, 1, 11);
    api_setnum(a, 2, 22);
    api_setnum(a, 3, 33);
    n = p386_builtin_select(&vm, a, 4, 0);
    ASSERT_EQ(1, n);
    ASSERT_TRUE(api_is_num(&a[0], 3));
    api_setstr(a, 0, "#");
    n = p386_builtin_select(&vm, a, 1, 0);
    ASSERT_EQ(1, n);
    ASSERT_TRUE(api_is_num(&a[0], 0));
    PASS();
}

TEST(api_select_positive_and_negative) {
    P386VMState vm;
    P386Value *a;
    int n;
    p386_vm_init(&vm);
    a = &vm.value_stack[10];
    api_setnum(a, 0, 2);
    api_setnum(a, 1, 11);
    api_setnum(a, 2, 22);
    api_setnum(a, 3, 33);
    n = p386_builtin_select(&vm, a, 4, 0);
    ASSERT_EQ(2, n);
    ASSERT_TRUE(api_is_num(&a[0], 22));
    ASSERT_TRUE(api_is_num(&a[1], 33));
    api_setnum(a, 0, 1);
    api_setnum(a, 1, 11);
    api_setnum(a, 2, 22);
    api_setnum(a, 3, 33);
    n = p386_builtin_select(&vm, a, 4, 0);
    ASSERT_EQ(3, n);
    ASSERT_TRUE(api_is_num(&a[0], 11));
    api_setnum(a, 0, -1);
    api_setnum(a, 1, 11);
    api_setnum(a, 2, 22);
    api_setnum(a, 3, 33);
    n = p386_builtin_select(&vm, a, 4, 0);
    ASSERT_EQ(1, n);
    ASSERT_TRUE(api_is_num(&a[0], 33));
    api_setnum(a, 0, -2);
    api_setnum(a, 1, 11);
    api_setnum(a, 2, 22);
    api_setnum(a, 3, 33);
    n = p386_builtin_select(&vm, a, 4, 0);
    ASSERT_EQ(2, n);
    ASSERT_TRUE(api_is_num(&a[0], 22));
    ASSERT_TRUE(api_is_num(&a[1], 33));
    PASS();
}

TEST(api_split_defaults) {
    P386VMState vm;
    P386Value a[4];
    P386Value v;
    P386Table *t;
    int n;
    p386_vm_init(&vm);
    api_setstr(a, 0, "1,2,3");
    n = p386_builtin_split(&vm, a, 1, 0);
    ASSERT_EQ(1, n);
    ASSERT_EQ(P386_TAG_TAB, a[0].tag);
    t = api_tab_of(&a[0]);
    ASSERT_EQ(3, (int)p386_table_len(t));
    api_tget_i(t, 1, &v);
    ASSERT_TRUE(api_is_num(&v, 1));
    api_tget_i(t, 2, &v);
    ASSERT_TRUE(api_is_num(&v, 2));
    api_tget_i(t, 3, &v);
    ASSERT_TRUE(api_is_num(&v, 3));
    api_setstr(a, 0, "ab,1,c");
    p386_builtin_split(&vm, a, 1, 0);
    t = api_tab_of(&a[0]);
    ASSERT_EQ(3, (int)p386_table_len(t));
    api_tget_i(t, 1, &v);
    ASSERT_TRUE(api_is_str(&v, "ab"));
    api_tget_i(t, 2, &v);
    ASSERT_TRUE(api_is_num(&v, 1));
    api_tget_i(t, 3, &v);
    ASSERT_TRUE(api_is_str(&v, "c"));
    PASS();
}

TEST(api_split_no_convert) {
    P386VMState vm;
    P386Value a[4];
    P386Value v;
    P386Table *t;
    p386_vm_init(&vm);
    api_setstr(a, 0, "1,2");
    api_setstr(a, 1, ",");
    gfx_t_bool(a, 2, 0);
    p386_builtin_split(&vm, a, 3, 0);
    t = api_tab_of(&a[0]);
    ASSERT_EQ(2, (int)p386_table_len(t));
    api_tget_i(t, 1, &v);
    ASSERT_TRUE(api_is_str(&v, "1"));
    api_tget_i(t, 2, &v);
    ASSERT_TRUE(api_is_str(&v, "2"));
    PASS();
}

TEST(api_split_custom_sep_and_empty_fields) {
    P386VMState vm;
    P386Value a[4];
    P386Value v;
    P386Table *t;
    p386_vm_init(&vm);
    api_setstr(a, 0, "a,,b");
    p386_builtin_split(&vm, a, 1, 0);
    t = api_tab_of(&a[0]);
    ASSERT_EQ(3, (int)p386_table_len(t));
    api_tget_i(t, 1, &v);
    ASSERT_TRUE(api_is_str(&v, "a"));
    api_tget_i(t, 2, &v);
    ASSERT_TRUE(api_is_str(&v, ""));
    api_tget_i(t, 3, &v);
    ASSERT_TRUE(api_is_str(&v, "b"));
    api_setstr(a, 0, "x;y;z");
    api_setstr(a, 1, ";");
    p386_builtin_split(&vm, a, 2, 0);
    t = api_tab_of(&a[0]);
    ASSERT_EQ(3, (int)p386_table_len(t));
    api_tget_i(t, 3, &v);
    ASSERT_TRUE(api_is_str(&v, "z"));
    PASS();
}

TEST(api_split_chars_and_chunks) {
    P386VMState vm;
    P386Value a[4];
    P386Value v;
    P386Table *t;
    p386_vm_init(&vm);
    api_setstr(a, 0, "abc");
    api_setstr(a, 1, "");
    p386_builtin_split(&vm, a, 2, 0);
    t = api_tab_of(&a[0]);
    ASSERT_EQ(3, (int)p386_table_len(t));
    api_tget_i(t, 1, &v);
    ASSERT_TRUE(api_is_str(&v, "a"));
    api_tget_i(t, 3, &v);
    ASSERT_TRUE(api_is_str(&v, "c"));
    api_setstr(a, 0, "abcde");
    api_setnum(a, 1, 2);
    p386_builtin_split(&vm, a, 2, 0);
    t = api_tab_of(&a[0]);
    ASSERT_EQ(3, (int)p386_table_len(t));
    api_tget_i(t, 1, &v);
    ASSERT_TRUE(api_is_str(&v, "ab"));
    api_tget_i(t, 2, &v);
    ASSERT_TRUE(api_is_str(&v, "cd"));
    api_tget_i(t, 3, &v);
    ASSERT_TRUE(api_is_str(&v, "e"));
    api_setstr(a, 0, "1234");
    api_setnum(a, 1, 2);
    p386_builtin_split(&vm, a, 2, 0);
    t = api_tab_of(&a[0]);
    api_tget_i(t, 1, &v);
    ASSERT_TRUE(api_is_num(&v, 12));
    api_tget_i(t, 2, &v);
    ASSERT_TRUE(api_is_num(&v, 34));
    PASS();
}

TEST(api_rawget_rawset) {
    P386VMState vm;
    P386Value a[4];
    P386Value v;
    P386Table *t;
    int n;
    p386_vm_init(&vm);
    t = p386_table_new(0, 0);
    api_settab(a, 0, t);
    api_setstr(a, 1, "k");
    api_setnum(a, 2, 42);
    n = p386_builtin_rawset(&vm, a, 3, 0);
    ASSERT_TRUE(n >= 0);
    api_tget_s(t, "k", &v);
    ASSERT_TRUE(api_is_num(&v, 42));
    api_settab(a, 0, t);
    api_setstr(a, 1, "k");
    n = p386_builtin_rawget(&vm, a, 2, 0);
    ASSERT_EQ(1, n);
    ASSERT_TRUE(api_is_num(&a[0], 42));
    api_settab(a, 0, t);
    api_setstr(a, 1, "missing");
    n = p386_builtin_rawget(&vm, a, 2, 0);
    ASSERT_EQ(1, n);
    ASSERT_EQ(P386_TAG_NIL, a[0].tag);
    api_settab(a, 0, t);
    api_setnum(a, 1, 1);
    api_setnum(a, 2, 9);
    p386_builtin_rawset(&vm, a, 3, 0);
    ASSERT_EQ(1, (int)p386_table_len(t));
    PASS();
}

TEST(api_rawlen) {
    P386VMState vm;
    P386Value a[4];
    static const int vals[4] = { 1, 2, 3, 4 };
    int n;
    p386_vm_init(&vm);
    api_settab(a, 0, api_mk_numtab(vals, 4));
    n = p386_builtin_rawlen(&vm, a, 1, 0);
    ASSERT_EQ(1, n);
    ASSERT_TRUE(api_is_num(&a[0], 4));
    api_setstr(a, 0, "hello");
    p386_builtin_rawlen(&vm, a, 1, 0);
    ASSERT_TRUE(api_is_num(&a[0], 5));
    api_setstr(a, 0, "");
    p386_builtin_rawlen(&vm, a, 1, 0);
    ASSERT_TRUE(api_is_num(&a[0], 0));
    PASS();
}

TEST(api_rawequal) {
    P386VMState vm;
    P386Value a[4];
    P386Table *t1, *t2;
    int n;
    p386_vm_init(&vm);
    t1 = p386_table_new(0, 0);
    t2 = p386_table_new(0, 0);
    api_setnum(a, 0, 3);
    api_setnum(a, 1, 3);
    n = p386_builtin_rawequal(&vm, a, 2, 0);
    ASSERT_EQ(1, n);
    ASSERT_EQ(P386_TAG_BOOL, a[0].tag);
    ASSERT_NEQ(0, a[0].value);
    api_setnum(a, 0, 3);
    api_setnum(a, 1, 4);
    p386_builtin_rawequal(&vm, a, 2, 0);
    ASSERT_EQ(P386_TAG_BOOL, a[0].tag);
    ASSERT_EQ(0, a[0].value);
    api_setstr(a, 0, "abc");
    api_setstr(a, 1, "abc");
    p386_builtin_rawequal(&vm, a, 2, 0);
    ASSERT_NEQ(0, a[0].value);
    api_setstr(a, 0, "abc");
    api_setstr(a, 1, "abd");
    p386_builtin_rawequal(&vm, a, 2, 0);
    ASSERT_EQ(0, a[0].value);
    api_settab(a, 0, t1);
    api_settab(a, 1, t1);
    p386_builtin_rawequal(&vm, a, 2, 0);
    ASSERT_NEQ(0, a[0].value);
    api_settab(a, 0, t1);
    api_settab(a, 1, t2);
    p386_builtin_rawequal(&vm, a, 2, 0);
    ASSERT_EQ(0, a[0].value);
    api_setnum(a, 0, 0);
    api_setnil(a, 1);
    p386_builtin_rawequal(&vm, a, 2, 0);
    ASSERT_EQ(P386_TAG_BOOL, a[0].tag);
    ASSERT_EQ(0, a[0].value);
    PASS();
}

/* ------------------------------------------------------------------ */
/* memcpy / memset / reload / cstore                                  */
/* ------------------------------------------------------------------ */

TEST(api_memset_basic) {
    P386VMState vm;
    api_reset();
    p386_vm_init(&vm);
    memset(&p8_ram.raw[0x4300], 0, 32);
    api_run(p386_builtin_memset, &vm, 3, 0x4304, 0xAB, 8, 0, 0);
    ASSERT_EQ(0, p8_ram.raw[0x4303]);
    ASSERT_EQ(0xAB, p8_ram.raw[0x4304]);
    ASSERT_EQ(0xAB, p8_ram.raw[0x430B]);
    ASSERT_EQ(0, p8_ram.raw[0x430C]);
    api_run(p386_builtin_memset, &vm, 3, 0x4310, 0x1FF, 2, 0, 0);
    ASSERT_EQ(0xFF, p8_ram.raw[0x4310]);
    ASSERT_EQ(0xFF, p8_ram.raw[0x4311]);
    ASSERT_EQ(0, p8_ram.raw[0x4312]);
    api_run(p386_builtin_memset, &vm, 3, 0x4320, 0x55, 0, 0, 0);
    ASSERT_EQ(0, p8_ram.raw[0x4320]);
    PASS();
}

TEST(api_memset_high_addresses) {
    P386VMState vm;
    api_reset();
    p386_vm_init(&vm);
    p8_ram.raw[0x7FF0] = 0;
    p8_ram.raw[0x7FFF] = 0;
    p8_ram.raw[0x8000] = 0;
    p8_ram.raw[0x8001] = 0;
    api_run(p386_builtin_memset, &vm, 3, 0x7FFE, 0x33, 4, 0, 0);
    ASSERT_EQ(0x33, p8_ram.raw[0x7FFE]);
    ASSERT_EQ(0x33, p8_ram.raw[0x7FFF]);
    ASSERT_EQ(0x33, p8_ram.raw[0x8000]);
    ASSERT_EQ(0x33, p8_ram.raw[0x8001]);
    ASSERT_EQ(0, p8_ram.raw[0x8002]);
    PASS();
}

TEST(api_memset_out_of_range_safe) {
    P386VMState vm;
    api_reset();
    p386_vm_init(&vm);
    p8_ram.raw[0x0000] = 0x55;
    p8_ram.raw[0x0001] = 0x55;
    p8_ram.raw[0x0040] = 0x55;
    /* negative dest, range reaches past 0: must not crash or touch
       memory beyond the requested end */
    api_run(p386_builtin_memset, &vm, 3, -4, 0x77, 6, 0, 0);
    ASSERT_EQ(0x55, p8_ram.raw[0x0040]);
    /* very large length from a high address must clamp, not wrap */
    p8_ram.raw[0x0100] = 0x55;
    api_run(p386_builtin_memset, &vm, 3, 0x7FFF, 0x11, 0x7FFF, 0, 0);
    ASSERT_EQ(0x11, p8_ram.raw[0x7FFF]);
    ASSERT_EQ(0x11, p8_ram.raw[0xFFFD]);
    ASSERT_EQ(0x55, p8_ram.raw[0x0100]);
    PASS();
}

TEST(api_memcpy_basic) {
    P386VMState vm;
    int i;
    api_reset();
    p386_vm_init(&vm);
    for (i = 0; i < 8; i++) {
        p8_ram.raw[0x4300 + i] = (uint8_t)(i + 1);
        p8_ram.raw[0x4400 + i] = 0;
    }
    api_run(p386_builtin_memcpy, &vm, 3, 0x4400, 0x4300, 8, 0, 0);
    for (i = 0; i < 8; i++) ASSERT_EQ(i + 1, p8_ram.raw[0x4400 + i]);
    ASSERT_EQ(0, p8_ram.raw[0x4408]);
    ASSERT_EQ(0, p8_ram.raw[0x43FF]);
    api_run(p386_builtin_memcpy, &vm, 3, 0x4500, 0x4300, 0, 0, 0);
    ASSERT_EQ(0, p8_ram.raw[0x4500]);
    PASS();
}

TEST(api_memcpy_overlap) {
    P386VMState vm;
    int i;
    api_reset();
    p386_vm_init(&vm);
    for (i = 0; i < 8; i++) p8_ram.raw[0x4300 + i] = (uint8_t)(i + 1);
    api_run(p386_builtin_memcpy, &vm, 3, 0x4302, 0x4300, 6, 0, 0);
    ASSERT_EQ(1, p8_ram.raw[0x4300]);
    ASSERT_EQ(2, p8_ram.raw[0x4301]);
    for (i = 0; i < 6; i++) ASSERT_EQ(i + 1, p8_ram.raw[0x4302 + i]);
    for (i = 0; i < 8; i++) p8_ram.raw[0x4300 + i] = (uint8_t)(i + 1);
    api_run(p386_builtin_memcpy, &vm, 3, 0x4300, 0x4302, 6, 0, 0);
    for (i = 0; i < 6; i++) ASSERT_EQ(i + 3, p8_ram.raw[0x4300 + i]);
    ASSERT_EQ(7, p8_ram.raw[0x4306]);
    ASSERT_EQ(8, p8_ram.raw[0x4307]);
    PASS();
}

TEST(api_memcpy_out_of_range_safe) {
    P386VMState vm;
    api_reset();
    p386_vm_init(&vm);
    p8_ram.raw[0x0100] = 0x55;
    p8_ram.raw[0x7FFF] = 0x66;
    p8_ram.raw[0xFFFE] = 0;
    api_run(p386_builtin_memcpy, &vm, 3, 0x7FFF, 0x7FFF, 0x7FFF, 0, 0);
    ASSERT_EQ(0x55, p8_ram.raw[0x0100]);
    ASSERT_EQ(0x66, p8_ram.raw[0x7FFF]);
    api_run(p386_builtin_memcpy, &vm, 3, -8, 0x4300, 16, 0, 0);
    ASSERT_EQ(0x55, p8_ram.raw[0x0100]);
    PASS();
}

TEST(api_reload_cstore) {
    P386VMState vm;
    static uint8_t rom[0x4400];
    P386Host saved;
    int i;
    int ram_ok, rom_ok, ram_after, rom_after;
    api_reset();
    p386_vm_init(&vm);
    saved = p386_host;
    for (i = 0; i < 0x4400; i++) rom[i] = (uint8_t)(i * 3 + 1);
    p386_host.cart_rom = rom;
    p386_host.cart_rom_size = 0x4400;
    memset(&p8_ram.raw[0x100], 0xEE, 16);
    api_run(p386_builtin_reload, &vm, 3, 0x100, 0x10, 8, 0, 0);
    ram_ok = 1;
    for (i = 0; i < 8; i++)
        if (p8_ram.raw[0x100 + i] != rom[0x10 + i]) ram_ok = 0;
    ram_after = p8_ram.raw[0x108];
    memset(&p8_ram.raw[0x4300], 0x5A, 8);
    api_run(p386_builtin_cstore, &vm, 3, 0x20, 0x4300, 4, 0, 0);
    rom_ok = (rom[0x20] == 0x5A && rom[0x23] == 0x5A);
    rom_after = rom[0x24];
    p386_host = saved;
    ASSERT_TRUE(ram_ok);
    ASSERT_EQ(0xEE, ram_after);
    ASSERT_TRUE(rom_ok);
    ASSERT_EQ((0x24 * 3 + 1) & 0xFF, rom_after);
    PASS();
}

TEST(api_reload_no_args) {
    P386VMState vm;
    P386Value a[2];
    static uint8_t rom[0x4400];
    P386Host saved;
    int i, ok;
    int first, last, after;
    api_reset();
    p386_vm_init(&vm);
    saved = p386_host;
    for (i = 0; i < 0x4400; i++) rom[i] = (uint8_t)(i * 5 + 7);
    p386_host.cart_rom = rom;
    p386_host.cart_rom_size = 0x4400;
    memset(&p8_ram.raw[0], 0xEE, 0x4400);
    p386_builtin_reload(&vm, a, 0, 0);
    ok = 1;
    for (i = 0; i < 0x4300; i++)
        if (p8_ram.raw[i] != rom[i]) ok = 0;
    first = p8_ram.raw[0];
    last = p8_ram.raw[0x42FF];
    after = p8_ram.raw[0x4300];
    p386_host = saved;
    ASSERT_TRUE(ok);
    ASSERT_EQ(7, first);
    ASSERT_EQ(rom[0x42FF], last);
    ASSERT_EQ(0xEE, after);
    PASS();
}

TEST(api_reload_cstore_null_rom) {
    P386VMState vm;
    P386Host saved;
    int a0, a1;
    api_reset();
    p386_vm_init(&vm);
    saved = p386_host;
    p386_host.cart_rom = 0;
    p386_host.cart_rom_size = 0;
    memset(&p8_ram.raw[0x100], 0x42, 16);
    api_run(p386_builtin_reload, &vm, 3, 0x100, 0, 16, 0, 0);
    a0 = p8_ram.raw[0x100];
    api_run(p386_builtin_cstore, &vm, 3, 0, 0x100, 16, 0, 0);
    a1 = p8_ram.raw[0x10F];
    {
        P386Value a[2];
        p386_builtin_reload(&vm, a, 0, 0);
    }
    p386_host = saved;
    ASSERT_EQ(0x42, a0);
    ASSERT_EQ(0x42, a1);
    ASSERT_EQ(0x42, p8_ram.raw[0x100]);
    PASS();
}

TEST(api_reload_clamps_to_rom_size) {
    P386VMState vm;
    static uint8_t rom[0x40];
    P386Host saved;
    int i;
    int in_ok, past;
    api_reset();
    p386_vm_init(&vm);
    saved = p386_host;
    for (i = 0; i < 0x40; i++) rom[i] = (uint8_t)(i + 1);
    p386_host.cart_rom = rom;
    p386_host.cart_rom_size = 0x20;
    memset(&p8_ram.raw[0x200], 0xEE, 0x40);
    api_run(p386_builtin_reload, &vm, 3, 0x200, 0, 0x40, 0, 0);
    in_ok = (p8_ram.raw[0x200] == 1 && p8_ram.raw[0x21F - 0x200 + 0x200] == 0x20);
    past = p8_ram.raw[0x220];
    p386_host = saved;
    ASSERT_TRUE(in_ok);
    ASSERT_EQ(0xEE, past);
    PASS();
}

/* ------------------------------------------------------------------ */
/* time / stat / flip                                                 */
/* ------------------------------------------------------------------ */

TEST(api_time_and_stat) {
    P386VMState vm;
    P386Value a[2];
    P386Host saved;
    int n_t, n_s7, n_s8, n_s9;
    int32_t t, s7, s8, s9;
    uint32_t tt, st7, st8, st9;
    p386_vm_init(&vm);
    saved = p386_host;
    p386_host.time_fp = 0x00018000;
    p386_host.fps = 30;                 /* plain integers (P386Host) */
    p386_host.target_fps = 60;
    n_t = p386_builtin_time(&vm, a, 0, 0);
    t = a[0].value; tt = a[0].tag;
    api_setnum(a, 0, 7);
    n_s7 = p386_builtin_stat(&vm, a, 1, 0);
    s7 = a[0].value; st7 = a[0].tag;
    api_setnum(a, 0, 8);
    n_s8 = p386_builtin_stat(&vm, a, 1, 0);
    s8 = a[0].value; st8 = a[0].tag;
    api_setnum(a, 0, 999);
    n_s9 = p386_builtin_stat(&vm, a, 1, 0);
    s9 = a[0].value; st9 = a[0].tag;
    p386_host = saved;
    ASSERT_EQ(1, n_t);
    ASSERT_EQ(P386_TAG_NUM, tt);
    ASSERT_EQ(0x00018000, t);
    ASSERT_EQ(1, n_s7);
    ASSERT_EQ(P386_TAG_NUM, st7);
    ASSERT_EQ(P386_FP_INT(30), s7);
    ASSERT_EQ(1, n_s8);
    ASSERT_EQ(P386_TAG_NUM, st8);
    ASSERT_EQ(P386_FP_INT(60), s8);
    ASSERT_EQ(1, n_s9);
    ASSERT_EQ(P386_TAG_NUM, st9);
    ASSERT_EQ(0, s9);
    PASS();
}

static int api_flip_calls;
static int api_flip_ret;

static int api_flip_hook(void)
{
    api_flip_calls++;
    return api_flip_ret;
}

TEST(api_flip_hook_behavior) {
    P386VMState vm;
    P386Value a[2];
    P386Host saved;
    int r_null, r_ok, r_quit, calls_ok, calls_quit;
    p386_vm_init(&vm);
    saved = p386_host;
    p386_host.flip = 0;
    r_null = p386_builtin_flip(&vm, a, 0, 0);
    p386_host.flip = api_flip_hook;
    api_flip_calls = 0;
    api_flip_ret = 0;
    r_ok = p386_builtin_flip(&vm, a, 0, 0);
    calls_ok = api_flip_calls;
    api_flip_ret = 1;
    r_quit = p386_builtin_flip(&vm, a, 0, 0);
    calls_quit = api_flip_calls;
    p386_host = saved;
    ASSERT_EQ(0, r_null);
    ASSERT_EQ(0, r_ok);
    ASSERT_EQ(1, calls_ok);
    ASSERT_EQ(P386_VM_ERR_QUIT, r_quit);
    ASSERT_TRUE(r_quit < 0);
    ASSERT_EQ(2, calls_quit);
    PASS();
}

/*
 * Registration order:
 *   api_line_horizontal_vertical
 *   api_line_diagonal_and_point
 *   api_line_reversed_same_pixels
 *   api_line_shallow_one_pixel_per_column
 *   api_rect_outline
 *   api_rect_filled_any_order
 *   api_rect_degenerate
 *   api_circ_radius_zero
 *   api_circ_outline
 *   api_circ_small_outline
 *   api_circ_filled
 *   api_oval_outline_odd_box
 *   api_oval_outline_even_box
 *   api_oval_filled
 *   api_oval_swapped_corners
 *   api_shapes_camera
 *   api_shapes_clip
 *   api_shapes_palette_map
 *   api_shapes_ignore_transparency
 *   api_fillp_bit_order_top_left
 *   api_fillp_bit_order_bottom_right
 *   api_fillp_uses_screen_coords_with_camera
 *   api_fillp_transparent_flag
 *   api_fillp_all_shapes_follow_pattern
 *   api_fillp_all_shapes_transparent
 *   api_builtin_fillp
 *   api_b_line_four_args
 *   api_b_line_continues_from_last_end
 *   api_b_line_no_args_resets_start
 *   api_b_shapes_default_pen_color
 *   api_b_shapes_color_sets_pen
 *   api_b_circ_default_radius
 *   api_b_color
 *   api_b_cursor
 *   api_print_narrow_glyph
 *   api_print_several_glyphs_advance
 *   api_print_wide_glyph
 *   api_print_mixed_narrow_wide_advance
 *   api_print_default_color_is_pen
 *   api_print_xy_sets_cursor
 *   api_print_at_cursor
 *   api_print_newline_at_cursor
 *   api_print_camera_applies
 *   api_print_clip_applies
 *   api_print_ignores_transparency_and_maps
 *   api_print_nil_and_number
 *   api_print_color_escape
 *   api_print_scrolls_when_low
 *   api_print_no_scroll_at_122
 *   api_type_names
 *   api_unpack_default_range
 *   api_unpack_ranges
 *   api_pack_basic
 *   api_pack_empty_and_nil
 *   api_select_count
 *   api_select_positive_and_negative
 *   api_split_defaults
 *   api_split_no_convert
 *   api_split_custom_sep_and_empty_fields
 *   api_split_chars_and_chunks
 *   api_rawget_rawset
 *   api_rawlen
 *   api_rawequal
 *   api_memset_basic
 *   api_memset_high_addresses
 *   api_memset_out_of_range_safe
 *   api_memcpy_basic
 *   api_memcpy_overlap
 *   api_memcpy_out_of_range_safe
 *   api_reload_cstore
 *   api_reload_no_args
 *   api_reload_cstore_null_rom
 *   api_reload_clamps_to_rom_size
 *   api_time_and_stat
 *   api_flip_hook_behavior
 */
