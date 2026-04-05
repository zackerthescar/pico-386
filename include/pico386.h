#ifndef _PICO386_H
#define _PICO386_H

#include <stdint.h>

#include "cart.h"
#include "mem.h"
#include "pico8.h"
#include "rust.h"
#include "serial.h"

/* Maximum PICO-8 Lua code size (64KB + null terminator) */
#define P8_CODE_MAX     0x10000
#define P8_CODE_ALLOC   (P8_CODE_MAX + 1)

/* Offset of code section in cartridge data */
#define P8_CODE_OFFSET  0x4300

/* Cartridge context — owns all memory related to a loaded cart */
typedef struct {
    uint8_t *cart_data;     /* raw cartridge bytes (160*205) */
    uint8_t *lua_code;      /* decompressed Lua source */
    P8Program program;      /* compiled bytecode (NULL if not compiled) */
} P386_Cart;

/* Load a .p8.png cartridge from disk into ctx.
 * Returns 0 on success, negative on error. */
int p386_cart_load(P386_Cart *ctx, const char *filename);

/* Compile the Lua code in a loaded cart.
 * Returns 0 on success, negative on error. */
int p386_cart_compile(P386_Cart *ctx);

/* Free all resources owned by ctx. Safe to call on a zeroed struct. */
void p386_cart_free(P386_Cart *ctx);

#endif
