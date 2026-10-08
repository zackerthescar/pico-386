/*
 * Spec-based graphics tests. Included at the end of test_vm.c's translation
 * unit (uses BNUM, p8_ram, and the test.h macros from there).
 */
#include "gfx.h"

#define GFP_HALF 0x8000

static void gfx_t_reset(void)
{
    p8_ram_init();
    memset(p8_ram.raw, 0, 0x3100);
    memset(p8_ram.mem.screen, 0, 0x2000);
    p8_ram.mem.draw.clip_x0 = 0;
    p8_ram.mem.draw.clip_y0 = 0;
    p8_ram.mem.draw.clip_x1 = 128;
    p8_ram.mem.draw.clip_y1 = 128;
    p8_ram.mem.draw.camera_x = 0;
    p8_ram.mem.draw.camera_y = 0;
}

static int gfx_t_scr(int x, int y)
{
    uint8_t b = p8_ram.mem.screen[y * 64 + (x >> 1)];
    return (x & 1) ? (b >> 4) : (b & 15);
}

static void gfx_t_set_scr(int x, int y, int c)
{
    uint8_t *p = &p8_ram.mem.screen[y * 64 + (x >> 1)];
    if (x & 1) *p = (uint8_t)((*p & 0x0F) | (c << 4));
    else *p = (uint8_t)((*p & 0xF0) | c);
}

static void gfx_t_fill_scr(int c)
{
    memset(p8_ram.mem.screen, (c << 4) | c, 0x2000);
}

static void gfx_t_set_sheet(int x, int y, int c)
{
    uint8_t *p = &p8_ram.mem.gfx[y * 64 + (x >> 1)];
    if (x & 1) *p = (uint8_t)((*p & 0x0F) | (c << 4));
    else *p = (uint8_t)((*p & 0xF0) | c);
}

static void gfx_t_fill_spr(int n, int c)
{
    int x, y;
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++)
            gfx_t_set_sheet((n % 16) * 8 + x, (n / 16) * 8 + y, c);
}

/* 1 if every pixel in [x0,x1) x [y0,y1) equals c */
static int gfx_t_rect(int x0, int y0, int x1, int y1, int c)
{
    int x, y;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            if (gfx_t_scr(x, y) != c) return 0;
    return 1;
}

static int gfx_t_count(int c)
{
    int x, y, n = 0;
    for (y = 0; y < 128; y++)
        for (x = 0; x < 128; x++)
            if (gfx_t_scr(x, y) == c) n++;
    return n;
}

static void gfx_t_bool(P386Value *a, int i, int b)
{
    a[i].value = b ? 1 : 0;
    a[i].tag = P386_TAG_BOOL;
}

/* ------------------------------------------------------------------ */
/* pset / pget / sget / sset / mget / mset                            */
/* ------------------------------------------------------------------ */

TEST(gfx_pset_nibbles) {
    gfx_t_reset();
    gfx_pset(3, 2, 7);
    gfx_pset(4, 2, 9);
    ASSERT_EQ(0x70, p8_ram.mem.screen[2 * 64 + 1]);
    ASSERT_EQ(0x09, p8_ram.mem.screen[2 * 64 + 2]);
    ASSERT_EQ(0x00, p8_ram.mem.screen[2 * 64 + 0]);
    ASSERT_EQ(2, gfx_t_count(7) + gfx_t_count(9));
    PASS();
}

TEST(gfx_pset_ignores_transparency) {
    gfx_t_reset();
    gfx_t_fill_scr(5);
    gfx_pset(10, 10, 0);
    ASSERT_EQ(0, gfx_t_scr(10, 10));
    ASSERT_EQ(5, gfx_t_scr(11, 10));
    ASSERT_EQ(5, gfx_t_scr(9, 10));
    PASS();
}

TEST(gfx_pset_palette_mapping) {
    gfx_t_reset();
    p8_ram.mem.draw.draw_pal[3] = 9 | GFX_TRANSPARENT;
    gfx_pset(0, 0, 3);
    ASSERT_EQ(9, gfx_t_scr(0, 0));
    gfx_pset(1, 0, 19); /* 19 & 15 == 3 */
    ASSERT_EQ(9, gfx_t_scr(1, 0));
    PASS();
}

TEST(gfx_pset_camera_and_clip) {
    gfx_t_reset();
    p8_ram.mem.draw.camera_x = 10;
    p8_ram.mem.draw.camera_y = 20;
    gfx_pset(15, 25, 6);
    ASSERT_EQ(6, gfx_t_scr(5, 5));
    ASSERT_EQ(1, gfx_t_count(6));
    p8_ram.mem.draw.camera_x = 0;
    p8_ram.mem.draw.camera_y = 0;
    p8_ram.mem.draw.clip_x0 = 2;
    p8_ram.mem.draw.clip_y0 = 2;
    p8_ram.mem.draw.clip_x1 = 4;
    p8_ram.mem.draw.clip_y1 = 4;
    gfx_pset(1, 2, 8);
    gfx_pset(2, 1, 8);
    gfx_pset(4, 3, 8);
    gfx_pset(3, 4, 8);
    ASSERT_EQ(0, gfx_t_count(8));
    gfx_pset(2, 2, 8);
    gfx_pset(3, 3, 8);
    ASSERT_EQ(2, gfx_t_count(8));
    PASS();
}

TEST(gfx_pset_offscreen_noop) {
    gfx_t_reset();
    gfx_pset(-1, 0, 5);
    gfx_pset(0, -1, 5);
    gfx_pset(128, 0, 5);
    gfx_pset(0, 128, 5);
    gfx_pset(127, 128, 5);
    ASSERT_EQ(128 * 128, gfx_t_count(0));
    gfx_pset(127, 127, 5);
    ASSERT_EQ(5, gfx_t_scr(127, 127));
    PASS();
}

TEST(gfx_pget_raw_and_bounds) {
    gfx_t_reset();
    gfx_t_set_scr(1, 1, 6);
    gfx_t_set_scr(2, 1, 11);
    p8_ram.mem.draw.draw_pal[6] = 2;
    p8_ram.mem.draw.clip_x0 = 50;
    p8_ram.mem.draw.clip_y0 = 50;
    ASSERT_EQ(6, gfx_pget(1, 1));
    ASSERT_EQ(11, gfx_pget(2, 1));
    p8_ram.mem.draw.camera_x = 4;
    p8_ram.mem.draw.camera_y = 4;
    ASSERT_EQ(6, gfx_pget(5, 5));
    ASSERT_EQ(0, gfx_pget(-1 + 4, 5)); /* screen (-1,1) is off-screen */
    ASSERT_EQ(0, gfx_pget(4 + 128, 4));
    ASSERT_EQ(0, gfx_pget(4, 4 + 128));
    PASS();
}

