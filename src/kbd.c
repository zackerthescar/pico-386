#include <dos.h>
#include <conio.h>
#include <string.h>
#include "kbd.h"

#define KBD_DATA_PORT 0x60
#define PIC1_CMD      0x20
#define PIC_EOI       0x20

volatile uint8_t kbd_keys[256];

/* Set on every make code and cleared when read. A tap shorter than one
 * frame releases the key before the game loop samples kbd_keys; the latch
 * still reports it. */
static volatile uint8_t kbd_hit[256];

static void (__interrupt __far *old_int9)(void);
static volatile uint8_t ext_prefix;
static volatile uint8_t skip_bytes;
static int installed;

/* An IRQ that arrives in real mode runs the ISR on the extender's stack,
 * so the stack-overflow probe (which checks the main stack) must be off. */
#pragma off (check_stack)
static void __interrupt __far kbd_isr(void) {
    uint8_t sc = (uint8_t)inp(KBD_DATA_PORT);

    if (skip_bytes) {
        /* Pause sends E1 1D 45 E1 9D C5 and has no break code. */
        skip_bytes--;
    } else if (sc == 0xE0) {
        ext_prefix = 0x80;
    } else if (sc == 0xE1) {
        skip_bytes = 2;
    } else {
        uint8_t code = (uint8_t)((sc & 0x7F) | ext_prefix);
        ext_prefix = 0;
        /* E0 2A / E0 36 are fake shifts sent around the grey arrows when
         * NumLock is on; they must not press the real shift keys. */
        if (code != KBD_EXT(0x2A) && code != KBD_EXT(0x36)) {
            if (sc & 0x80) {
                kbd_keys[code] = 0;
            } else {
                kbd_keys[code] = 1;
                kbd_hit[code] = 1;
            }
        }
    }
    outp(PIC1_CMD, PIC_EOI);
}
#pragma on (check_stack)

void kbd_init(void) {
    if (installed) return;
    memset((void *)kbd_keys, 0, sizeof(kbd_keys));
    memset((void *)kbd_hit, 0, sizeof(kbd_hit));
    ext_prefix = 0;
    skip_bytes = 0;
    old_int9 = _dos_getvect(0x09);
    _dos_setvect(0x09, kbd_isr);
    installed = 1;
}

void kbd_shutdown(void) {
    if (!installed) return;
    _dos_setvect(0x09, old_int9);
    installed = 0;
}

typedef struct {
    uint8_t key;
    uint8_t player;
    uint8_t button;
} KeyBind;

/* PICO-8 default layout. Keypad arrows (NumLock off) also work for P0. */
static const KeyBind binds[] = {
    { KBD_LEFT,  0, P8_BTN_LEFT  }, { 0x4B, 0, P8_BTN_LEFT  },
    { KBD_RIGHT, 0, P8_BTN_RIGHT }, { 0x4D, 0, P8_BTN_RIGHT },
    { KBD_UP,    0, P8_BTN_UP    }, { 0x48, 0, P8_BTN_UP    },
    { KBD_DOWN,  0, P8_BTN_DOWN  }, { 0x50, 0, P8_BTN_DOWN  },
    { 0x2C,      0, P8_BTN_O     }, /* Z */
    { 0x2E,      0, P8_BTN_O     }, /* C */
    { 0x31,      0, P8_BTN_O     }, /* N */
    { 0x2D,      0, P8_BTN_X     }, /* X */
    { 0x2F,      0, P8_BTN_X     }, /* V */
    { 0x32,      0, P8_BTN_X     }, /* M */

    { 0x1F,      1, P8_BTN_LEFT  }, /* S */
    { 0x21,      1, P8_BTN_RIGHT }, /* F */
    { 0x12,      1, P8_BTN_UP    }, /* E */
    { 0x20,      1, P8_BTN_DOWN  }, /* D */
    { 0x2A,      1, P8_BTN_O     }, /* Left Shift */
    { 0x0F,      1, P8_BTN_O     }, /* Tab */
    { 0x1E,      1, P8_BTN_X     }, /* A */
    { 0x10,      1, P8_BTN_X     }  /* Q */
};

int kbd_take_hit(uint8_t key) {
    if (!kbd_hit[key]) return 0;
    kbd_hit[key] = 0;
    return 1;
}

void kbd_read_buttons(uint8_t down[P8_PLAYERS]) {
    unsigned i;
    memset(down, 0, P8_PLAYERS);
    for (i = 0; i < sizeof(binds) / sizeof(binds[0]); i++) {
        uint8_t key = binds[i].key;
        /* Evaluate both: the latch must be cleared even if still held. */
        if (kbd_take_hit(key) | kbd_keys[key]) {
            down[binds[i].player] |= (uint8_t)(1 << binds[i].button);
        }
    }
}
