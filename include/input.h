#ifndef _INPUT_H
#define _INPUT_H

#include <stdint.h>

/*
 * PICO-8 button state, sampled once per _update.
 *
 * This module has no hardware dependencies: the host fills a per-player
 * "down" bitmask (from the keyboard ISR, see kbd.h) and calls
 * p8_input_frame(). btn() reads p8_ram.mem.hw.btn[]; btnp() reads
 * p8_btnp_bits[].
 */

#define P8_PLAYERS 8
#define P8_BUTTONS 8

/* Button indices (bit positions in each player's mask). */
#define P8_BTN_LEFT  0
#define P8_BTN_RIGHT 1
#define P8_BTN_UP    2
#define P8_BTN_DOWN  3
#define P8_BTN_O     4
#define P8_BTN_X     5

/* Per-player btnp() result bits for the current frame. */
extern uint8_t p8_btnp_bits[P8_PLAYERS];

/* Advance one game frame. down[p] is the held-button mask of player p.
 * fps60 is nonzero when the cart uses _update60 (btnp delays double). */
void p8_input_frame(const uint8_t down[P8_PLAYERS], int fps60);

/* Clear all held/btnp state (cart start). */
void p8_input_reset(void);

#endif