TEST(gfx_sget_sset) {
    int i;
    int sum = 0;
    gfx_t_reset();
    gfx_sset(3, 4, 0xB);
    gfx_sset(2, 4, 5);
    ASSERT_EQ(0xB5, p8_ram.mem.gfx[4 * 64 + 1]);
    ASSERT_EQ(0xB, gfx_sget(3, 4));
    ASSERT_EQ(5, gfx_sget(2, 4));
    gfx_t_reset();
    gfx_sset(128, 0, 1);
    gfx_sset(0, 128, 1);
    gfx_sset(-1, 0, 1);
    gfx_sset(0, -1, 1);
    for (i = 0; i < 0x2000; i++) sum += p8_ram.raw[i];
    ASSERT_EQ(0, sum);
    p8_ram.raw[64] = 0x55;
    ASSERT_EQ(0, gfx_sget(128, 0));
    ASSERT_EQ(0, gfx_sget(-1, 1));
    ASSERT_EQ(0, gfx_sget(0, 128));
    PASS();
}

TEST(gfx_mget_mset_rows_and_bounds) {
    gfx_t_reset();
    gfx_mset(5, 3, 9);
    ASSERT_EQ(9, p8_ram.raw[0x2000 + 3 * 128 + 5]);
    ASSERT_EQ(9, gfx_mget(5, 3));
    gfx_mset(7, 40, 21);
    ASSERT_EQ(21, p8_ram.raw[0x1000 + 8 * 128 + 7]);
    ASSERT_EQ(21, gfx_mget(7, 40));
    ASSERT_EQ(21, p8_ram.mem.gfx_map[8 * 128 + 7]);
    /* out of range: no-op / 0 */
    gfx_mset(128, 0, 7);
    gfx_mset(0, 64, 7);
    gfx_mset(-1, 0, 7);
    gfx_mset(0, -1, 7);
    ASSERT_EQ(0, p8_ram.raw[0x2000]);
    ASSERT_EQ(0, p8_ram.raw[0x2080]);
    ASSERT_EQ(0, p8_ram.raw[0x1FFF]);
    p8_ram.raw[0x2080] = 5;
    ASSERT_EQ(0, gfx_mget(128, 0));
    ASSERT_EQ(0, gfx_mget(0, 64));
    ASSERT_EQ(0, gfx_mget(-1, 1));
    ASSERT_EQ(0, gfx_mget(0, -1));
    PASS();
}

/* ------------------------------------------------------------------ */
/* spr                                                                */
/* ------------------------------------------------------------------ */

TEST(gfx_spr_basic_position) {
    gfx_t_reset();
    gfx_t_fill_spr(1, 5);
    gfx_spr(1, 20, 30, 8, 8, 0, 0);
    ASSERT_TRUE(gfx_t_rect(20, 30, 28, 38, 5));
    ASSERT_EQ(64, gfx_t_count(5));
    PASS();
}

TEST(gfx_spr_sheet_origin_for_n) {
    gfx_t_reset();
    gfx_t_fill_spr(0, 1);
    gfx_t_fill_spr(17, 2); /* sheet (8,8) */
    gfx_spr(17, 0, 0, 8, 8, 0, 0);
    ASSERT_TRUE(gfx_t_rect(0, 0, 8, 8, 2));
    ASSERT_EQ(0, gfx_t_count(1));
    PASS();
}

TEST(gfx_spr_transparent_color0) {
    int x, y;
    gfx_t_reset();
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++)
            gfx_t_set_sheet(x, y, ((x + y) & 1) ? 6 : 0);
    gfx_t_fill_scr(9);
    gfx_spr(0, 0, 0, 8, 8, 0, 0);
    ASSERT_EQ(9, gfx_t_scr(0, 0));
    ASSERT_EQ(6, gfx_t_scr(1, 0));
    ASSERT_EQ(6, gfx_t_scr(0, 1));
    ASSERT_EQ(9, gfx_t_scr(1, 1));
    ASSERT_EQ(32, gfx_t_count(6));
    PASS();
}

TEST(gfx_spr_palette_and_transparency) {
    gfx_t_reset();
    gfx_t_fill_spr(0, 5);
    p8_ram.mem.draw.draw_pal[5] = 12;
    gfx_spr(0, 0, 0, 8, 8, 0, 0);
    ASSERT_TRUE(gfx_t_rect(0, 0, 8, 8, 12));
    gfx_t_fill_scr(2);
    p8_ram.mem.draw.draw_pal[5] = 12 | GFX_TRANSPARENT;
    gfx_spr(0, 0, 0, 8, 8, 0, 0);
    ASSERT_EQ(0, gfx_t_count(12));
    ASSERT_EQ(128 * 128, gfx_t_count(2));
    /* color 0 opaque once its transparency bit is cleared */
    gfx_t_fill_spr(0, 0);
    p8_ram.mem.draw.draw_pal[0] = 0;
    gfx_t_fill_scr(2);
    gfx_spr(0, 0, 0, 8, 8, 0, 0);
    ASSERT_TRUE(gfx_t_rect(0, 0, 8, 8, 0));
    ASSERT_EQ(2, gfx_t_scr(8, 0));
    PASS();
}

TEST(gfx_spr_clip_rect) {
    gfx_t_reset();
    gfx_t_fill_spr(0, 4);
    p8_ram.mem.draw.clip_x0 = 4;
    p8_ram.mem.draw.clip_y0 = 4;
    p8_ram.mem.draw.clip_x1 = 12;
    p8_ram.mem.draw.clip_y1 = 12;
    gfx_spr(0, 0, 0, 8, 8, 0, 0);
    ASSERT_TRUE(gfx_t_rect(4, 4, 8, 8, 4));
    ASSERT_EQ(16, gfx_t_count(4));
    gfx_t_fill_scr(0);
    /* clip on the right/bottom edges (exclusive) */
    gfx_spr(0, 8, 8, 8, 8, 0, 0);
    ASSERT_TRUE(gfx_t_rect(8, 8, 12, 12, 4));
    ASSERT_EQ(16, gfx_t_count(4));
    PASS();
}

TEST(gfx_spr_negative_origin_screen_edge) {
    gfx_t_reset();
    gfx_t_fill_spr(0, 3);
    gfx_spr(0, -4, -4, 8, 8, 0, 0);
    ASSERT_TRUE(gfx_t_rect(0, 0, 4, 4, 3));
    ASSERT_EQ(16, gfx_t_count(3));
    gfx_t_fill_scr(0);
    gfx_spr(0, 124, 124, 8, 8, 0, 0);
    ASSERT_TRUE(gfx_t_rect(124, 124, 128, 128, 3));
    ASSERT_EQ(16, gfx_t_count(3));
    PASS();
}

TEST(gfx_spr_camera) {
    gfx_t_reset();
    gfx_t_fill_spr(0, 7);
    p8_ram.mem.draw.camera_x = 10;
    p8_ram.mem.draw.camera_y = 20;
    gfx_spr(0, 15, 25, 8, 8, 0, 0);
    ASSERT_TRUE(gfx_t_rect(5, 5, 13, 13, 7));
    ASSERT_EQ(64, gfx_t_count(7));
    PASS();
}

