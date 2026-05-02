#include <stdint.h>
#include "p386_builtins.h"

static void set_nil_results(P386Value *args, uint8_t want_rets) {
    uint8_t i;
    if (!args) return;
    for (i = 0; i < want_rets; i++) {
        args[i].value = 0;
        args[i].tag = P386_TAG_NIL;
    }
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
    return p386_builtin_noop(vm, args, nargs, want_rets);
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
    { P386_BUILTIN_PSET,     "pset",     p386_builtin_noop },
    { P386_BUILTIN_PGET,     "pget",     p386_builtin_noop },
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
