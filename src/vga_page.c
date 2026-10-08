#include <string.h>
#include "vga.h"
#include "timer.h"

/*
 * Triple-buffer page rotation.
 *
 * Pages are shown in order 0, 1, 2, 0, ... The page we blit into next was
 * last shown three flips ago, so it can only still be on screen if no
 * retrace has happened since the flip to the page after it. That flip is
 * usually two frames old, so vga_blit() almost never waits.
 *
 * "A retrace has happened since flip F" is true if either:
 *  - RETRACE_TICKS timer ticks have passed (2 ticks at 60 Hz is more than
 *    1/60 s, longer than one 70 Hz refresh; a 60 Hz mode would need 3), or
 *  - the display is in retrace now: vga_show_page() writes only during
 *    active display, so any retrace seen later began after the write.
 * Without the timer ISR the tick count stays still, and vga_blit() waits
 * for a retrace each time. That is slower, but still correct.
 */

#define RETRACE_TICKS 2

static uint8_t draw_page;               /* page vga_blit() writes next */
static uint8_t flipped[VGA_PAGES];      /* page has been flipped to */
static uint32_t flip_tick[VGA_PAGES];   /* timer tick of that flip */
static uint8_t dac_rgb6[16 * 3];        /* DAC colors 0-15 as programmed */

void vga_init(void) {
    uint8_t i;
    vga_mode_init();                    /* page 0 on screen */
    memcpy(dac_rgb6, p8_palette_rgb6, sizeof(dac_rgb6));
    for (i = 0; i < VGA_PAGES; i++) flipped[i] = 0;
    draw_page = 1;
}

static void wait_retrace_since_flip(uint8_t page) {
    if (!flipped[page]) return;
    while ((int32_t)(timer_ticks() - flip_tick[page]) < RETRACE_TICKS) {
        if (vga_in_retrace()) return;
    }
}

void vga_blit(const void *screen_buf) {
    wait_retrace_since_flip((uint8_t)((draw_page + 1) % VGA_PAGES));
    vga_blit_page(screen_buf, (uint32_t)draw_page * VGA_PAGE_SIZE);
}

void vga_flip(void) {
    vga_show_page((uint32_t)draw_page * VGA_PAGE_SIZE);
    flipped[draw_page] = 1;
    flip_tick[draw_page] = timer_ticks();
    draw_page = (uint8_t)((draw_page + 1) % VGA_PAGES);
}

/* ── screen palette ─────────────────────────────────────────────────── */

void vga_screen_pal_to_rgb6(const uint8_t screen_pal[16], uint8_t out[16 * 3]) {
    int i;
    for (i = 0; i < 16; i++) {
        /* Mask as fake-08: 0-15 standard, 128-143 the secret palette,
         * which vga_mode_init loads into DAC entries 16-31. */
        uint8_t v = (uint8_t)(screen_pal[i] & 0x8F);
        uint8_t idx = (uint8_t)((v & 0x80) ? 16 + (v & 0x0F) : v);
        memcpy(&out[i * 3], &p8_palette_rgb6[idx * 3], 3);
    }
}

/*
 * PICO-8 applies the screen palette when the frame is shown. Here the DAC
 * does it: reprogram entries 0-15 only when the mapping changes. The DAC
 * changes at once, so a new mapping also shows on the frame still on
 * screen for up to one refresh.
 */
void vga_apply_screen_pal(const uint8_t screen_pal[16]) {
    uint8_t rgb[16 * 3];
    vga_screen_pal_to_rgb6(screen_pal, rgb);
    if (memcmp(rgb, dac_rgb6, sizeof(rgb)) == 0) return;
    memcpy(dac_rgb6, rgb, sizeof(rgb));
    vga_set_palette(rgb, 16);
}
