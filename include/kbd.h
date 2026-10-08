#ifndef _KBD_H
#define _KBD_H

#include <stdint.h>
#include "input.h"

/*
 * INT 9 keyboard handler. Replaces the BIOS handler while installed, so
 * DOS sees no keystrokes (and Ctrl-C/Ctrl-Break do nothing): Esc quits.
 *
 * Keys are indexed by set-1 make code; E0-prefixed (extended) keys have
 * bit 7 set, e.g. KBD_UP = 0xC8 and keypad 8 = 0x48.
 */

#define KBD_EXT(sc) ((uint8_t)((sc) | 0x80))

#define KBD_ESC     0x01
#define KBD_UP      KBD_EXT(0x48)
#define KBD_DOWN    KBD_EXT(0x50)
#define KBD_LEFT    KBD_EXT(0x4B)
#define KBD_RIGHT   KBD_EXT(0x4D)

extern volatile uint8_t kbd_keys[256];

void kbd_init(void);
void kbd_shutdown(void);

/* Nonzero if `key` was pressed since the last call (clears the latch). */
int kbd_take_hit(uint8_t key);

/* Fill down[] with each player's held-button mask (PICO-8 default keys).
 * A key tapped and released since the last call counts as held. */
void kbd_read_buttons(uint8_t down[P8_PLAYERS]);

#endif
