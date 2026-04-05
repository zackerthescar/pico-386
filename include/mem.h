#ifndef _MEM_H
#define _MEM_H

#include <stdint.h>
#include <stddef.h>

/*
 * PICO-8 base RAM memory map (64KB)
 *
 * 0x0000  GFX (sprite sheet upper half)
 * 0x1000  GFX/MAP shared region
 * 0x2000  MAP (lower half)
 * 0x3000  Sprite flags
 * 0x3100  Music (song patterns)
 * 0x3200  SFX
 * 0x4300  User data
 * 0x5600  Custom font
 * 0x5E00  Persistent cart data
 * 0x5F00  Draw state
 * 0x5F40  Hardware state
 * 0x5F80  GPIO pins
 * 0x6000  Screen buffer (128x128, 4bpp packed)
 * 0x8000  Upper user data
 */

#pragma pack(push, 1)

typedef struct {
    uint8_t draw_pal[16];       /* 0x5F00 - draw palette + transparency  */
    uint8_t screen_pal[16];     /* 0x5F10 - screen palette               */
    uint8_t clip_x0;            /* 0x5F20 */
    uint8_t clip_y0;            /* 0x5F21 */
    uint8_t clip_x1;            /* 0x5F22 */
    uint8_t clip_y1;            /* 0x5F23 */
    uint8_t _pad0;              /* 0x5F24 */
    uint8_t pen_color;          /* 0x5F25 */
    uint8_t cursor_x;           /* 0x5F26 */
    uint8_t cursor_y;           /* 0x5F27 */
    int16_t camera_x;           /* 0x5F28 */
    int16_t camera_y;           /* 0x5F2A */
    uint8_t screen_mode;        /* 0x5F2C */
    uint8_t devkit_flags;       /* 0x5F2D */
    uint8_t persist_flags;      /* 0x5F2E */
    uint8_t _pad1;              /* 0x5F2F */
    uint8_t _pad2;              /* 0x5F30 */
    uint16_t fillp;             /* 0x5F31 */
    uint8_t fillp_flags;        /* 0x5F33 */
    uint8_t _pad3[12];          /* 0x5F34-0x5F3F */
} P8DrawState;

typedef struct {
    uint8_t audio_fx[4];        /* 0x5F40 */
    uint8_t _pad0[8];           /* 0x5F44 */
    uint8_t btn[8];             /* 0x5F4C - button state players 0-7     */
    uint8_t gfx_remap;          /* 0x5F54 */
    uint8_t screen_remap;       /* 0x5F55 */
    uint8_t map_remap;          /* 0x5F56 */
    uint8_t map_width;          /* 0x5F57 */
    uint8_t _pad1[4];           /* 0x5F58 */
    uint8_t btnp_delay;         /* 0x5F5C */
    uint8_t btnp_repeat;        /* 0x5F5D */
    uint8_t color_bitmask;      /* 0x5F5E */
    uint8_t _pad2[33];          /* 0x5F5F-0x5F7F */
} P8HwState;

typedef struct {
    uint8_t     gfx[0x1000];        /* 0x0000 - sprite sheet upper       */
    uint8_t     gfx_map[0x1000];    /* 0x1000 - shared sprite/map        */
    uint8_t     map[0x1000];        /* 0x2000 - map lower                */
    uint8_t     gfx_flags[0x100];   /* 0x3000 - sprite flags             */
    uint8_t     music[0x100];       /* 0x3100 - song patterns            */
    uint8_t     sfx[0x1100];        /* 0x3200 - sound effects            */
    uint8_t     userdata[0x1300];   /* 0x4300 - general purpose          */
    uint8_t     font[0x800];        /* 0x5600 - custom font              */
    uint8_t     cartdata[0x100];    /* 0x5E00 - persistent storage       */
    P8DrawState draw;               /* 0x5F00 - draw state               */
    P8HwState   hw;                 /* 0x5F40 - hardware state           */
    uint8_t     gpio[0x80];         /* 0x5F80 - GPIO pins                */
    uint8_t     screen[0x2000];     /* 0x6000 - framebuffer              */
    uint8_t     upper[0x8000];      /* 0x8000 - upper user data          */
} P8Mem;

#pragma pack(pop)

/* compile-time layout verification */
#define P8_STATIC_ASSERT(cond, msg) typedef char _sa_##msg[(cond)?1:-1]

P8_STATIC_ASSERT(sizeof(P8DrawState) == 0x40,  draw_state_size);
P8_STATIC_ASSERT(sizeof(P8HwState)   == 0x40,  hw_state_size);
P8_STATIC_ASSERT(sizeof(P8Mem)       == 0x10000, mem_total_size);

typedef union {
    uint8_t raw[0x10000];
    P8Mem   mem;
} P8Ram;

extern P8Ram p8_ram;

void p8_ram_init(void);

#endif