TEST(gfx_spr_flip_x_y) {
    int x, y;
    gfx_t_reset();
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++)
            gfx_t_set_sheet(8 + x, y, x + 1);
    gfx_spr(1, 0, 0, 8, 8, 1, 0);
    ASSERT_EQ(8, gfx_t_scr(0, 0));
    ASSERT_EQ(1, gfx_t_scr(7, 0));
    ASSERT_EQ(8, gfx_t_scr(0, 7));
    gfx_t_fill_scr(0);
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++)
            gfx_t_set_sheet(8 + x, y, y + 1);
    gfx_spr(1, 0, 0, 8, 8, 0, 1);
    ASSERT_EQ(8, gfx_t_scr(0, 0));
    ASSERT_EQ(1, gfx_t_scr(0, 7));
    ASSERT_EQ(1, gfx_t_scr(7, 7));
    PASS();
}

TEST(gfx_spr_wide_flip_whole_block) {
    gfx_t_reset();
    gfx_t_fill_spr(0, 1);
    gfx_t_fill_spr(1, 2);
    gfx_t_set_sheet(0, 0, 3);
    gfx_spr(0, 0, 0, 16, 8, 0, 0);
    ASSERT_EQ(3, gfx_t_scr(0, 0));
    ASSERT_EQ(1, gfx_t_scr(1, 0));
    ASSERT_EQ(2, gfx_t_scr(8, 0));
    gfx_t_fill_scr(0);
    gfx_spr(0, 0, 0, 16, 8, 1, 0);
    ASSERT_EQ(2, gfx_t_scr(0, 0));
    ASSERT_EQ(2, gfx_t_scr(7, 0));
    ASSERT_EQ(1, gfx_t_scr(8, 0));
    ASSERT_EQ(1, gfx_t_scr(14, 0));
    ASSERT_EQ(3, gfx_t_scr(15, 0));
    PASS();
}

TEST(gfx_spr_small_size_and_bad_n) {
    gfx_t_reset();
    gfx_t_fill_spr(0, 6);
    gfx_spr(0, 10, 10, 4, 4, 0, 0);
    ASSERT_TRUE(gfx_t_rect(10, 10, 14, 14, 6));
    ASSERT_EQ(16, gfx_t_count(6));
    gfx_t_fill_scr(0);
    gfx_spr(256, 0, 0, 8, 8, 0, 0);
    gfx_spr(-1, 0, 0, 8, 8, 0, 0);
    gfx_spr(1000, 0, 0, 8, 8, 0, 0);
    ASSERT_EQ(128 * 128, gfx_t_count(0));
    PASS();
}

/* ------------------------------------------------------------------ */
/* sspr                                                               */
/* ------------------------------------------------------------------ */

TEST(gfx_sspr_one_to_one) {
    int i;
    gfx_t_reset();
    for (i = 0; i < 4; i++) gfx_t_set_sheet(10 + i, 20, i + 1);
    gfx_sspr(10, 20, 4, 1, 50, 60, 4, 1, 0, 0);
    ASSERT_EQ(1, gfx_t_scr(50, 60));
    ASSERT_EQ(4, gfx_t_scr(53, 60));
    ASSERT_EQ(0, gfx_t_scr(49, 60));
    ASSERT_EQ(0, gfx_t_scr(54, 60));
    ASSERT_EQ(0, gfx_t_scr(50, 61));
    ASSERT_EQ(0, gfx_t_scr(50, 59));
    PASS();
}

TEST(gfx_sspr_upscale_2x) {
    gfx_t_reset();
    gfx_t_set_sheet(0, 0, 1);
    gfx_t_set_sheet(1, 0, 2);
    gfx_t_set_sheet(0, 1, 3);
    gfx_t_set_sheet(1, 1, 4);
    gfx_sspr(0, 0, 2, 2, 10, 10, 4, 4, 0, 0);
    ASSERT_TRUE(gfx_t_rect(10, 10, 12, 12, 1));
    ASSERT_TRUE(gfx_t_rect(12, 10, 14, 12, 2));
    ASSERT_TRUE(gfx_t_rect(10, 12, 12, 14, 3));
    ASSERT_TRUE(gfx_t_rect(12, 12, 14, 14, 4));
    ASSERT_EQ(0, gfx_t_scr(9, 10));
    ASSERT_EQ(0, gfx_t_scr(14, 10));
    ASSERT_EQ(0, gfx_t_scr(10, 14));
    ASSERT_EQ(0, gfx_t_scr(10, 9));
    PASS();
}

TEST(gfx_sspr_downscale_2x) {
    int i;
    gfx_t_reset();
    for (i = 0; i < 4; i++) {
        gfx_t_set_sheet(i, 0, i + 1);
        gfx_t_set_sheet(i, 1, i + 5);
    }
    gfx_sspr(0, 0, 4, 2, 0, 0, 2, 1, 0, 0);
    ASSERT_EQ(1, gfx_t_scr(0, 0));
    ASSERT_EQ(3, gfx_t_scr(1, 0));
    ASSERT_EQ(0, gfx_t_scr(2, 0));
    ASSERT_EQ(0, gfx_t_scr(0, 1));
    PASS();
}

TEST(gfx_sspr_negative_dw_dh_flip) {
    int i;
    gfx_t_reset();
    for (i = 0; i < 4; i++) {
        gfx_t_set_sheet(i, 0, i + 1);
        gfx_t_set_sheet(20, i, i + 1);
    }
    /* dx is the RIGHT edge: span 7..10 */
    gfx_sspr(0, 0, 4, 1, 10, 5, -4, 1, 0, 0);
    ASSERT_EQ(4, gfx_t_scr(7, 5));
    ASSERT_EQ(3, gfx_t_scr(8, 5));
    ASSERT_EQ(2, gfx_t_scr(9, 5));
    ASSERT_EQ(1, gfx_t_scr(10, 5));
    ASSERT_EQ(0, gfx_t_scr(6, 5));
    ASSERT_EQ(0, gfx_t_scr(11, 5));
    /* dy is the BOTTOM edge: span 7..10 */
    gfx_sspr(20, 0, 1, 4, 30, 10, 1, -4, 0, 0);
    ASSERT_EQ(4, gfx_t_scr(30, 7));
    ASSERT_EQ(1, gfx_t_scr(30, 10));
    ASSERT_EQ(0, gfx_t_scr(30, 6));
    ASSERT_EQ(0, gfx_t_scr(30, 11));
    /* explicit flip_x with positive dw */
    gfx_t_fill_scr(0);
    gfx_sspr(0, 0, 4, 1, 40, 5, 4, 1, 1, 0);
    ASSERT_EQ(4, gfx_t_scr(40, 5));
    ASSERT_EQ(1, gfx_t_scr(43, 5));
    PASS();
}

TEST(gfx_sspr_outside_sheet_is_color0) {
    int i;
    gfx_t_reset();
    for (i = 124; i < 128; i++) gfx_t_set_sheet(i, 0, 5);
    gfx_t_fill_scr(9);
    gfx_sspr(124, 0, 8, 1, 0, 0, 8, 1, 0, 0);
    ASSERT_TRUE(gfx_t_rect(0, 0, 4, 1, 5));
    ASSERT_TRUE(gfx_t_rect(4, 0, 8, 1, 9));
    PASS();
}

