#ifndef BUILTINS_H
#define BUILTINS_H

/*
 * Stable VM global slots for PICO-8/Lua builtins.
 *
 * Bytecode currently addresses globals by an 8-bit slot index. Keep these
 * values stable and in sync with the compiler once it starts emitting builtin
 * global references by name.
 */
typedef enum P386BuiltinSlot {
    P386_BUILTIN_PRINT = 0,
    P386_BUILTIN_CLS,
    P386_BUILTIN_PSET,
    P386_BUILTIN_PGET,
    P386_BUILTIN_LINE,
    P386_BUILTIN_RECT,
    P386_BUILTIN_RECTF,
    P386_BUILTIN_CIRCFILL,
    P386_BUILTIN_SPR,
    P386_BUILTIN_MAP,
    P386_BUILTIN_BTN,
    P386_BUILTIN_BTNP,
    P386_BUILTIN_SFX,
    P386_BUILTIN_MUSIC,
    P386_BUILTIN_PAIRS,
    P386_BUILTIN_IPAIRS,
    P386_BUILTIN_COUNT,

    /* Stable user-global slots the host runtime calls directly. */
    P386_GLOBAL_INIT = P386_BUILTIN_COUNT,
    P386_GLOBAL_UPDATE,
    P386_GLOBAL_UPDATE60,
    P386_GLOBAL_DRAW,

    P386_USER_GLOBAL_BASE
} P386BuiltinSlot;

#endif /* BUILTINS_H */
