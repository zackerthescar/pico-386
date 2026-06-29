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

    /* ── math ── */
    P386_BUILTIN_ABS,
    P386_BUILTIN_FLR,
    P386_BUILTIN_CEIL,
    P386_BUILTIN_SGN,
    P386_BUILTIN_MIN,
    P386_BUILTIN_MAX,
    P386_BUILTIN_MID,
    P386_BUILTIN_SQRT,
    P386_BUILTIN_SIN,
    P386_BUILTIN_COS,
    P386_BUILTIN_ATAN2,
    P386_BUILTIN_RND,
    P386_BUILTIN_SRAND,

    /* ── bitwise (function forms; operate on raw 16.16 bits) ── */
    P386_BUILTIN_BAND,
    P386_BUILTIN_BOR,
    P386_BUILTIN_BXOR,
    P386_BUILTIN_BNOT,
    P386_BUILTIN_SHL,
    P386_BUILTIN_SHR,
    P386_BUILTIN_LSHR,
    P386_BUILTIN_ROTL,
    P386_BUILTIN_ROTR,

    /* ── memory (function forms) ── */
    P386_BUILTIN_PEEK,
    P386_BUILTIN_POKE,
    P386_BUILTIN_PEEK2,
    P386_BUILTIN_POKE2,
    P386_BUILTIN_PEEK4,
    P386_BUILTIN_POKE4,

    /* ── table ── */
    P386_BUILTIN_ADD,
    P386_BUILTIN_DEL,
    P386_BUILTIN_DELI,
    P386_BUILTIN_COUNTF,

    /* ── string / conversion ── */
    P386_BUILTIN_TOSTR,
    P386_BUILTIN_TONUM,
    P386_BUILTIN_CHR,
    P386_BUILTIN_ORD,
    P386_BUILTIN_SUB,

    P386_BUILTIN_COUNT,

    /* Stable user-global slots the host runtime calls directly. */
    P386_GLOBAL_INIT = P386_BUILTIN_COUNT,
    P386_GLOBAL_UPDATE,
    P386_GLOBAL_UPDATE60,
    P386_GLOBAL_DRAW,

    P386_USER_GLOBAL_BASE
} P386BuiltinSlot;

#endif /* BUILTINS_H */