TEST(gfx_sspr_camera_clip_palette) {
    gfx_t_reset();
    gfx_t_set_sheet(0, 0, 1);
    gfx_t_set_sheet(1, 0, 1);
    gfx_t_set_sheet(0, 1, 1);
    gfx_t_set_sheet(1, 1, 1);
    p8_ram.mem.draw.camera_x = 10;
    p8_ram.mem.draw.camera_y = 10;
    p8_ram.mem.draw.draw_pal[1] = 7;
    p8_ram.mem.draw.clip_x1 = 1;
    gfx_sspr(0, 0, 2, 2, 10, 10, 2, 2, 0, 0);
    ASSERT_EQ(7, gfx_t_scr(0, 0));
    ASSERT_EQ(7, gfx_t_scr(0, 1));
    ASSERT_EQ(0, gfx_t_scr(1, 0));
    ASSERT_EQ(0, gfx_t_scr(1, 1));
    ASSERT_EQ(2, gfx_t_count(7));
    PASS();
}

/* ------------------------------------------------------------------ */
/* map                                                                */
/* ------------------------------------------------------------------ */

TEST(gfx_map_basic_and_tile0) {
    gfx_t_reset();
    gfx_t_fill_spr(0, 7); /* tile 0 must never be drawn */
    gfx_t_fill_spr(1, 5);
    gfx_mset(0, 0, 1);
    gfx_mset(1, 0, 1);
    gfx_map(0, 0, 0, 0, 3, 1, 0);
    ASSERT_TRUE(gfx_t_rect(0, 0, 16, 8, 5));
    ASSERT_EQ(0, gfx_t_count(7));
    ASSERT_EQ(128, gfx_t_count(5));
    PASS();
}

TEST(gfx_map_offsets_and_camera) {
    gfx_t_reset();
    gfx_t_fill_spr(1, 5);
    gfx_mset(3, 2, 1);
    gfx_mset(4, 3, 1);
    gfx_map(3, 2, 10, 20, 1, 1, 0);
    ASSERT_TRUE(gfx_t_rect(10, 20, 18, 28, 5));
    ASSERT_EQ(64, gfx_t_count(5));
    gfx_t_fill_scr(0);
    gfx_map(3, 2, 10, 20, 2, 2, 0);
    ASSERT_TRUE(gfx_t_rect(10, 20, 18, 28, 5));
    ASSERT_TRUE(gfx_t_rect(18, 28, 26, 36, 5));
    ASSERT_EQ(128, gfx_t_count(5));
    gfx_t_fill_scr(0);
    p8_ram.mem.draw.camera_x = 4;
    p8_ram.mem.draw.camera_y = 6;
    gfx_map(3, 2, 10, 20, 1, 1, 0);
    ASSERT_TRUE(gfx_t_rect(6, 14, 14, 22, 5));
    ASSERT_EQ(64, gfx_t_count(5));
    PASS();
}

TEST(gfx_map_layers_any_bit) {
    gfx_t_reset();
    gfx_t_fill_spr(1, 5);
    gfx_t_fill_spr(2, 6);
    gfx_t_fill_spr(3, 7);
    p8_ram.mem.gfx_flags[1] = 0x02;
    p8_ram.mem.gfx_flags[2] = 0x04;
    p8_ram.mem.gfx_flags[3] = 0x00;
    gfx_mset(0, 0, 1);
    gfx_mset(1, 0, 2);
    gfx_mset(2, 0, 3);
    gfx_map(0, 0, 0, 0, 3, 1, 0x5);
    ASSERT_EQ(0, gfx_t_count(5));
    ASSERT_EQ(64, gfx_t_count(6));
    ASSERT_TRUE(gfx_t_rect(8, 0, 16, 8, 6));
    ASSERT_EQ(0, gfx_t_count(7));
    gfx_t_fill_scr(0);
    gfx_map(0, 0, 0, 0, 3, 1, 0);
    ASSERT_TRUE(gfx_t_rect(0, 0, 8, 8, 5));
    ASSERT_TRUE(gfx_t_rect(8, 0, 16, 8, 6));
    ASSERT_TRUE(gfx_t_rect(16, 0, 24, 8, 7));
    PASS();
}

TEST(gfx_map_outside_and_lower_rows) {
    gfx_t_reset();
    gfx_t_fill_spr(1, 5);
    gfx_mset(0, 0, 1);
    gfx_mset(127, 0, 1);
    /* cx = -1: cell -1 draws nothing, cell 0 lands at sx + 8 */
    gfx_map(-1, 0, 0, 0, 2, 1, 0);
    ASSERT_EQ(64, gfx_t_count(5));
    ASSERT_TRUE(gfx_t_rect(8, 0, 16, 8, 5));
    gfx_t_fill_scr(0);
    /* cx = 127: cell 128 draws nothing */
    gfx_map(127, 0, 0, 0, 2, 1, 0);
    ASSERT_EQ(64, gfx_t_count(5));
    ASSERT_TRUE(gfx_t_rect(0, 0, 8, 8, 5));
    gfx_t_fill_scr(0);
    /* rows 32-63 come from 0x1000 */
    gfx_t_reset();
    gfx_t_fill_spr(1, 5);
    p8_ram.raw[0x1000 + 1 * 128 + 2] = 1; /* map (2,33) */
    gfx_map(0, 32, 0, 0, 4, 2, 0);
    ASSERT_TRUE(gfx_t_rect(16, 8, 24, 16, 5));
    ASSERT_EQ(64, gfx_t_count(5));
    PASS();
}

/* ------------------------------------------------------------------ */
/* pal / palt reset                                                   */
/* ------------------------------------------------------------------ */

TEST(gfx_reset_pal_and_palt) {
    int i;
    gfx_t_reset();
    for (i = 0; i < 16; i++) {
        p8_ram.mem.draw.draw_pal[i] = (uint8_t)(15 - i);
        p8_ram.mem.draw.screen_pal[i] = (uint8_t)(15 - i);
    }
    gfx_reset_palt();
    ASSERT_EQ(15 | GFX_TRANSPARENT, p8_ram.mem.draw.draw_pal[0]);
    ASSERT_EQ(14, p8_ram.mem.draw.draw_pal[1]);
    ASSERT_EQ(0, p8_ram.mem.draw.draw_pal[15]);
    ASSERT_EQ(15, p8_ram.mem.draw.screen_pal[0]);
    /* palt reset clears transparency but keeps the mapped color */
    p8_ram.mem.draw.draw_pal[4] = 4 | GFX_TRANSPARENT;
    gfx_reset_palt();
    ASSERT_EQ(4, p8_ram.mem.draw.draw_pal[4]);
    gfx_reset_pal();
    ASSERT_EQ(GFX_TRANSPARENT, p8_ram.mem.draw.draw_pal[0]);
    for (i = 1; i < 16; i++) {
        ASSERT_EQ(i, p8_ram.mem.draw.draw_pal[i]);
        ASSERT_EQ(i, p8_ram.mem.draw.screen_pal[i]);
    }
    PASS();
}

