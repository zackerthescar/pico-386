#include <stdint.h>
#include "p386_builtins.h"
#include "mem.h"

static void set_nil_results(P386Value *args, uint8_t want_rets) {
    uint8_t i;
    if (!args) return;
    for (i = 0; i < want_rets; i++) {
        args[i].value = 0;
        args[i].tag = P386_TAG_NIL;
    }
}

static int32_t arg_num(P386Value *args, uint8_t nargs, uint8_t idx, int32_t def) {
    if (!args || idx >= nargs || args[idx].tag == P386_TAG_NIL) return def;
    if (args[idx].tag != P386_TAG_NUM) return def;
    return args[idx].value >> 16;
}

static void p8_set_pixel(int32_t x, int32_t y, uint8_t color) {
    uint32_t off;
    if (x < 0 || y < 0 || x >= 128 || y >= 128) return;
    color &= 0x0f;
    off = (uint32_t)y * 64U + ((uint32_t)x >> 1);
    color = p8_ram.mem.draw.draw_pal[color];
    if (color & 0x10) return;
    color &= 0x0f;
    if (x & 1) {
        p8_ram.mem.screen[off] = (p8_ram.mem.screen[off] & 0x0f) | (uint8_t)(color << 4);
    } else {
        p8_ram.mem.screen[off] = (p8_ram.mem.screen[off] & 0xf0) | color;
    }
}

static uint8_t p8_get_pixel(int32_t x, int32_t y) {
    uint8_t b;
    if (x < 0 || y < 0 || x >= 128 || y >= 128) return 0;
    b = p8_ram.mem.screen[(uint32_t)y * 64U + ((uint32_t)x >> 1)];
    return (x & 1) ? (uint8_t)(b >> 4) : (uint8_t)(b & 0x0f);
}

int p386_builtin_noop(P386VMState *vm, P386Value *args,
                      uint8_t nargs, uint8_t want_rets) {
    (void)vm;
    (void)nargs;
    set_nil_results(args, want_rets);
    return 0;
}

int p386_builtin_print(P386VMState *vm, P386Value *args,
                       uint8_t nargs, uint8_t want_rets) {
    /* Rendering/text output will be implemented later; preserve ABI now. */
    return p386_builtin_noop(vm, args, nargs, want_rets);
}

int p386_builtin_cls(P386VMState *vm, P386Value *args,
                     uint8_t nargs, uint8_t want_rets) {
    uint8_t packed;
    uint32_t i;
    uint8_t color = (uint8_t)arg_num(args, nargs, 0, 0) & 0x0f;
    (void)vm;
    packed = (uint8_t)(color | (color << 4));
    for (i = 0; i < sizeof(p8_ram.mem.screen); i++) {
        p8_ram.mem.screen[i] = packed;
    }
    if (nargs > 0) p8_ram.mem.draw.pen_color = color;
    set_nil_results(args, want_rets);
    return 0;
}

int p386_builtin_pset(P386VMState *vm, P386Value *args,
                      uint8_t nargs, uint8_t want_rets) {
    int32_t x = arg_num(args, nargs, 0, 0);
    int32_t y = arg_num(args, nargs, 1, 0);
    uint8_t color = (uint8_t)arg_num(args, nargs, 2, p8_ram.mem.draw.pen_color) & 0x0f;
    (void)vm;
    p8_set_pixel(x, y, color);
    p8_ram.mem.draw.pen_color = color;
    set_nil_results(args, want_rets);
    return 0;
}

int p386_builtin_pget(P386VMState *vm, P386Value *args,
                      uint8_t nargs, uint8_t want_rets) {
    int32_t x = arg_num(args, nargs, 0, 0);
    int32_t y = arg_num(args, nargs, 1, 0);
    (void)vm;
    if (args && want_rets > 0) {
        args[0].value = (int32_t)p8_get_pixel(x, y) << 16;
        args[0].tag = P386_TAG_NUM;
    }
    return (want_rets > 0) ? 1 : 0;
}

int p386_builtin_pairs(P386VMState *vm, P386Value *args,
                       uint8_t nargs, uint8_t want_rets) {
    P386Value state;
    (void)vm;
    /* Lua pairs(t) returns next, t, nil.  The VM's TFORCALL has a fast path
     * for this nil/CFUNC iterator shape and uses p386_table_next directly. */
    state.value = 0;
    state.tag = P386_TAG_NIL;
    if (args && nargs > 0) state = args[0];
    if (!args) return 0;
    if (want_rets == 0 || want_rets > 0) {
        args[0].value = (int32_t)(uintptr_t)p386_builtin_pairs;
        args[0].tag = P386_TAG_CFUNC;
    }
    if (want_rets == 0 || want_rets > 1) {
        args[1] = state;
    }
    if (want_rets == 0 || want_rets > 2) {
        args[2].value = 0;
        args[2].tag = P386_TAG_NIL;
    }
    return (want_rets == 0 || want_rets > 3) ? 3 : want_rets;
}

const P386BuiltinDef p386_builtin_defs[P386_BUILTIN_COUNT] = {
    { P386_BUILTIN_PRINT,    "print",    p386_builtin_print },
    { P386_BUILTIN_CLS,      "cls",      p386_builtin_cls },
    { P386_BUILTIN_PSET,     "pset",     p386_builtin_pset },
    { P386_BUILTIN_PGET,     "pget",     p386_builtin_pget },
    { P386_BUILTIN_LINE,     "line",     p386_builtin_noop },
    { P386_BUILTIN_RECT,     "rect",     p386_builtin_noop },
    { P386_BUILTIN_RECTF,    "rectfill", p386_builtin_noop },
    { P386_BUILTIN_CIRCFILL, "circfill", p386_builtin_noop },
    { P386_BUILTIN_SPR,      "spr",      p386_builtin_noop },
    { P386_BUILTIN_MAP,      "map",      p386_builtin_noop },
    { P386_BUILTIN_BTN,      "btn",      p386_builtin_noop },
    { P386_BUILTIN_BTNP,     "btnp",     p386_builtin_noop },
    { P386_BUILTIN_SFX,      "sfx",      p386_builtin_noop },
    { P386_BUILTIN_MUSIC,    "music",    p386_builtin_noop },
    { P386_BUILTIN_PAIRS,    "pairs",    p386_builtin_pairs },
    { P386_BUILTIN_IPAIRS,   "ipairs",   p386_builtin_pairs }
};

void p386_register_builtins(P386VMState *vm) {
    uint32_t i;
    if (!vm) return;
    for (i = 0; i < (uint32_t)P386_BUILTIN_COUNT; i++) {
        P386BuiltinSlot slot = p386_builtin_defs[i].slot;
        if ((uint32_t)slot >= 256U) continue;
        vm->globals[(uint32_t)slot].value = (int32_t)(uintptr_t)p386_builtin_defs[i].func;
        vm->globals[(uint32_t)slot].tag = P386_TAG_CFUNC;
    }
}

P386CFunc p386_builtin_func(P386BuiltinSlot slot) {
    if ((uint32_t)slot >= (uint32_t)P386_BUILTIN_COUNT) return 0;
    return p386_builtin_defs[(uint32_t)slot].func;
}

const char *p386_builtin_name(P386BuiltinSlot slot) {
    if ((uint32_t)slot >= (uint32_t)P386_BUILTIN_COUNT) return 0;
    return p386_builtin_defs[(uint32_t)slot].name;
}
