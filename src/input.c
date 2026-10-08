#include <string.h>
#include "input.h"
#include "mem.h"

uint8_t p8_btnp_bits[P8_PLAYERS];

/* Frames each button has been held, kept bounded (see p8_input_frame). */
static uint16_t held[P8_PLAYERS][P8_BUTTONS];

void p8_input_reset(void) {
    memset(p8_btnp_bits, 0, sizeof(p8_btnp_bits));
    memset(held, 0, sizeof(held));
    memset(p8_ram.mem.hw.btn, 0, sizeof(p8_ram.mem.hw.btn));
}

/*
 * btnp() is true on the first frame of a press, then after `delay` frames
 * it repeats every `rep` frames. Delay and repeat come from 0x5F5C/0x5F5D
 * (0 = default 15/4; delay 255 = never repeat). Both are counted in 30fps
 * frames, so they double under _update60.
 */
void p8_input_frame(const uint8_t down[P8_PLAYERS], int fps60) {
    uint16_t delay = p8_ram.mem.hw.btnp_delay;
    uint16_t rep = p8_ram.mem.hw.btnp_repeat;
    int no_repeat = (delay == 255);
    int p, b;

    if (delay == 0) delay = 15;
    if (rep == 0) rep = 4;
    if (fps60) {
        delay *= 2;
        rep *= 2;
    }

    for (p = 0; p < P8_PLAYERS; p++) {
        uint8_t bits = 0;
        for (b = 0; b < P8_BUTTONS; b++) {
            uint16_t *h = &held[p][b];
            if (!(down[p] & (1 << b))) {
                *h = 0;
                continue;
            }
            (*h)++;
            /* Fold the counter back by one period so it never overflows:
             * delay+1 is the first repeat, delay+1+rep the next, etc. */
            if (!no_repeat && *h > delay + rep) *h -= rep;
            if (*h == 1 || (!no_repeat && *h == delay + 1)) bits |= (uint8_t)(1 << b);
            if (no_repeat && *h > 2) *h = 2;
        }
        p8_btnp_bits[p] = bits;
        p8_ram.mem.hw.btn[p] = down[p];
    }
}