/* ------------------------------------------------------------------ */
/* builtins                                                           */
/* ------------------------------------------------------------------ */

TEST(gfx_b_spr_size_in_sprites) {
    P386VMState vm;
    P386Value a[8];
    gfx_t_reset();
    p386_vm_init(&vm);
    gfx_t_fill_spr(1, 5);
    gfx_t_fill_spr(2, 6);
    BNUM(a, 0, 1); BNUM(a, 1, 10); BNUM(a, 2, 10);
    a[3].value = GFP_HALF; a[3].tag = P386_TAG_NUM; /* w = 0.5 */
    BNUM(a, 4, 1);
    ASSERT_EQ(0, p386_builtin_spr(&vm, a, 5, 0));
    ASSERT_TRUE(gfx_t_rect(10, 10, 14, 18, 5));
    ASSERT_EQ(32, gfx_t_count(5));
    gfx_t_fill_scr(0);
    BNUM(a, 0, 1); BNUM(a, 1, 0); BNUM(a, 2, 0);
    BNUM(a, 3, 2); BNUM(a, 4, 1);
    p386_builtin_spr(&vm, a, 5, 0);
    ASSERT_TRUE(gfx_t_rect(0, 0, 8, 8, 5));
    ASSERT_TRUE(gfx_t_rect(8, 0, 16, 8, 6)); /* w=2 also draws sprite 2 */
    PASS();
}

TEST(gfx_b_spr_defaults_and_floor) {
    P386VMState vm;
    P386Value a[8];
    gfx_t_reset();
    p386_vm_init(&vm);
    gfx_t_fill_spr(1, 5);
    BNUM(a, 0, 1); BNUM(a, 1, 20); BNUM(a, 2, 30);
    p386_builtin_spr(&vm, a, 3, 0);
    ASSERT_TRUE(gfx_t_rect(20, 30, 28, 38, 5));
    ASSERT_EQ(64, gfx_t_count(5));
    gfx_t_fill_scr(0);
    BNUM(a, 0, 1);
    a[1].value = P386_FP_INT(3) | GFP_HALF; a[1].tag = P386_TAG_NUM;
    BNUM(a, 2, 0);
    p386_builtin_spr(&vm, a, 3, 0);
    ASSERT_TRUE(gfx_t_rect(3, 0, 11, 8, 5));
    ASSERT_EQ(0, gfx_t_scr(2, 0));
    ASSERT_EQ(0, gfx_t_scr(11, 0));
    gfx_t_fill_scr(0);
    BNUM(a, 0, 1);
    a[1].value = -GFP_HALF; a[1].tag = P386_TAG_NUM; /* -0.5 -> -1 */
    BNUM(a, 2, 0);
    p386_builtin_spr(&vm, a, 3, 0);
    ASSERT_TRUE(gfx_t_rect(0, 0, 7, 8, 5));
    ASSERT_EQ(0, gfx_t_scr(7, 0));
    PASS();
}

TEST(gfx_b_spr_flip_args) {
    P386VMState vm;
    P386Value a[8];
    int x;
    gfx_t_reset();
    p386_vm_init(&vm);
    for (x = 0; x < 8; x++) gfx_t_set_sheet(8 + x, 0, x + 1);
    BNUM(a, 0, 1); BNUM(a, 1, 0); BNUM(a, 2, 0);
    BNUM(a, 3, 1); BNUM(a, 4, 1);
    gfx_t_bool(a, 5, 1);
    gfx_t_bool(a, 6, 0);
    p386_builtin_spr(&vm, a, 7, 0);
    ASSERT_EQ(8, gfx_t_scr(0, 0));
    ASSERT_EQ(1, gfx_t_scr(7, 0));
    PASS();
}

TEST(gfx_b_map_no_args) {
    P386VMState vm;
    P386Value a[8];
    gfx_t_reset();
    p386_vm_init(&vm);
    gfx_t_fill_spr(1, 5);
    gfx_mset(0, 0, 1);
    gfx_mset(15, 15, 1);
    ASSERT_EQ(0, p386_builtin_map(&vm, a, 0, 0));
    ASSERT_TRUE(gfx_t_rect(0, 0, 8, 8, 5));
    ASSERT_TRUE(gfx_t_rect(120, 120, 128, 128, 5));
    ASSERT_EQ(128, gfx_t_count(5));
    PASS();
}

TEST(gfx_b_mget_fget_fset) {
    P386VMState vm;
    P386Value a[4];
    gfx_t_reset();
    p386_vm_init(&vm);
    gfx_mset(3, 2, 42);
    BNUM(a, 0, 3); BNUM(a, 1, 2);
    ASSERT_EQ(1, p386_builtin_mget(&vm, a, 2, 1));
    ASSERT_EQ(P386_TAG_NUM, a[0].tag);
    ASSERT_EQ(P386_FP_INT(42), a[0].value);
    /* fset(n, v) sets the byte */
    BNUM(a, 0, 7); BNUM(a, 1, 0x25);
    p386_builtin_fset(&vm, a, 2, 0);
    ASSERT_EQ(0x25, p8_ram.mem.gfx_flags[7]);
    BNUM(a, 0, 7);
    ASSERT_EQ(1, p386_builtin_fget(&vm, a, 1, 1));
    ASSERT_EQ(P386_TAG_NUM, a[0].tag);
    ASSERT_EQ(P386_FP_INT(0x25), a[0].value);
    /* fget(n, f) -> bool */
    BNUM(a, 0, 7); BNUM(a, 1, 5);
    ASSERT_EQ(1, p386_builtin_fget(&vm, a, 2, 1));
    ASSERT_EQ(P386_TAG_BOOL, a[0].tag);
    ASSERT_TRUE(a[0].value != 0);
    BNUM(a, 0, 7); BNUM(a, 1, 1);
    ASSERT_EQ(1, p386_builtin_fget(&vm, a, 2, 1));
    ASSERT_EQ(P386_TAG_BOOL, a[0].tag);
    ASSERT_EQ(0, a[0].value);
    /* fset(n, f, v) */
    BNUM(a, 0, 7); BNUM(a, 1, 1); gfx_t_bool(a, 2, 1);
    p386_builtin_fset(&vm, a, 3, 0);
    ASSERT_EQ(0x27, p8_ram.mem.gfx_flags[7]);
    BNUM(a, 0, 7); BNUM(a, 1, 5); gfx_t_bool(a, 2, 0);
    p386_builtin_fset(&vm, a, 3, 0);
    ASSERT_EQ(0x07, p8_ram.mem.gfx_flags[7]);
    PASS();
}

