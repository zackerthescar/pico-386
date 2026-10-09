#ifndef BUILTINS_H
#define BUILTINS_H

/*
 * Stable VM global slots for PICO-8/Lua builtins.
 *
 * Bytecode addresses globals by a 16-bit slot index (Bx, up to
 * P386_GLOBAL_SLOTS). Keep these
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

    /* ── higher-order (implemented in the compiler's Lua prelude) ──
     * These slots have NO CFUNC registered (func == NULL in
     * p386_builtin_defs). The rust compiler prepends a Lua prelude to every
     * cart which defines them via SETGLOBAL into these slots. IPAIRS above
     * is also prelude-implemented (its CFUNC registration was removed). */
    P386_BUILTIN_ALL,
    P386_BUILTIN_FOREACH,

    /* ── graphics: sprites, map, draw state ── */
    P386_BUILTIN_SSPR,
    P386_BUILTIN_MGET,
    P386_BUILTIN_MSET,
    P386_BUILTIN_FGET,
    P386_BUILTIN_FSET,
    P386_BUILTIN_SGET,
    P386_BUILTIN_SSET,
    P386_BUILTIN_CAMERA,
    P386_BUILTIN_CLIP,
    P386_BUILTIN_PAL,
    P386_BUILTIN_PALT,

    /* ── shapes, pen state ── */
    P386_BUILTIN_CIRC,
    P386_BUILTIN_OVAL,
    P386_BUILTIN_OVALFILL,
    P386_BUILTIN_FILLP,
    P386_BUILTIN_COLOR,
    P386_BUILTIN_CURSOR,

    /* ── system ── */
    P386_BUILTIN_TIME,
    P386_BUILTIN_STAT,
    P386_BUILTIN_FLIP,
    P386_BUILTIN_PRINTH,

    /* ── values / tables ── */
    P386_BUILTIN_TYPE,
    P386_BUILTIN_UNPACK,
    P386_BUILTIN_PACK,
    P386_BUILTIN_SELECT,
    P386_BUILTIN_SPLIT,
    P386_BUILTIN_RAWGET,
    P386_BUILTIN_RAWSET,
    P386_BUILTIN_RAWEQUAL,
    P386_BUILTIN_RAWLEN,

    /* ── memory blocks / cart ROM ── */
    P386_BUILTIN_MEMCPY,
    P386_BUILTIN_MEMSET,
    P386_BUILTIN_RELOAD,
    P386_BUILTIN_CSTORE,

    /* ── metatables ── */
    P386_BUILTIN_SETMETATABLE,
    P386_BUILTIN_GETMETATABLE,

    /* ── coroutines ── */
    P386_BUILTIN_COCREATE,
    P386_BUILTIN_CORESUME,
    P386_BUILTIN_COSTATUS,
    P386_BUILTIN_YIELD,

    /* ── system ── */
    P386_BUILTIN_T,             /* time() under a second name */
    P386_BUILTIN_MENUITEM,      /* no pause menu yet: ignored */
    P386_BUILTIN_EXTCMD,        /* host commands: ignored */
    P386_BUILTIN_CARTDATA,
    P386_BUILTIN_DGET,
    P386_BUILTIN_DSET,

    P386_BUILTIN_COUNT,

    /* Stable user-global slots the host runtime calls directly. */
    P386_GLOBAL_INIT = P386_BUILTIN_COUNT,
    P386_GLOBAL_UPDATE,
    P386_GLOBAL_UPDATE60,
    P386_GLOBAL_DRAW,

    P386_USER_GLOBAL_BASE
} P386BuiltinSlot;

#endif /* BUILTINS_H */