TEST(gfx_b_camera) {
    P386VMState vm;
    P386Value a[4];
    gfx_t_reset();
    p386_vm_init(&vm);
    BNUM(a, 0, 5); BNUM(a, 1, 6);
    ASSERT_EQ(2, p386_builtin_camera(&vm, a, 2, 2));
    ASSERT_EQ(P386_FP_INT(0), a[0].value);
    ASSERT_EQ(P386_FP_INT(0), a[1].value);
    ASSERT_EQ(5, p8_ram.mem.draw.camera_x);
    ASSERT_EQ(6, p8_ram.mem.draw.camera_y);
    BNUM(a, 0, -3); BNUM(a, 1, -4);
    ASSERT_EQ(2, p386_builtin_camera(&vm, a, 2, 2));
    ASSERT_EQ(P386_FP_INT(5), a[0].value);
    ASSERT_EQ(P386_FP_INT(6), a[1].value);
    ASSERT_EQ(-3, p8_ram.mem.draw.camera_x);
    ASSERT_EQ(-4, p8_ram.mem.draw.camera_y);
    gfx_pset(0, 0, 8);
    ASSERT_EQ(8, gfx_t_scr(3, 4));
    ASSERT_EQ(2, p386_builtin_camera(&vm, a, 0, 2));
    ASSERT_EQ(P386_FP_INT(-3), a[0].value);
    ASSERT_EQ(P386_FP_INT(-4), a[1].value);
    ASSERT_EQ(0, p8_ram.mem.draw.camera_x);
    ASSERT_EQ(0, p8_ram.mem.draw.camera_y);
    PASS();
}

TEST(gfx_b_clip) {
    P386VMState vm;
    P386Value a[6];
    gfx_t_reset();
    p386_vm_init(&vm);
    BNUM(a, 0, 10); BNUM(a, 1, 20); BNUM(a, 2, 30); BNUM(a, 3, 40);
    ASSERT_EQ(4, p386_builtin_clip(&vm, a, 4, 4));
    ASSERT_EQ(P386_FP_INT(0), a[0].value);
    ASSERT_EQ(P386_FP_INT(0), a[1].value);
    ASSERT_EQ(P386_FP_INT(128), a[2].value);
    ASSERT_EQ(P386_FP_INT(128), a[3].value);
    ASSERT_EQ(10, p8_ram.mem.draw.clip_x0);
    ASSERT_EQ(20, p8_ram.mem.draw.clip_y0);
    ASSERT_EQ(40, p8_ram.mem.draw.clip_x1);
    ASSERT_EQ(60, p8_ram.mem.draw.clip_y1);
    /* clamp to the screen */
    BNUM(a, 0, -5); BNUM(a, 1, -5); BNUM(a, 2, 300); BNUM(a, 3, 300);
    ASSERT_EQ(4, p386_builtin_clip(&vm, a, 4, 4));
    ASSERT_EQ(P386_FP_INT(10), a[0].value);
    ASSERT_EQ(P386_FP_INT(20), a[1].value);
    ASSERT_EQ(P386_FP_INT(40), a[2].value);
    ASSERT_EQ(P386_FP_INT(60), a[3].value);
    ASSERT_EQ(0, p8_ram.mem.draw.clip_x0);
    ASSERT_EQ(0, p8_ram.mem.draw.clip_y0);
    ASSERT_EQ(128, p8_ram.mem.draw.clip_x1);
    ASSERT_EQ(128, p8_ram.mem.draw.clip_y1);
    /* reset */
    p8_ram.mem.draw.clip_x0 = 3;
    p8_ram.mem.draw.clip_y1 = 9;
    ASSERT_EQ(4, p386_builtin_clip(&vm, a, 0, 4));
    ASSERT_EQ(P386_FP_INT(3), a[0].value);
    ASSERT_EQ(P386_FP_INT(9), a[3].value);
    ASSERT_EQ(0, p8_ram.mem.draw.clip_x0);
    ASSERT_EQ(128, p8_ram.mem.draw.clip_y1);
    /* clip applies to drawing */
    BNUM(a, 0, 1); BNUM(a, 1, 1); BNUM(a, 2, 2); BNUM(a, 3, 2);
    p386_builtin_clip(&vm, a, 4, 4);
    gfx_pset(0, 0, 8);
    gfx_pset(1, 1, 8);
    gfx_pset(2, 2, 8);
    gfx_pset(3, 3, 8);
    ASSERT_EQ(2, gfx_t_count(8));
    ASSERT_EQ(8, gfx_t_scr(1, 1));
    ASSERT_EQ(8, gfx_t_scr(2, 2));
    PASS();
}

TEST(gfx_b_clip_intersect) {
    P386VMState vm;
    P386Value a[6];
    gfx_t_reset();
    p386_vm_init(&vm);
    p8_ram.mem.draw.clip_x0 = 10;
    p8_ram.mem.draw.clip_y0 = 10;
    p8_ram.mem.draw.clip_x1 = 50;
    p8_ram.mem.draw.clip_y1 = 50;
    BNUM(a, 0, 0); BNUM(a, 1, 0); BNUM(a, 2, 30); BNUM(a, 3, 30);
    gfx_t_bool(a, 4, 1);
    ASSERT_EQ(4, p386_builtin_clip(&vm, a, 5, 4));
    ASSERT_EQ(P386_FP_INT(10), a[0].value);
    ASSERT_EQ(P386_FP_INT(50), a[2].value);
    ASSERT_EQ(10, p8_ram.mem.draw.clip_x0);
    ASSERT_EQ(10, p8_ram.mem.draw.clip_y0);
    ASSERT_EQ(30, p8_ram.mem.draw.clip_x1);
    ASSERT_EQ(30, p8_ram.mem.draw.clip_y1);
    PASS();
}

TEST(gfx_b_pal_draw_and_screen) {
    P386VMState vm;
    P386Value a[4];
    gfx_t_reset();
    p386_vm_init(&vm);
    BNUM(a, 0, 3); BNUM(a, 1, 9);
    ASSERT_EQ(1, p386_builtin_pal(&vm, a, 2, 1));
    ASSERT_EQ(P386_TAG_NUM, a[0].tag);
    ASSERT_EQ(P386_FP_INT(3), a[0].value);
    ASSERT_EQ(9, p8_ram.mem.draw.draw_pal[3]);
    /* keeps transparency bit */
    p8_ram.mem.draw.draw_pal[2] = 2 | GFX_TRANSPARENT;
    BNUM(a, 0, 2); BNUM(a, 1, 5);
    p386_builtin_pal(&vm, a, 2, 1);
    ASSERT_EQ(5 | GFX_TRANSPARENT, p8_ram.mem.draw.draw_pal[2]);
    /* screen palette */
    BNUM(a, 0, 4); BNUM(a, 1, 12); BNUM(a, 2, 1);
    p386_builtin_pal(&vm, a, 3, 0);
    ASSERT_EQ(12, p8_ram.mem.draw.screen_pal[4]);
    ASSERT_EQ(4, p8_ram.mem.draw.draw_pal[4]);
    PASS();
}

TEST(gfx_b_pal_resets) {
    P386VMState vm;
    P386Value a[4];
    int i;
    gfx_t_reset();
    p386_vm_init(&vm);
    p8_ram.mem.draw.draw_pal[1] = 4;
    p8_ram.mem.draw.screen_pal[1] = 7;
    BNUM(a, 0, 0);
    p386_builtin_pal(&vm, a, 1, 0);
    ASSERT_EQ(1, p8_ram.mem.draw.draw_pal[1] & 15);
    ASSERT_EQ(7, p8_ram.mem.draw.screen_pal[1]);
    p8_ram.mem.draw.draw_pal[1] = 4;
    p8_ram.mem.draw.draw_pal[0] = 0;
    p386_builtin_pal(&vm, a, 0, 0);
    ASSERT_EQ(GFX_TRANSPARENT, p8_ram.mem.draw.draw_pal[0]);
    for (i = 1; i < 16; i++) {
        ASSERT_EQ(i, p8_ram.mem.draw.draw_pal[i]);
        ASSERT_EQ(i, p8_ram.mem.draw.screen_pal[i]);
    }
    PASS();
}

TEST(gfx_b_palt) {
    P386VMState vm;
    P386Value a[4];
    gfx_t_reset();
    p386_vm_init(&vm);
    /* palt(c, t) returns previous as bool */
    BNUM(a, 0, 0); gfx_t_bool(a, 1, 0);
    ASSERT_EQ(1, p386_builtin_palt(&vm, a, 2, 1));
    ASSERT_EQ(P386_TAG_BOOL, a[0].tag);
    ASSERT_TRUE(a[0].value != 0);
    ASSERT_EQ(0, p8_ram.mem.draw.draw_pal[0] & GFX_TRANSPARENT);
    BNUM(a, 0, 0); gfx_t_bool(a, 1, 0);
    ASSERT_EQ(1, p386_builtin_palt(&vm, a, 2, 1));
    ASSERT_EQ(P386_TAG_BOOL, a[0].tag);
    ASSERT_EQ(0, a[0].value);
    BNUM(a, 0, 6); gfx_t_bool(a, 1, 1);
    p386_builtin_palt(&vm, a, 2, 1);
    ASSERT_EQ(6 | GFX_TRANSPARENT, p8_ram.mem.draw.draw_pal[6]);
    /* bitfield: bit 15 = color 0 ... bit 0 = color 15 */
    BNUM(a, 0, 0x4001);
    ASSERT_EQ(1, p386_builtin_palt(&vm, a, 1, 1));
    ASSERT_EQ(P386_TAG_NUM, a[0].tag);
    ASSERT_EQ(0, p8_ram.mem.draw.draw_pal[0] & GFX_TRANSPARENT);
    ASSERT_TRUE(p8_ram.mem.draw.draw_pal[1] & GFX_TRANSPARENT);
    ASSERT_TRUE(p8_ram.mem.draw.draw_pal[15] & GFX_TRANSPARENT);
    ASSERT_EQ(0, p8_ram.mem.draw.draw_pal[6] & GFX_TRANSPARENT);
    ASSERT_EQ(0, p8_ram.mem.draw.draw_pal[14] & GFX_TRANSPARENT);
    BNUM(a, 0, 0);
    ASSERT_EQ(1, p386_builtin_palt(&vm, a, 1, 1));
    ASSERT_EQ(P386_FP_INT(0x4001), a[0].value);
    ASSERT_EQ(0, p8_ram.mem.draw.draw_pal[1] & GFX_TRANSPARENT);
    ASSERT_EQ(0, p8_ram.mem.draw.draw_pal[15] & GFX_TRANSPARENT);
    PASS();
}

TEST(gfx_b_palt_reset_keeps_mapping) {
    P386VMState vm;
    P386Value a[2];
    gfx_t_reset();
    p386_vm_init(&vm);
    p8_ram.mem.draw.draw_pal[5] = 9 | GFX_TRANSPARENT;
    p8_ram.mem.draw.draw_pal[0] = 0;
    p386_builtin_palt(&vm, a, 0, 0);
    ASSERT_EQ(GFX_TRANSPARENT, p8_ram.mem.draw.draw_pal[0]);
    ASSERT_EQ(9, p8_ram.mem.draw.draw_pal[5]);
    PASS();
}

TEST(gfx_b_cls) {
    P386VMState vm;
    P386Value a[2];
    gfx_t_reset();
    p386_vm_init(&vm);
    p8_ram.mem.draw.clip_x0 = 5;
    p8_ram.mem.draw.clip_y0 = 6;
    p8_ram.mem.draw.clip_x1 = 7;
    p8_ram.mem.draw.clip_y1 = 8;
    BNUM(a, 0, 3);
    p386_builtin_cls(&vm, a, 1, 0);
    ASSERT_EQ(128 * 128, gfx_t_count(3));
    ASSERT_EQ(0, p8_ram.mem.draw.clip_x0);
    ASSERT_EQ(0, p8_ram.mem.draw.clip_y0);
    ASSERT_EQ(128, p8_ram.mem.draw.clip_x1);
    ASSERT_EQ(128, p8_ram.mem.draw.clip_y1);
    p386_builtin_cls(&vm, a, 0, 0);
    ASSERT_EQ(128 * 128, gfx_t_count(0));
    PASS();
}

TEST(gfx_b_abi_want_rets_zero) {
    P386VMState vm;
    P386Value a[4];
    gfx_t_reset();
    p386_vm_init(&vm);
    gfx_mset(3, 2, 42);
    BNUM(a, 0, 3); BNUM(a, 1, 2);
    ASSERT_EQ(1, p386_builtin_mget(&vm, a, 2, 0));
    ASSERT_EQ(P386_TAG_NUM, a[0].tag);
    ASSERT_EQ(P386_FP_INT(42), a[0].value);
    BNUM(a, 0, 7); BNUM(a, 1, 8);
    ASSERT_EQ(2, p386_builtin_camera(&vm, a, 2, 0));
    ASSERT_EQ(P386_FP_INT(0), a[0].value);
    ASSERT_EQ(P386_FP_INT(0), a[1].value);
    BNUM(a, 0, 1); BNUM(a, 1, 1);
    ASSERT_EQ(2, p386_builtin_camera(&vm, a, 2, 0));
    ASSERT_EQ(P386_FP_INT(7), a[0].value);
    ASSERT_EQ(P386_FP_INT(8), a[1].value);
    PASS();
}

/*
 * Registration order (RUN_TEST):
 *   gfx_pset_nibbles
 *   gfx_pset_ignores_transparency
 *   gfx_pset_palette_mapping
 *   gfx_pset_camera_and_clip
 *   gfx_pset_offscreen_noop
 *   gfx_pget_raw_and_bounds
 *   gfx_sget_sset
 *   gfx_mget_mset_rows_and_bounds
 *   gfx_spr_basic_position
 *   gfx_spr_sheet_origin_for_n
 *   gfx_spr_transparent_color0
 *   gfx_spr_palette_and_transparency
 *   gfx_spr_clip_rect
 *   gfx_spr_negative_origin_screen_edge
 *   gfx_spr_camera
 *   gfx_spr_flip_x_y
 *   gfx_spr_wide_flip_whole_block
 *   gfx_spr_small_size_and_bad_n
 *   gfx_sspr_one_to_one
 *   gfx_sspr_upscale_2x
 *   gfx_sspr_downscale_2x
 *   gfx_sspr_negative_dw_dh_flip
 *   gfx_sspr_outside_sheet_is_color0
 *   gfx_sspr_camera_clip_palette
 *   gfx_map_basic_and_tile0
 *   gfx_map_offsets_and_camera
 *   gfx_map_layers_any_bit
 *   gfx_map_outside_and_lower_rows
 *   gfx_reset_pal_and_palt
 *   gfx_b_spr_size_in_sprites
 *   gfx_b_spr_defaults_and_floor
 *   gfx_b_spr_flip_args
 *   gfx_b_map_no_args
 *   gfx_b_mget_fget_fset
 *   gfx_b_camera
 *   gfx_b_clip
 *   gfx_b_clip_intersect
 *   gfx_b_pal_draw_and_screen
 *   gfx_b_pal_resets
 *   gfx_b_palt
 *   gfx_b_palt_reset_keeps_mapping
 *   gfx_b_cls
 *   gfx_b_abi_want_rets_zero
 */

/* ------------------------------------------------------------------ */
/* Randomized differential tests: the fast paths in gfx.c against a    */
/* direct per-pixel model of spr/map. Each case compares the screen.   */
/* ------------------------------------------------------------------ */

static uint32_t gfx_rng = 12345;

static int gfx_rand(int n)
{
    gfx_rng = gfx_rng * 1103515245u + 12345u;
    return (int)((gfx_rng >> 16) % (uint32_t)n);
}

static uint8_t gfx_ref_screen[0x2000];

static void gfx_ref_set(int x, int y, int c)
{
    uint8_t *p = &gfx_ref_screen[y * 64 + (x >> 1)];
    if (x & 1) *p = (uint8_t)((*p & 0x0F) | (c << 4));
    else *p = (uint8_t)((*p & 0xF0) | c);
}

/* Reference spr: every destination pixel computed on its own. */
static void gfx_ref_spr(int n, int x, int y, int w, int h, int fx, int fy)
{
    P8DrawState *ds = &p8_ram.mem.draw;
    int i, j;
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            int X = x - ds->camera_x + i, Y = y - ds->camera_y + j;
            int u = (n % 16) * 8 + (fx ? w - 1 - i : i);
            int v = (n / 16) * 8 + (fy ? h - 1 - j : j);
            int c, m;
            if (X < ds->clip_x0 || Y < ds->clip_y0 ||
                X >= ds->clip_x1 || Y >= ds->clip_y1) continue;
            c = (u < 128 && v < 128) ? gfx_sget(u, v) : 0;
            m = ds->draw_pal[c];
            if (m & GFX_TRANSPARENT) continue;
            gfx_ref_set(X, Y, m & 15);
        }
    }
}

/* Random sheet, screen, palette/transparency, camera and clip. */
static void gfx_random_state(void)
{
    int i;
    P8DrawState *ds = &p8_ram.mem.draw;
    gfx_t_reset();
    for (i = 0; i < 0x2000; i++) p8_ram.mem.gfx[i] = (uint8_t)gfx_rand(256);
    for (i = 0; i < 0x2000; i++) p8_ram.mem.screen[i] = (uint8_t)gfx_rand(256);
    for (i = 0; i < 16; i++) {
        ds->draw_pal[i] = (uint8_t)(gfx_rand(4) == 0 ? i : gfx_rand(16));
        if (gfx_rand(4) == 0) ds->draw_pal[i] |= GFX_TRANSPARENT;
    }
    ds->camera_x = (int16_t)(gfx_rand(3) == 0 ? gfx_rand(21) - 10 : 0);
    ds->camera_y = (int16_t)(gfx_rand(3) == 0 ? gfx_rand(21) - 10 : 0);
    if (gfx_rand(3) == 0) {
        ds->clip_x0 = (uint8_t)gfx_rand(64);
        ds->clip_y0 = (uint8_t)gfx_rand(64);
        ds->clip_x1 = (uint8_t)(ds->clip_x0 + gfx_rand(129 - ds->clip_x0));
        ds->clip_y1 = (uint8_t)(ds->clip_y0 + gfx_rand(129 - ds->clip_y0));
    }
    memcpy(gfx_ref_screen, p8_ram.mem.screen, sizeof(gfx_ref_screen));
}

TEST(gfx_spr_matches_reference_random) {
    int iter;
    gfx_rng = 777;
    for (iter = 0; iter < 400; iter++) {
        int n = gfx_rand(256);
        int x = gfx_rand(150) - 12, y = gfx_rand(150) - 12;
        /* mostly 8x8 (tile path), some other sizes incl. odd widths */
        int w = gfx_rand(2) ? 8 : 1 + gfx_rand(16);
        int h = gfx_rand(2) ? 8 : 1 + gfx_rand(16);
        int fx = gfx_rand(2), fy = gfx_rand(2);
        gfx_random_state();
        gfx_ref_spr(n, x, y, w, h, fx, fy);
        gfx_spr(n, x, y, w, h, fx, fy);
        if (memcmp(gfx_ref_screen, p8_ram.mem.screen, sizeof(gfx_ref_screen)) != 0) {
            FAIL("spr differs from reference");
        }
    }
    PASS();
}

TEST(gfx_map_matches_reference_random) {
    int iter;
    gfx_rng = 4242;
    for (iter = 0; iter < 150; iter++) {
        int cx = gfx_rand(8), cy = gfx_rand(8);
        int sx = gfx_rand(40) - 12, sy = gfx_rand(40) - 12;
        int cw = 1 + gfx_rand(17), ch = 1 + gfx_rand(17);
        int layers = gfx_rand(3) == 0 ? gfx_rand(256) : 0;
        int i, j;
        gfx_random_state();
        for (i = 0; i < 256; i++) p8_ram.mem.gfx_flags[i] = (uint8_t)gfx_rand(256);
        for (j = 0; j < 32; j++)
            for (i = 0; i < 32; i++)
                gfx_mset(i, j, (uint8_t)(gfx_rand(3) == 0 ? 0 : gfx_rand(64)));
        for (j = 0; j < ch; j++) {
            for (i = 0; i < cw; i++) {
                int t = gfx_mget(cx + i, cy + j);
                if (t == 0) continue;
                if (layers && !(p8_ram.mem.gfx_flags[t] & layers)) continue;
                gfx_ref_spr(t, sx + i * 8, sy + j * 8, 8, 8, 0, 0);
            }
        }
        gfx_map(cx, cy, sx, sy, cw, ch, (uint8_t)layers);
        if (memcmp(gfx_ref_screen, p8_ram.mem.screen, sizeof(gfx_ref_screen)) != 0) {
            FAIL("map differs from reference");
        }
    }
    PASS();
}
