#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "p386_builtins.h"
#include "p386_obj.h"
#include "mem.h"
#include "input.h"
#include "gfx.h"
#include "serial.h"

#include "p386_trig.inc"

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

int p386_builtin_noop(P386VMState *vm, P386Value *args,
                      uint8_t nargs, uint8_t want_rets) {
    (void)vm;
    (void)nargs;
    set_nil_results(args, want_rets);
    return 0;
}

int p386_builtin_cls(P386VMState *vm, P386Value *args,
                     uint8_t nargs, uint8_t want_rets) {
    uint8_t color = (uint8_t)arg_num(args, nargs, 0, 0) & 0x0f;
    (void)vm;
    gfx_cls(color);
    if (nargs > 0) p8_ram.mem.draw.pen_color = color;
    set_nil_results(args, want_rets);
    return 0;
}

int p386_builtin_pset(P386VMState *vm, P386Value *args,
                      uint8_t nargs, uint8_t want_rets) {
    int32_t x = arg_num(args, nargs, 0, 0);
    int32_t y = arg_num(args, nargs, 1, 0);
    uint8_t color = (uint8_t)arg_num(args, nargs, 2, p8_ram.mem.draw.pen_color);
    (void)vm;
    gfx_pset(x, y, color);
    p8_ram.mem.draw.pen_color = color;
    set_nil_results(args, want_rets);
    return 0;
}

int p386_builtin_pget(P386VMState *vm, P386Value *args,
                      uint8_t nargs, uint8_t want_rets) {
    int32_t x = arg_num(args, nargs, 0, 0);
    int32_t y = arg_num(args, nargs, 1, 0);
    (void)vm;
    if (args) {
        args[0].value = (int32_t)gfx_pget(x, y) << 16;
        args[0].tag = P386_TAG_NUM;
    }
    return args ? 1 : 0;
}

/* btn()/btnp() share one shape: no button -> bitfield of players 0 and 1
 * (P1 in bits 8-15); btn(i, [p]) -> boolean for button i of player p. */
static int button_query(P386Value *args, uint8_t nargs, uint8_t want_rets,
                        const uint8_t *bits) {
    int32_t i, p;
    (void)want_rets;
    if (!args) return 0;
    if (nargs == 0 || args[0].tag == P386_TAG_NIL) {
        args[0].value = (int32_t)(bits[0] | ((uint32_t)bits[1] << 8)) << 16;
        args[0].tag = P386_TAG_NUM;
        return 1;
    }
    i = arg_num(args, nargs, 0, -1);
    p = arg_num(args, nargs, 1, 0);
    args[0].value = (i >= 0 && i < P8_BUTTONS && p >= 0 && p < P8_PLAYERS)
                    ? (bits[p] >> i) & 1 : 0;
    args[0].tag = P386_TAG_BOOL;
    return 1;
}

int p386_builtin_btn(P386VMState *vm, P386Value *args,
                     uint8_t nargs, uint8_t want_rets) {
    (void)vm;
    return button_query(args, nargs, want_rets, p8_ram.mem.hw.btn);
}

int p386_builtin_btnp(P386VMState *vm, P386Value *args,
                      uint8_t nargs, uint8_t want_rets) {
    (void)vm;
    return button_query(args, nargs, want_rets, p8_btnp_bits);
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

/* ===================================================================== *
 * Trivial-tier builtins: pure value-in / value-out, no rendering, no VM  *
 * re-entrancy. All numbers are 16.16 fixed point; no FPU is used.        *
 * ===================================================================== */

/* Write a single NUM result (16.16). Returns the count.
 *
 * want_rets 0 means either "no results" (statement call) or "all results"
 * (last argument of another call); CALL copies as many values as we return,
 * so a builtin must always write and return its results. */
static int ret_num(P386Value *a, uint8_t w, int32_t fp) {
    (void)w;
    if (!a) return 0;
    a[0].value = fp;
    a[0].tag = P386_TAG_NUM;
    return 1;
}

/* No results (poke, srand, ...). CALL pads any wanted values with nil. */
static int ret_none(P386Value *a, uint8_t w) {
    set_nil_results(a, w);
    return 0;
}

/* A single nil result. */
static int ret_nil(P386Value *a, uint8_t w) {
    set_nil_results(a, w);
    if (!a) return 0;
    if (w == 0) { a[0].value = 0; a[0].tag = P386_TAG_NIL; }
    return 1;
}

/* Raw 16.16 bits of arg idx, or a default if missing/non-number. */
static int32_t arg_fp(P386Value *a, uint8_t n, uint8_t idx, int32_t def) {
    if (!a || idx >= n || a[idx].tag != P386_TAG_NUM) return def;
    return a[idx].value;
}

static P386Table *arg_table(P386Value *a, uint8_t n, uint8_t idx) {
    if (!a || idx >= n || a[idx].tag != P386_TAG_TAB) return 0;
    return (P386Table *)(uintptr_t)a[idx].value;
}

/* ---- math ---------------------------------------------------------- */

int p386_builtin_abs(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    int32_t v = arg_fp(a, n, 0, 0);
    (void)vm;
    /* PICO-8: abs(-32768) saturates at 32767.99998 (0x7fffffff). */
    if (v == (int32_t)0x80000000) v = 0x7fffffff;
    else if (v < 0) v = -v;
    return ret_num(a, w, v);
}

int p386_builtin_flr(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    int32_t v = arg_fp(a, n, 0, 0);
    (void)vm;
    return ret_num(a, w, (int32_t)(v & (int32_t)0xffff0000u));
}

int p386_builtin_ceil(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    int32_t v = arg_fp(a, n, 0, 0);
    (void)vm;
    if (v & 0xffff) v = (int32_t)((v & (int32_t)0xffff0000u) + 0x10000);
    return ret_num(a, w, v);
}

int p386_builtin_sgn(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    int32_t v = arg_fp(a, n, 0, 0);
    (void)vm;
    /* PICO-8: sgn(0) == 1. */
    return ret_num(a, w, (v < 0) ? -0x10000 : 0x10000);
}

int p386_builtin_min(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    int32_t x = arg_fp(a, n, 0, 0);
    int32_t y = arg_fp(a, n, 1, 0);
    (void)vm;
    return ret_num(a, w, (x < y) ? x : y);
}

int p386_builtin_max(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    int32_t x = arg_fp(a, n, 0, 0);
    int32_t y = arg_fp(a, n, 1, 0);
    (void)vm;
    return ret_num(a, w, (x > y) ? x : y);
}

int p386_builtin_mid(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    int32_t x = arg_fp(a, n, 0, 0);
    int32_t y = arg_fp(a, n, 1, 0);
    int32_t z = arg_fp(a, n, 2, 0);
    int32_t lo, hi;
    (void)vm;
    /* median of three */
    lo = (x < y) ? x : y; hi = (x < y) ? y : x;
    if (z < lo) return ret_num(a, w, lo);
    if (z > hi) return ret_num(a, w, hi);
    return ret_num(a, w, z);
}

/* isqrt of a 32-bit unsigned, returns floor(sqrt). */
static uint32_t isqrt32(uint32_t v) {
    uint32_t res = 0;
    uint32_t bit = 1UL << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= res + bit) { v -= res + bit; res = (res >> 1) + bit; }
        else res >>= 1;
        bit >>= 2;
    }
    return res;
}

int p386_builtin_sqrt(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    int32_t v = arg_fp(a, n, 0, 0);
    uint32_t r;
    (void)vm;
    if (v <= 0) return ret_num(a, w, 0);   /* PICO-8: sqrt of <=0 -> 0 */
    /* sqrt(v/65536) * 65536 = sqrt(v * 65536). Compute over 64 bits. */
    {
        unsigned long long vv = (unsigned long long)(uint32_t)v << 16;
        /* 64-bit integer sqrt via Newton, seeded by 32-bit isqrt. */
        unsigned long long x = isqrt32((uint32_t)(vv >> 16)) << 8;
        if (x == 0) x = 1;
        /* a few Newton iterations for full 64-bit precision */
        x = (x + vv / x) >> 1;
        x = (x + vv / x) >> 1;
        x = (x + vv / x) >> 1;
        /* correct any off-by-one */
        while (x * x > vv) x--;
        while ((x + 1) * (x + 1) <= vv) x++;
        r = (uint32_t)x;
    }
    return ret_num(a, w, (int32_t)r);
}

/* Look up sin for a fixed-point turn fraction. PICO-8 sin is NEGATED:
 * sin(x) returns -sin_math(2*pi*x). cos(x) = sin_math(2*pi*x + pi/2). */
static int32_t sin_turns_fp(int32_t turns_fp) {
    /* index into the table by the fractional turn. The table holds
     * math sin(2*pi*i/N); PICO-8 negates the result. */
    uint32_t idx = ((uint32_t)turns_fp >> P386_TRIG_SHIFT) & (P386_TRIG_N - 1);
    return -p386_sin_table[idx];
}

int p386_builtin_sin(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    (void)vm;
    return ret_num(a, w, sin_turns_fp(arg_fp(a, n, 0, 0)));
}

int p386_builtin_cos(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    int32_t t = arg_fp(a, n, 0, 0);
    (void)vm;
    /* cos(x) = sin(x + 0.25 turn), but PICO-8 sin is negated and the table is
     * math-positive, so cos = +table[(idx + N/4)]. */
    {
        uint32_t idx = (((uint32_t)t >> P386_TRIG_SHIFT) + (P386_TRIG_N / 4))
                       & (P386_TRIG_N - 1);
        return ret_num(a, w, p386_sin_table[idx]);
    }
}

int p386_builtin_atan2(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    int32_t dx = arg_fp(a, n, 0, 0);
    int32_t dy = arg_fp(a, n, 1, 0);
    uint32_t best = 0, i;
    int32_t bestd = 0x7fffffff;
    (void)vm;
    /* PICO-8 atan2 returns a turn fraction [0,1) measured CLOCKWISE from +x,
     * i.e. angle of (dx, dy) where +y is down. Brute-force nearest table
     * angle: minimize |dx*sin_i + dy*cos_i form|. We search the table for the
     * angle whose unit vector best matches (dx,dy). Cheap and exact enough. */
    if (dx == 0 && dy == 0) return ret_num(a, w, 0x4000); /* PICO-8: 0.25 */
    for (i = 0; i < P386_TRIG_N; i++) {
        /* candidate angle i: cos = table[(i+N/4)], pico_sin = -table[i].
         * direction vector PICO uses: (cos(t), sin(t)) with sin negated and
         * y-down => (table[(i+N/4)&M], table[i]). Compare direction. */
        int32_t cx = p386_sin_table[(i + P386_TRIG_N / 4) & (P386_TRIG_N - 1)];
        int32_t cy = p386_sin_table[i];
        /* cross-product magnitude (want parallel & same dir): minimise
         * |dx*cy - dy*cx| while dot>0. Use 64-bit to avoid overflow. */
        long long dot = (long long)dx * cx + (long long)dy * cy;
        long long crs = (long long)dx * cy - (long long)dy * cx;
        if (dot > 0) {
            int32_t d = (int32_t)(crs < 0 ? -crs : crs) >> 8;
            if (d < bestd) { bestd = d; best = i; }
        }
    }
    return ret_num(a, w, (int32_t)((best << P386_TRIG_SHIFT) & 0xffff));
}

/* PICO-8 PRNG: simple LCG-ish two-state used widely; we approximate with a
 * deterministic xorshift seeded by srand. Not bit-identical to Lexaloffle's
 * RNG, but stable and well-distributed. */
static uint32_t rng_state = 0x12345678u;

static uint32_t rng_next(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    rng_state = x ? x : 0x1u;
    return rng_state;
}

int p386_builtin_rnd(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    uint32_t r = rng_next();
    (void)vm;
    if (n == 0 || (a && a[0].tag == P386_TAG_NIL)) {
        /* rnd() -> [0,1): take 16 fractional bits. */
        return ret_num(a, w, (int32_t)(r & 0xffff));
    }
    {
        int32_t lim = arg_fp(a, n, 0, 0x10000);
        /* rnd(x) -> [0,x): multiply fractional [0,1) by x in fixed point. */
        long long frac = (long long)(r & 0xffff);  /* 0..65535 = [0,1) */
        long long prod = frac * (long long)lim;     /* 16.16 * 0.16 */
        return ret_num(a, w, (int32_t)(prod >> 16));
    }
}

int p386_builtin_srand(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    uint32_t s = (uint32_t)arg_fp(a, n, 0, 0);
    (void)vm;
    rng_state = s ? s : 0x1u;
    return ret_none(a, w);
}

/* ---- bitwise (function forms; raw 16.16 bit ops, true PICO-8) ------- */

int p386_builtin_band(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    (void)vm; return ret_num(a, w, arg_fp(a, n, 0, 0) & arg_fp(a, n, 1, 0));
}
int p386_builtin_bor(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    (void)vm; return ret_num(a, w, arg_fp(a, n, 0, 0) | arg_fp(a, n, 1, 0));
}
int p386_builtin_bxor(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    (void)vm; return ret_num(a, w, arg_fp(a, n, 0, 0) ^ arg_fp(a, n, 1, 0));
}
int p386_builtin_bnot(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    (void)vm; return ret_num(a, w, ~arg_fp(a, n, 0, 0));
}
int p386_builtin_shl(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    int32_t v = arg_fp(a, n, 0, 0);
    int32_t s = arg_fp(a, n, 1, 0) >> 16;
    (void)vm;
    if (s < 0) s = 0; if (s > 31) return ret_num(a, w, 0);
    return ret_num(a, w, (int32_t)((uint32_t)v << s));
}
int p386_builtin_shr(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    int32_t v = arg_fp(a, n, 0, 0);
    int32_t s = arg_fp(a, n, 1, 0) >> 16;
    (void)vm;
    if (s < 0) s = 0; if (s > 31) return ret_num(a, w, (v < 0) ? -1 : 0);
    return ret_num(a, w, v >> s);   /* arithmetic (sign-extending) */
}
int p386_builtin_lshr(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    int32_t v = arg_fp(a, n, 0, 0);
    int32_t s = arg_fp(a, n, 1, 0) >> 16;
    (void)vm;
    if (s < 0) s = 0; if (s > 31) return ret_num(a, w, 0);
    return ret_num(a, w, (int32_t)((uint32_t)v >> s));
}
int p386_builtin_rotl(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    uint32_t v = (uint32_t)arg_fp(a, n, 0, 0);
    int32_t s = (arg_fp(a, n, 1, 0) >> 16) & 31;
    (void)vm;
    return ret_num(a, w, (int32_t)((v << s) | (v >> ((32 - s) & 31))));
}
int p386_builtin_rotr(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    uint32_t v = (uint32_t)arg_fp(a, n, 0, 0);
    int32_t s = (arg_fp(a, n, 1, 0) >> 16) & 31;
    (void)vm;
    return ret_num(a, w, (int32_t)((v >> s) | (v << ((32 - s) & 31))));
}

/* ---- memory (function forms) --------------------------------------- */

static uint32_t mem_addr(P386Value *a, uint8_t n, uint8_t idx) {
    return ((uint32_t)(arg_fp(a, n, idx, 0) >> 16)) & 0xffff;
}

int p386_builtin_peek(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    uint32_t addr = mem_addr(a, n, 0);
    (void)vm;
    return ret_num(a, w, (int32_t)((uint32_t)p8_ram.raw[addr] << 16));
}
int p386_builtin_poke(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    uint32_t addr = mem_addr(a, n, 0);
    (void)vm;
    p8_ram.raw[addr] = (uint8_t)(arg_fp(a, n, 1, 0) >> 16);
    return ret_none(a, w);
}
int p386_builtin_peek2(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    uint32_t addr = mem_addr(a, n, 0);
    uint32_t lo = p8_ram.raw[addr];
    uint32_t hi = p8_ram.raw[(addr + 1) & 0xffff];
    int32_t v = (int32_t)(int16_t)(uint16_t)(lo | (hi << 8));  /* signed 16 */
    (void)vm;
    return ret_num(a, w, (int32_t)((uint32_t)v << 16));
}
int p386_builtin_poke2(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    uint32_t addr = mem_addr(a, n, 0);
    uint32_t v = (uint32_t)(arg_fp(a, n, 1, 0) >> 16);
    (void)vm;
    p8_ram.raw[addr] = (uint8_t)v;
    p8_ram.raw[(addr + 1) & 0xffff] = (uint8_t)(v >> 8);
    return ret_none(a, w);
}
int p386_builtin_peek4(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    uint32_t addr = mem_addr(a, n, 0);
    uint32_t v = 0; int i;
    (void)vm;
    /* peek4 reads a 32-bit value AS a 16.16 fixed-point number directly. */
    for (i = 0; i < 4; i++) v |= (uint32_t)p8_ram.raw[(addr + i) & 0xffff] << (i * 8);
    return ret_num(a, w, (int32_t)v);
}
int p386_builtin_poke4(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    uint32_t addr = mem_addr(a, n, 0);
    uint32_t v = (uint32_t)arg_fp(a, n, 1, 0); int i;
    (void)vm;
    for (i = 0; i < 4; i++) p8_ram.raw[(addr + i) & 0xffff] = (uint8_t)(v >> (i * 8));
    return ret_none(a, w);
}

/* ---- table --------------------------------------------------------- */

int p386_builtin_add(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    P386Table *t = arg_table(a, n, 0);
    P386Value key, val;
    (void)vm; (void)w;
    if (!t || n < 2) { return ret_nil(a, w); }
    val = a[1];
    if (n >= 3 && a[2].tag == P386_TAG_NUM) {
        key = a[2];  /* add(t, v, i): insert at index i (no shift for v1) */
    } else {
        key.value = (int32_t)((p386_table_len(t) + 1) << 16);
        key.tag = P386_TAG_NUM;
    }
    p386_table_set(t, &key, &val);
    /* PICO-8 add returns the value added. */
    if (a) { a[0] = val; return 1; }
    return 0;
}

/* del(t, v): remove first element equal to v, shifting down. Returns v. */
int p386_builtin_del(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    P386Table *t = arg_table(a, n, 0);
    uint32_t len, i, found = 0;
    P386Value target;
    (void)vm;
    if (!t || n < 2) return ret_nil(a, w);
    target = a[1];
    len = p386_table_len(t);
    for (i = 1; i <= len; i++) {
        P386Value k, v;
        k.value = (int32_t)(i << 16); k.tag = P386_TAG_NUM;
        p386_table_get(t, &k, &v);
        if (!found) {
            if (v.tag == target.tag && v.value == target.value) found = i;
        }
        if (found && i > found) {
            /* shift v down into slot i-1 */
            P386Value pk; pk.value = (int32_t)((i - 1) << 16); pk.tag = P386_TAG_NUM;
            p386_table_set(t, &pk, &v);
        }
    }
    if (found) {
        P386Value lk, nil; lk.value = (int32_t)(len << 16); lk.tag = P386_TAG_NUM;
        nil.value = 0; nil.tag = P386_TAG_NIL;
        p386_table_set(t, &lk, &nil);
        if (a) { a[0] = target; return 1; }
        return 0;
    }
    return ret_nil(a, w);
}

/* deli(t, i): remove element at index i (default last), shifting down. */
int p386_builtin_deli(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    P386Table *t = arg_table(a, n, 0);
    uint32_t len, idx, i;
    P386Value removed;
    (void)vm;
    if (!t) return ret_nil(a, w);
    len = p386_table_len(t);
    if (len == 0) return ret_nil(a, w);
    idx = (n >= 2 && a[1].tag == P386_TAG_NUM) ? (uint32_t)(a[1].value >> 16) : len;
    if (idx < 1 || idx > len) return ret_nil(a, w);
    {
        P386Value k; k.value = (int32_t)(idx << 16); k.tag = P386_TAG_NUM;
        p386_table_get(t, &k, &removed);
    }
    for (i = idx; i < len; i++) {
        P386Value src, dk, sk;
        sk.value = (int32_t)((i + 1) << 16); sk.tag = P386_TAG_NUM;
        p386_table_get(t, &sk, &src);
        dk.value = (int32_t)(i << 16); dk.tag = P386_TAG_NUM;
        p386_table_set(t, &dk, &src);
    }
    {
        P386Value lk, nil; lk.value = (int32_t)(len << 16); lk.tag = P386_TAG_NUM;
        nil.value = 0; nil.tag = P386_TAG_NIL;
        p386_table_set(t, &lk, &nil);
    }
    if (a) { a[0] = removed; return 1; }
    return 0;
}

int p386_builtin_count(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    P386Table *t = arg_table(a, n, 0);
    (void)vm;
    if (!t) return ret_num(a, w, 0);
    if (n >= 2 && a[1].tag != P386_TAG_NIL) {
        /* count(t, v): number of elements equal to v in array part. */
        uint32_t len = p386_table_len(t), i, c = 0;
        for (i = 1; i <= len; i++) {
            P386Value k, v; k.value = (int32_t)(i << 16); k.tag = P386_TAG_NUM;
            p386_table_get(t, &k, &v);
            if (v.tag == a[1].tag && v.value == a[1].value) c++;
        }
        return ret_num(a, w, (int32_t)(c << 16));
    }
    return ret_num(a, w, (int32_t)(p386_table_len(t) << 16));
}

/* ---- string / conversion ------------------------------------------- */

int p386_builtin_tostr(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    P386String *s = 0;
    (void)vm;
    if (a) {
        if (n == 0 || a[0].tag == P386_TAG_NIL) {
            s = p386_string_intern("", 0);
        } else if (a[0].tag == P386_TAG_STR) {
            a[0].tag = P386_TAG_STR;  /* already a string */
            return 1;
        } else if (a[0].tag == P386_TAG_NUM) {
            s = p386_num_to_string(a[0].value);
        } else if (a[0].tag == P386_TAG_BOOL) {
            s = p386_string_intern(a[0].value ? "true" : "false",
                                   a[0].value ? 4 : 5);
        } else {
            s = p386_string_intern("[obj]", 5);
        }
        if (!s) return ret_nil(a, w);
        a[0].value = (int32_t)(uintptr_t)s;
        a[0].tag = P386_TAG_STR;
        return 1;
    }
    return 0;
}

int p386_builtin_tonum(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    (void)vm;
    if (!a) return 0;
    if (n == 0) return ret_nil(a, w);
    if (a[0].tag == P386_TAG_NUM) return 1;  /* already numeric */
    if (a[0].tag == P386_TAG_STR) {
        P386String *s = (P386String *)(uintptr_t)a[0].value;
        const char *p = s->data;
        int neg = 0; long whole = 0; int any = 0;
        long frac = 0, fdiv = 1;
        while (*p == ' ') p++;
        if (*p == '-') { neg = 1; p++; } else if (*p == '+') p++;
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            /* hex integer */
            unsigned long hv = 0; p += 2;
            while (*p) {
                int d;
                if (*p >= '0' && *p <= '9') d = *p - '0';
                else if (*p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
                else if (*p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
                else break;
                hv = hv * 16 + d; any = 1; p++;
            }
            if (!any) return ret_nil(a, w);
            return ret_num(a, w, (int32_t)((uint32_t)hv << 16) * (neg ? -1 : 1));
        }
        while (*p >= '0' && *p <= '9') { whole = whole * 10 + (*p - '0'); any = 1; p++; }
        if (*p == '.') { p++; while (*p >= '0' && *p <= '9' && fdiv < 100000) {
            frac = frac * 10 + (*p - '0'); fdiv *= 10; any = 1; p++; } }
        if (!any) return ret_nil(a, w);
        {
            int32_t fp = (int32_t)(whole << 16);
            if (fdiv > 1) fp += (int32_t)(((long long)frac << 16) / fdiv);
            if (neg) fp = -fp;
            return ret_num(a, w, fp);
        }
    }
    return ret_nil(a, w);
}

int p386_builtin_chr(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    char buf[8]; uint8_t i, cnt = 0;
    P386String *s;
    (void)vm;
    if (!a) return 0;
    for (i = 0; i < n && cnt < (uint8_t)sizeof(buf); i++) {
        if (a[i].tag != P386_TAG_NUM) break;
        buf[cnt++] = (char)((a[i].value >> 16) & 0xff);
    }
    s = p386_string_intern(buf, cnt);
    if (!s) return ret_nil(a, w);
    a[0].value = (int32_t)(uintptr_t)s;
    a[0].tag = P386_TAG_STR;
    return 1;
}

int p386_builtin_ord(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    P386String *s;
    uint32_t idx = 1;
    (void)vm;
    if (!a || n == 0 || a[0].tag != P386_TAG_STR) return ret_nil(a, w);
    s = (P386String *)(uintptr_t)a[0].value;
    if (n >= 2 && a[1].tag == P386_TAG_NUM) idx = (uint32_t)(a[1].value >> 16);
    if (idx < 1 || idx > s->len) return ret_nil(a, w);
    return ret_num(a, w, (int32_t)((uint32_t)(uint8_t)s->data[idx - 1] << 16));
}

int p386_builtin_sub(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    P386String *s, *r;
    int32_t len, i0, i1;
    (void)vm;
    if (!a || n == 0 || a[0].tag != P386_TAG_STR) return ret_nil(a, w);
    s = (P386String *)(uintptr_t)a[0].value;
    len = (int32_t)s->len;
    i0 = (n >= 2 && a[1].tag == P386_TAG_NUM) ? (a[1].value >> 16) : 1;
    i1 = (n >= 3 && a[2].tag == P386_TAG_NUM) ? (a[2].value >> 16) : len;
    if (i0 < 0) i0 = len + i0 + 1;
    if (i1 < 0) i1 = len + i1 + 1;
    if (i0 < 1) i0 = 1;
    if (i1 > len) i1 = len;
    if (i1 < i0) { r = p386_string_intern("", 0); }
    else { r = p386_string_intern(s->data + (i0 - 1), (uint32_t)(i1 - i0 + 1)); }
    if (!r) return ret_nil(a, w);
    a[0].value = (int32_t)(uintptr_t)r;
    a[0].tag = P386_TAG_STR;
    return 1;
}

/* ===================================================================== *
 * Graphics: sprites, map, draw state. Drawing lives in gfx.c; these     *
 * parse arguments. Coordinates are floored (the integer part of 16.16). *
 * ===================================================================== */

static int arg_truthy(P386Value *a, uint8_t n, uint8_t idx) {
    if (!a || idx >= n) return 0;
    if (a[idx].tag == P386_TAG_NIL) return 0;
    if (a[idx].tag == P386_TAG_BOOL) return a[idx].value != 0;
    return 1;
}

static int arg_present(P386Value *a, uint8_t n, uint8_t idx) {
    return a && idx < n && a[idx].tag != P386_TAG_NIL;
}

static void set_num(P386Value *v, int32_t i) {
    v->value = P386_FP_INT(i);
    v->tag = P386_TAG_NUM;
}

static void set_bool(P386Value *v, int b) {
    v->value = b ? 1 : 0;
    v->tag = P386_TAG_BOOL;
}

/* Sprite counts (w, h) to pixels: flr(w * 8) on the 16.16 value. */
static int32_t sprite_px(int32_t fp) {
    return fp >> 13;
}

int p386_builtin_spr(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    (void)vm; (void)w;
    gfx_spr(arg_num(a, n, 0, 0), arg_num(a, n, 1, 0), arg_num(a, n, 2, 0),
            sprite_px(arg_fp(a, n, 3, P386_FP_INT(1))),
            sprite_px(arg_fp(a, n, 4, P386_FP_INT(1))),
            arg_truthy(a, n, 5), arg_truthy(a, n, 6));
    return 0;
}

int p386_builtin_sspr(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    int32_t sw = arg_num(a, n, 2, 0);
    int32_t sh = arg_num(a, n, 3, 0);
    (void)vm; (void)w;
    gfx_sspr(arg_num(a, n, 0, 0), arg_num(a, n, 1, 0), sw, sh,
             arg_num(a, n, 4, 0), arg_num(a, n, 5, 0),
             arg_num(a, n, 6, sw), arg_num(a, n, 7, sh),
             arg_truthy(a, n, 8), arg_truthy(a, n, 9));
    return 0;
}

int p386_builtin_map(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    (void)vm; (void)w;
    gfx_map(arg_num(a, n, 0, 0), arg_num(a, n, 1, 0),
            arg_num(a, n, 2, 0), arg_num(a, n, 3, 0),
            arg_num(a, n, 4, 128), arg_num(a, n, 5, 32),
            (uint8_t)arg_num(a, n, 6, 0));
    return 0;
}

int p386_builtin_mget(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    (void)vm;
    return ret_num(a, w, P386_FP_INT(gfx_mget(arg_num(a, n, 0, 0), arg_num(a, n, 1, 0))));
}

int p386_builtin_mset(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    (void)vm; (void)w;
    gfx_mset(arg_num(a, n, 0, 0), arg_num(a, n, 1, 0), (uint8_t)arg_num(a, n, 2, 0));
    return 0;
}

/* fget(n) -> flag byte; fget(n, f) -> boolean for bit f. */
int p386_builtin_fget(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    uint8_t flags = p8_ram.mem.gfx_flags[(uint8_t)arg_num(a, n, 0, 0)];
    (void)vm; (void)w;
    if (!a) return 0;
    if (arg_present(a, n, 1)) {
        set_bool(&a[0], (flags >> (arg_num(a, n, 1, 0) & 7)) & 1);
    } else {
        set_num(&a[0], flags);
    }
    return 1;
}

/* fset(n, v) sets the flag byte; fset(n, f, v) sets or clears bit f. */
int p386_builtin_fset(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    uint8_t *flags = &p8_ram.mem.gfx_flags[(uint8_t)arg_num(a, n, 0, 0)];
    (void)vm; (void)w;
    if (n > 2) {
        uint8_t bit = (uint8_t)(1 << (arg_num(a, n, 1, 0) & 7));
        if (arg_truthy(a, n, 2)) *flags |= bit;
        else *flags &= (uint8_t)~bit;
    } else {
        *flags = (uint8_t)arg_num(a, n, 1, 0);
    }
    return 0;
}

int p386_builtin_sget(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    (void)vm;
    return ret_num(a, w, P386_FP_INT(gfx_sget(arg_num(a, n, 0, 0), arg_num(a, n, 1, 0))));
}

int p386_builtin_sset(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    (void)vm; (void)w;
    gfx_sset(arg_num(a, n, 0, 0), arg_num(a, n, 1, 0),
             (uint8_t)arg_num(a, n, 2, p8_ram.mem.draw.pen_color));
    return 0;
}

/* camera([x, y]) -> previous x, y */
int p386_builtin_camera(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    P8DrawState *ds = &p8_ram.mem.draw;
    int32_t px = ds->camera_x, py = ds->camera_y;
    (void)vm; (void)w;
    ds->camera_x = (int16_t)arg_num(a, n, 0, 0);
    ds->camera_y = (int16_t)arg_num(a, n, 1, 0);
    if (!a) return 0;
    set_num(&a[0], px);
    set_num(&a[1], py);
    return 2;
}

static int32_t clamp_screen(int32_t v) {
    return v < 0 ? 0 : (v > 128 ? 128 : v);
}

/* clip([x, y, w, h, [clip_previous]]) -> previous x0, y0, x1, y1.
 * With clip_previous the new rect is intersected with the old one. */
int p386_builtin_clip(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    P8DrawState *ds = &p8_ram.mem.draw;
    uint8_t old[4];
    (void)vm; (void)w;
    old[0] = ds->clip_x0; old[1] = ds->clip_y0;
    old[2] = ds->clip_x1; old[3] = ds->clip_y1;

    if (!arg_present(a, n, 3)) {
        ds->clip_x0 = 0; ds->clip_y0 = 0;
        ds->clip_x1 = 128; ds->clip_y1 = 128;
    } else {
        int32_t x = arg_num(a, n, 0, 0), y = arg_num(a, n, 1, 0);
        int32_t cw = arg_num(a, n, 2, 0), ch = arg_num(a, n, 3, 0);
        int32_t bx0 = 0, by0 = 0, bx1 = 128, by1 = 128;
        int32_t x0, y0, x1, y1;
        if (arg_truthy(a, n, 4)) {
            bx0 = old[0]; by0 = old[1]; bx1 = old[2]; by1 = old[3];
        }
        if (cw < 0) cw = 0;
        if (ch < 0) ch = 0;
        x0 = clamp_screen(x > bx0 ? x : bx0);
        y0 = clamp_screen(y > by0 ? y : by0);
        x1 = clamp_screen(x + cw < bx1 ? x + cw : bx1);
        y1 = clamp_screen(y + ch < by1 ? y + ch : by1);
        if (x1 < x0) x1 = x0;
        if (y1 < y0) y1 = y0;
        ds->clip_x0 = (uint8_t)x0; ds->clip_y0 = (uint8_t)y0;
        ds->clip_x1 = (uint8_t)x1; ds->clip_y1 = (uint8_t)y1;
    }
    if (!a) return 0;
    set_num(&a[0], old[0]); set_num(&a[1], old[1]);
    set_num(&a[2], old[2]); set_num(&a[3], old[3]);
    return 4;
}

#define P8_SECONDARY_PAL 0x5F60

/* One pal(c0, c1, p) entry. Returns the previous color (low nibble). */
static uint8_t pal_set(uint8_t c0, int32_t c1, int32_t p) {
    uint8_t *e, prev;
    c0 &= 0x0F;
    if (p == 1) {
        e = &p8_ram.mem.draw.screen_pal[c0];
        prev = *e;
        *e = (uint8_t)c1;
    } else if (p == 2) {
        e = &p8_ram.raw[P8_SECONDARY_PAL + c0];
        prev = *e;
        *e = (uint8_t)c1;
    } else {
        e = &p8_ram.mem.draw.draw_pal[c0];
        prev = *e;
        *e = (uint8_t)((*e & GFX_TRANSPARENT) | (c1 & 0x0F));
    }
    return (uint8_t)(prev & 0x0F);
}

/* pal(p): reset one palette. The draw palette keeps transparency. */
static void pal_reset_one(int32_t p) {
    int i;
    for (i = 0; i < 16; i++) {
        if (p == 1) p8_ram.mem.draw.screen_pal[i] = (uint8_t)i;
        else if (p == 2) p8_ram.raw[P8_SECONDARY_PAL + i] = 0;
        else p8_ram.mem.draw.draw_pal[i] =
                 (uint8_t)((p8_ram.mem.draw.draw_pal[i] & GFX_TRANSPARENT) | i);
    }
}

/* pal() | pal(p) | pal(c0, c1, [p]) -> previous c1 | pal(table, [p]) */
int p386_builtin_pal(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    (void)vm; (void)w;
    if (!arg_present(a, n, 0)) {
        gfx_reset_pal();
        return 0;
    }
    if (a[0].tag == P386_TAG_TAB) {
        P386Table *t = (P386Table *)(uintptr_t)a[0].value;
        int32_t p = arg_num(a, n, 1, 0);
        P386Value key, k, v;
        key.value = 0;
        key.tag = P386_TAG_NIL;
        while (p386_table_next(t, &key, &k, &v)) {
            if (k.tag == P386_TAG_NUM && v.tag == P386_TAG_NUM) {
                pal_set((uint8_t)(k.value >> 16), v.value >> 16, p);
            }
            key = k;
        }
        return 0;
    }
    if (!arg_present(a, n, 1)) {
        pal_reset_one(arg_num(a, n, 0, 0));
        return 0;
    }
    return ret_num(a, w, P386_FP_INT(pal_set((uint8_t)arg_num(a, n, 0, 0),
                                             arg_num(a, n, 1, 0),
                                             arg_num(a, n, 2, 0))));
}

/* Transparency as palt's bitfield: bit 15 = color 0 ... bit 0 = color 15. */
static uint16_t palt_bits(void) {
    uint16_t bits = 0;
    int i;
    for (i = 0; i < 16; i++) {
        if (p8_ram.mem.draw.draw_pal[i] & GFX_TRANSPARENT) bits |= (uint16_t)(1 << (15 - i));
    }
    return bits;
}

/* palt() | palt(bitfield) -> previous bitfield | palt(c, t) -> previous t */
int p386_builtin_palt(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    uint16_t prev = palt_bits();
    (void)vm; (void)w;
    if (!arg_present(a, n, 1)) {
        uint16_t bits = arg_present(a, n, 0)
                        ? (uint16_t)arg_num(a, n, 0, 0) : 0x8000;
        int i;
        for (i = 0; i < 16; i++) {
            uint8_t *e = &p8_ram.mem.draw.draw_pal[i];
            if (bits & (1 << (15 - i))) *e |= GFX_TRANSPARENT;
            else *e &= (uint8_t)~GFX_TRANSPARENT;
        }
        return ret_num(a, w, P386_FP_INT(prev));
    } else {
        uint8_t c = (uint8_t)(arg_num(a, n, 0, 0) & 0x0F);
        uint8_t *e = &p8_ram.mem.draw.draw_pal[c];
        int was = (*e & GFX_TRANSPARENT) != 0;
        if (arg_truthy(a, n, 1)) *e |= GFX_TRANSPARENT;
        else *e &= (uint8_t)~GFX_TRANSPARENT;
        if (!a) return 0;
        set_bool(&a[0], was);
        return 1;
    }
}

/* print(v, [x, y], [c]) -> x after the text. Without x, y: print at the
 * cursor, move it to the next line and scroll at the bottom. */
int p386_builtin_print(P386VMState *vm, P386Value *args,
                       uint8_t nargs, uint8_t want_rets) {
    P8DrawState *ds = &p8_ram.mem.draw;
    P386Value v;
    P386String *str;
    int32_t x, y, end, lines;
    int at_cursor = nargs < 3;
    uint8_t color_idx = (uint8_t)(at_cursor ? 1 : 3);
    (void)want_rets;
    if (!args || nargs == 0) return 0;

    v = args[0];
    if (v.tag == P386_TAG_NIL) {
        str = p386_string_intern("[nil]", 5);
    } else if (v.tag == P386_TAG_STR) {
        str = (P386String *)(uintptr_t)v.value;
    } else {
        if (p386_builtin_tostr(vm, &v, 1, 1) != 1 || v.tag != P386_TAG_STR) return 0;
        str = (P386String *)(uintptr_t)v.value;
    }
    if (arg_present(args, nargs, color_idx)) {
        ds->pen_color = (uint8_t)arg_num(args, nargs, color_idx, 0);
    }
    x = at_cursor ? ds->cursor_x : arg_num(args, nargs, 1, 0);
    y = at_cursor ? ds->cursor_y : arg_num(args, nargs, 2, 0);
    if (at_cursor && y > 128 - 6) {     /* no room for a line: scroll */
        gfx_scroll_up(y - (128 - 6));
        y = 128 - 6;
    }
    end = gfx_print((const uint8_t *)str->data, str->len, x, y, ds->pen_color, &lines);
    ds->cursor_x = (uint8_t)x;
    y += lines * 6;
    ds->cursor_y = (uint8_t)(y < 0 ? 0 : (y > 255 ? 255 : y));
    return ret_num(args, 1, P386_FP_INT(end));
}

/* ===================================================================== *
 * Shapes and pen state. A color argument sets the pen color (0x5F25);  *
 * 0xXY carries the fill-pattern secondary color X.                     *
 * ===================================================================== */

/* Color argument idx, or the pen color; updates the pen like PICO-8. */
static uint8_t pen_arg(P386Value *a, uint8_t n, uint8_t idx) {
    if (arg_present(a, n, idx)) {
        p8_ram.mem.draw.pen_color = (uint8_t)arg_num(a, n, idx, 0);
    }
    return p8_ram.mem.draw.pen_color;
}

/* line endpoint for the 2-argument forms (0x5F3C in PICO-8; kept here). */
static int32_t line_x, line_y;
static int line_valid;

/* line() | line(x1, y1, [c]) from the last end | line(x0, y0, x1, y1, [c]) */
int p386_builtin_line(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    (void)vm; (void)w;
    if (n == 0) {
        line_valid = 0;
    } else if (n <= 3) {
        int32_t x1 = arg_num(a, n, 0, 0), y1 = arg_num(a, n, 1, 0);
        uint8_t c = pen_arg(a, n, 2);
        if (line_valid) gfx_line(line_x, line_y, x1, y1, c);
        line_x = x1; line_y = y1; line_valid = 1;
    } else {
        int32_t x1 = arg_num(a, n, 2, 0), y1 = arg_num(a, n, 3, 0);
        gfx_line(arg_num(a, n, 0, 0), arg_num(a, n, 1, 0), x1, y1, pen_arg(a, n, 4));
        line_x = x1; line_y = y1; line_valid = 1;
    }
    return 0;
}

static int rect_common(P386Value *a, uint8_t n, int fill) {
    gfx_rect(arg_num(a, n, 0, 0), arg_num(a, n, 1, 0),
             arg_num(a, n, 2, 0), arg_num(a, n, 3, 0), pen_arg(a, n, 4), fill);
    return 0;
}

int p386_builtin_rect(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    (void)vm; (void)w;
    return rect_common(a, n, 0);
}

int p386_builtin_rectfill(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    (void)vm; (void)w;
    return rect_common(a, n, 1);
}

static int circ_common(P386Value *a, uint8_t n, int fill) {
    gfx_circ(arg_num(a, n, 0, 0), arg_num(a, n, 1, 0), arg_num(a, n, 2, 4),
             pen_arg(a, n, 3), fill);
    return 0;
}

int p386_builtin_circ(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    (void)vm; (void)w;
    return circ_common(a, n, 0);
}

int p386_builtin_circfill(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    (void)vm; (void)w;
    return circ_common(a, n, 1);
}

static int oval_common(P386Value *a, uint8_t n, int fill) {
    gfx_oval(arg_num(a, n, 0, 0), arg_num(a, n, 1, 0),
             arg_num(a, n, 2, 0), arg_num(a, n, 3, 0), pen_arg(a, n, 4), fill);
    return 0;
}

int p386_builtin_oval(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    (void)vm; (void)w;
    return oval_common(a, n, 0);
}

int p386_builtin_ovalfill(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    (void)vm; (void)w;
    return oval_common(a, n, 1);
}

/* fillp([p]) -> previous pattern. The integer part is the 4x4 pattern; the
 * 0.5 bit makes pattern 1 bits transparent. */
int p386_builtin_fillp(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    P8DrawState *ds = &p8_ram.mem.draw;
    int32_t prev = (int32_t)((uint32_t)ds->fillp << 16) | ((ds->fillp_flags & 1) ? 0x8000 : 0);
    int32_t fp = arg_fp(a, n, 0, 0);
    (void)vm;
    ds->fillp = (uint16_t)((uint32_t)fp >> 16);
    ds->fillp_flags = (uint8_t)((ds->fillp_flags & ~1) | ((fp & 0x8000) ? 1 : 0));
    return ret_num(a, w, prev);
}

/* color([c]) -> previous pen color; default 6. */
int p386_builtin_color(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    uint8_t prev = p8_ram.mem.draw.pen_color;
    (void)vm;
    p8_ram.mem.draw.pen_color = (uint8_t)arg_num(a, n, 0, 6);
    return ret_num(a, w, P386_FP_INT(prev));
}

/* cursor([x, y, [c]]) -> previous x, y, c */
int p386_builtin_cursor(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    P8DrawState *ds = &p8_ram.mem.draw;
    uint8_t px = ds->cursor_x, py = ds->cursor_y, pc = ds->pen_color;
    (void)vm; (void)w;
    ds->cursor_x = (uint8_t)arg_num(a, n, 0, 0);
    ds->cursor_y = (uint8_t)arg_num(a, n, 1, 0);
    if (arg_present(a, n, 2)) ds->pen_color = (uint8_t)arg_num(a, n, 2, 0);
    if (!a) return 0;
    set_num(&a[0], px);
    set_num(&a[1], py);
    set_num(&a[2], pc);
    return 3;
}

/* ===================================================================== *
 * System: time, stat, flip, printh. The host fills p386_host.           *
 * ===================================================================== */

P386Host p386_host;

int p386_builtin_time(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    (void)vm; (void)n;
    return ret_num(a, w, p386_host.time_fp);
}

/* stat(n): the values pico-386 can answer; 0 for the rest. */
int p386_builtin_stat(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    int32_t v = 0;
    (void)vm;
    switch (arg_num(a, n, 0, 0)) {
    case 7: v = p386_host.fps; break;           /* current fps */
    case 8:                                     /* target fps */
    case 9: v = p386_host.target_fps; break;
    default: break;
    }
    return ret_num(a, w, P386_FP_INT(v));
}

/* flip(): show the frame and wait for the next one (carts with their own
 * loop). No-op without a host. If the host says stop (Esc), the VM halts
 * with P386_VM_ERR_QUIT. */
int p386_builtin_flip(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    (void)vm; (void)a; (void)n; (void)w;
    if (p386_host.flip && p386_host.flip()) return P386_VM_ERR_QUIT;
    return 0;
}

/* printh(str): debug output to the serial port. */
int p386_builtin_printh(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    (void)vm; (void)w;
    if (!a || n == 0) return 0;
    if (a[0].tag == P386_TAG_STR) {
        debug_serial_print(((P386String *)(uintptr_t)a[0].value)->data);
    } else {
        P386Value tmp = a[0];
        if (p386_builtin_tostr(vm, &tmp, 1, 1) == 1 && tmp.tag == P386_TAG_STR) {
            debug_serial_print(((P386String *)(uintptr_t)tmp.value)->data);
        }
    }
    debug_serial_print("\n");
    return 0;
}

/* ===================================================================== *
 * Values and tables (no metatables yet, so raw* equal the plain ops).   *
 * ===================================================================== */

static int ret_str(P386Value *a, const char *s) {
    P386String *str = p386_string_intern(s, (uint32_t)strlen(s));
    if (!a) return 0;
    if (!str) { a[0].value = 0; a[0].tag = P386_TAG_NIL; return 1; }
    a[0].value = (int32_t)(uintptr_t)str;
    a[0].tag = P386_TAG_STR;
    return 1;
}

int p386_builtin_type(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    static const char *const names[] = {
        "nil", "boolean", "number", "string", "table", "function", "function"
    };
    uint32_t tag = (a && n > 0) ? a[0].tag : P386_TAG_NIL;
    (void)vm; (void)w;
    return ret_str(a, tag <= P386_TAG_CFUNC ? names[tag] : "userdata");
}

/* Values that fit in the value stack from a[] onward. */
static uint32_t result_room(P386VMState *vm, P386Value *a) {
    if (!vm || !a || a >= vm->value_stack_end) return 0;
    return (uint32_t)(vm->value_stack_end - a);
}

/* unpack(t, [i], [j]) -> t[i], ..., t[j] */
int p386_builtin_unpack(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    P386Table *t = arg_table(a, n, 0);
    int32_t i, j, k, room;
    P386Value key;
    (void)w;
    if (!t) return 0;
    i = arg_num(a, n, 1, 1);
    j = arg_num(a, n, 2, (int32_t)p386_table_len(t));
    room = (int32_t)result_room(vm, a);
    if (j - i + 1 > room) j = i + room - 1;
    for (k = i; k <= j; k++) {
        key.value = P386_FP_INT(k);
        key.tag = P386_TAG_NUM;
        p386_table_get(t, &key, &a[k - i]);
    }
    return j >= i ? j - i + 1 : 0;
}

/* pack(...) -> {..., n = count} */
int p386_builtin_pack(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    P386Table *t = p386_table_new(n, 1);
    P386Value key, val;
    uint8_t i;
    (void)vm; (void)w;
    if (!a || !t) return 0;
    for (i = 0; i < n; i++) {
        key.value = P386_FP_INT(i + 1);
        key.tag = P386_TAG_NUM;
        p386_table_set(t, &key, &a[i]);
    }
    key.value = (int32_t)(uintptr_t)p386_string_intern("n", 1);
    key.tag = P386_TAG_STR;
    val.value = P386_FP_INT(n);
    val.tag = P386_TAG_NUM;
    p386_table_set(t, &key, &val);
    a[0].value = (int32_t)(uintptr_t)t;
    a[0].tag = P386_TAG_TAB;
    return 1;
}

/* select("#", ...) -> count | select(i, ...) -> the arguments from i on;
 * a negative i counts from the end. */
int p386_builtin_select(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    int32_t i, count;
    (void)vm; (void)w;
    if (!a || n == 0) return 0;
    count = n - 1;
    if (a[0].tag == P386_TAG_STR) {
        set_num(&a[0], count);
        return 1;
    }
    i = arg_num(a, n, 0, 1);
    if (i < 0) i = count + i + 1;
    if (i < 1) i = 1;
    if (i > count) return 0;
    memmove(&a[0], &a[i], (size_t)(count - i + 1) * sizeof(P386Value));
    return count - i + 1;
}

/* split(str, [sep = ","], [convert = true]) -> table. A number sep cuts
 * fixed-size pieces; "" cuts single characters. */
int p386_builtin_split(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    P386String *s;
    P386Table *t;
    const char *sep = ",";
    uint32_t sep_len = 1, chunk = 0, pos = 0, start = 0, idx = 1;
    int convert;
    (void)w;
    if (!a || n == 0 || a[0].tag != P386_TAG_STR) return ret_nil(a, w);
    s = (P386String *)(uintptr_t)a[0].value;
    if (arg_present(a, n, 1)) {
        if (a[1].tag == P386_TAG_NUM) {
            int32_t c = arg_num(a, n, 1, 1);
            chunk = c > 0 ? (uint32_t)c : 1;
        } else if (a[1].tag == P386_TAG_STR) {
            P386String *ss = (P386String *)(uintptr_t)a[1].value;
            sep = ss->data;
            sep_len = ss->len;
        }
    }
    convert = !(n > 2 && a[2].tag == P386_TAG_BOOL && a[2].value == 0);
    t = p386_table_new(8, 0);
    if (!t) return ret_nil(a, w);

    for (;;) {
        uint32_t end;
        P386Value key, val;
        int last;
        if (chunk) {
            end = start + chunk < s->len ? start + chunk : s->len;
            last = end >= s->len;
        } else if (sep_len == 0) {
            if (start >= s->len) break;
            end = start + 1;
            last = end >= s->len;
        } else {
            pos = start;
            while (pos < s->len && memcmp(&s->data[pos], sep, sep_len) != 0) pos++;
            end = pos < s->len ? pos : s->len;
            last = pos >= s->len;
        }
        val.value = (int32_t)(uintptr_t)p386_string_intern(&s->data[start], end - start);
        val.tag = P386_TAG_STR;
        if (convert) {
            P386Value num = val;
            if (p386_builtin_tonum(vm, &num, 1, 1) == 1 && num.tag == P386_TAG_NUM) val = num;
        }
        key.value = P386_FP_INT((int32_t)idx++);
        key.tag = P386_TAG_NUM;
        p386_table_set(t, &key, &val);
        if (last) break;
        start = chunk || sep_len == 0 ? end : end + sep_len;
        if (chunk && start >= s->len) break;
    }
    a[0].value = (int32_t)(uintptr_t)t;
    a[0].tag = P386_TAG_TAB;
    return 1;
}

int p386_builtin_rawget(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    P386Table *t = arg_table(a, n, 0);
    P386Value out;
    (void)vm;
    if (!t || n < 2) return ret_nil(a, w);
    p386_table_get(t, &a[1], &out);
    a[0] = out;
    return 1;
}

int p386_builtin_rawset(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    P386Table *t = arg_table(a, n, 0);
    (void)vm; (void)w;
    if (t && n >= 3) p386_table_set(t, &a[1], &a[2]);
    return t ? 1 : 0;                   /* returns the table */
}

int p386_builtin_rawequal(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    int eq;
    (void)vm; (void)w;
    if (!a) return 0;
    if (n < 2) eq = n == 0 || a[0].tag == P386_TAG_NIL;
    else eq = a[0].tag == a[1].tag &&
              (a[0].tag == P386_TAG_NIL || a[0].value == a[1].value);
    set_bool(&a[0], eq);
    return 1;
}

/* setmetatable(t, mt): mt is a table or nil. Returns t. */
int p386_builtin_setmetatable(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    P386Table *t = arg_table(a, n, 0);
    if (!t) {
        vm->error_msg = "expected table";
        return P386_VM_ERR_TYPE;
    }
    if (n >= 2 && a[1].tag == P386_TAG_TAB) t->metatable = (P386Table *)(uintptr_t)a[1].value;
    else if (n < 2 || a[1].tag == P386_TAG_NIL) t->metatable = 0;
    else {
        vm->error_msg = "expected table";
        return P386_VM_ERR_TYPE;
    }
    (void)w;
    return 1;                           /* a[0] is still t */
}

int p386_builtin_getmetatable(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    P386Table *t = arg_table(a, n, 0);
    (void)vm;
    if (!t || !t->metatable) return ret_nil(a, w);
    a[0].value = (int32_t)(uintptr_t)t->metatable;
    a[0].tag = P386_TAG_TAB;
    return 1;
}

int p386_builtin_rawlen(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    (void)vm;
    if (!a || n == 0) return ret_nil(a, w);
    if (a[0].tag == P386_TAG_TAB) {
        return ret_num(a, w, P386_FP_INT((int32_t)p386_table_len((P386Table *)(uintptr_t)a[0].value)));
    }
    if (a[0].tag == P386_TAG_STR) {
        return ret_num(a, w, P386_FP_INT((int32_t)((P386String *)(uintptr_t)a[0].value)->len));
    }
    return ret_nil(a, w);
}

/* ===================================================================== *
 * Memory blocks. Ranges are clipped to the 64 KB address space; the     *
 * cart ROM (reload/cstore) is p386_host.cart_rom, if the host set one.  *
 * ===================================================================== */

/* Clip [addr, addr+len) to [0, limit); returns the usable length. */
static int32_t clip_range(int32_t *addr, int32_t len, int32_t limit) {
    if (len <= 0 || *addr >= limit) return 0;
    if (*addr < 0) { len += *addr; *addr = 0; }
    if (len <= 0) return 0;
    return *addr + len > limit ? limit - *addr : len;
}

int p386_builtin_memcpy(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    int32_t dst = arg_num(a, n, 0, 0), src = arg_num(a, n, 1, 0);
    int32_t len = arg_num(a, n, 2, 0), l1, l2;
    int32_t d = dst, s2 = src;
    (void)vm; (void)w;
    l1 = clip_range(&d, len, 0x10000);
    l2 = clip_range(&s2, len, 0x10000);
    if (d - dst != s2 - src) return 0;      /* clipped differently: skip */
    len = l1 < l2 ? l1 : l2;
    if (len > 0) memmove(&p8_ram.raw[d], &p8_ram.raw[s2], (size_t)len);
    return 0;
}

int p386_builtin_memset(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    int32_t dst = arg_num(a, n, 0, 0);
    int32_t len = clip_range(&dst, arg_num(a, n, 2, 0), 0x10000);
    (void)vm; (void)w;
    if (len > 0) memset(&p8_ram.raw[dst], arg_num(a, n, 1, 0) & 0xFF, (size_t)len);
    return 0;
}

/* reload([dst, src, len]) copies cart ROM -> RAM; cstore the other way.
 * Without arguments: the 0x4300 bytes of sprite/map/sound data. */
static void rom_copy(P386Value *a, uint8_t n, int to_rom) {
    int32_t dst = arg_num(a, n, 0, 0), src = arg_num(a, n, 1, 0);
    int32_t len = arg_num(a, n, 2, n ? 0 : 0x4300);
    int32_t ram = to_rom ? src : dst, rom = to_rom ? dst : src;
    int32_t ram0 = ram, rom0 = rom, l1, l2;
    if (!p386_host.cart_rom) return;
    l1 = clip_range(&ram, len, 0x10000);
    l2 = clip_range(&rom, len, (int32_t)p386_host.cart_rom_size);
    if (ram - ram0 != rom - rom0) return;
    len = l1 < l2 ? l1 : l2;
    if (len <= 0) return;
    if (to_rom) memcpy(&p386_host.cart_rom[rom], &p8_ram.raw[ram], (size_t)len);
    else memcpy(&p8_ram.raw[ram], &p386_host.cart_rom[rom], (size_t)len);
}

int p386_builtin_reload(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    (void)vm; (void)w;
    rom_copy(a, n, 0);
    return 0;
}

int p386_builtin_cstore(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    (void)vm; (void)w;
    rom_copy(a, n, 1);
    return 0;
}

const P386BuiltinDef p386_builtin_defs[P386_BUILTIN_COUNT] = {
    { P386_BUILTIN_PRINT,    "print",    p386_builtin_print },
    { P386_BUILTIN_CLS,      "cls",      p386_builtin_cls },
    { P386_BUILTIN_PSET,     "pset",     p386_builtin_pset },
    { P386_BUILTIN_PGET,     "pget",     p386_builtin_pget },
    { P386_BUILTIN_LINE,     "line",     p386_builtin_line },
    { P386_BUILTIN_RECT,     "rect",     p386_builtin_rect },
    { P386_BUILTIN_RECTF,    "rectfill", p386_builtin_rectfill },
    { P386_BUILTIN_CIRCFILL, "circfill", p386_builtin_circfill },
    { P386_BUILTIN_SPR,      "spr",      p386_builtin_spr },
    { P386_BUILTIN_MAP,      "map",      p386_builtin_map },
    { P386_BUILTIN_BTN,      "btn",      p386_builtin_btn },
    { P386_BUILTIN_BTNP,     "btnp",     p386_builtin_btnp },
    { P386_BUILTIN_SFX,      "sfx",      p386_builtin_noop },
    { P386_BUILTIN_MUSIC,    "music",    p386_builtin_noop },
    { P386_BUILTIN_PAIRS,    "pairs",    p386_builtin_pairs },
    /* ipairs is implemented in the compiler's Lua prelude; no CFUNC here. */
    { P386_BUILTIN_IPAIRS,   "ipairs",   0 },
    { P386_BUILTIN_ABS,      "abs",      p386_builtin_abs },
    { P386_BUILTIN_FLR,      "flr",      p386_builtin_flr },
    { P386_BUILTIN_CEIL,     "ceil",     p386_builtin_ceil },
    { P386_BUILTIN_SGN,      "sgn",      p386_builtin_sgn },
    { P386_BUILTIN_MIN,      "min",      p386_builtin_min },
    { P386_BUILTIN_MAX,      "max",      p386_builtin_max },
    { P386_BUILTIN_MID,      "mid",      p386_builtin_mid },
    { P386_BUILTIN_SQRT,     "sqrt",     p386_builtin_sqrt },
    { P386_BUILTIN_SIN,      "sin",      p386_builtin_sin },
    { P386_BUILTIN_COS,      "cos",      p386_builtin_cos },
    { P386_BUILTIN_ATAN2,    "atan2",    p386_builtin_atan2 },
    { P386_BUILTIN_RND,      "rnd",      p386_builtin_rnd },
    { P386_BUILTIN_SRAND,    "srand",    p386_builtin_srand },
    { P386_BUILTIN_BAND,     "band",     p386_builtin_band },
    { P386_BUILTIN_BOR,      "bor",      p386_builtin_bor },
    { P386_BUILTIN_BXOR,     "bxor",     p386_builtin_bxor },
    { P386_BUILTIN_BNOT,     "bnot",     p386_builtin_bnot },
    { P386_BUILTIN_SHL,      "shl",      p386_builtin_shl },
    { P386_BUILTIN_SHR,      "shr",      p386_builtin_shr },
    { P386_BUILTIN_LSHR,     "lshr",     p386_builtin_lshr },
    { P386_BUILTIN_ROTL,     "rotl",     p386_builtin_rotl },
    { P386_BUILTIN_ROTR,     "rotr",     p386_builtin_rotr },
    { P386_BUILTIN_PEEK,     "peek",     p386_builtin_peek },
    { P386_BUILTIN_POKE,     "poke",     p386_builtin_poke },
    { P386_BUILTIN_PEEK2,    "peek2",    p386_builtin_peek2 },
    { P386_BUILTIN_POKE2,    "poke2",    p386_builtin_poke2 },
    { P386_BUILTIN_PEEK4,    "peek4",    p386_builtin_peek4 },
    { P386_BUILTIN_POKE4,    "poke4",    p386_builtin_poke4 },
    { P386_BUILTIN_ADD,      "add",      p386_builtin_add },
    { P386_BUILTIN_DEL,      "del",      p386_builtin_del },
    { P386_BUILTIN_DELI,     "deli",     p386_builtin_deli },
    { P386_BUILTIN_COUNTF,   "count",    p386_builtin_count },
    { P386_BUILTIN_TOSTR,    "tostr",    p386_builtin_tostr },
    { P386_BUILTIN_TONUM,    "tonum",    p386_builtin_tonum },
    { P386_BUILTIN_CHR,      "chr",      p386_builtin_chr },
    { P386_BUILTIN_ORD,      "ord",      p386_builtin_ord },
    { P386_BUILTIN_SUB,      "sub",      p386_builtin_sub },
    /* all/foreach are implemented in the compiler's Lua prelude; the slots
     * are reserved (wire protocol) but no CFUNC is registered for them. */
    { P386_BUILTIN_ALL,      "all",      0 },
    { P386_BUILTIN_FOREACH,  "foreach",  0 },
    { P386_BUILTIN_SSPR,     "sspr",     p386_builtin_sspr },
    { P386_BUILTIN_MGET,     "mget",     p386_builtin_mget },
    { P386_BUILTIN_MSET,     "mset",     p386_builtin_mset },
    { P386_BUILTIN_FGET,     "fget",     p386_builtin_fget },
    { P386_BUILTIN_FSET,     "fset",     p386_builtin_fset },
    { P386_BUILTIN_SGET,     "sget",     p386_builtin_sget },
    { P386_BUILTIN_SSET,     "sset",     p386_builtin_sset },
    { P386_BUILTIN_CAMERA,   "camera",   p386_builtin_camera },
    { P386_BUILTIN_CLIP,     "clip",     p386_builtin_clip },
    { P386_BUILTIN_PAL,      "pal",      p386_builtin_pal },
    { P386_BUILTIN_PALT,     "palt",     p386_builtin_palt },
    { P386_BUILTIN_CIRC,     "circ",     p386_builtin_circ },
    { P386_BUILTIN_OVAL,     "oval",     p386_builtin_oval },
    { P386_BUILTIN_OVALFILL, "ovalfill", p386_builtin_ovalfill },
    { P386_BUILTIN_FILLP,    "fillp",    p386_builtin_fillp },
    { P386_BUILTIN_COLOR,    "color",    p386_builtin_color },
    { P386_BUILTIN_CURSOR,   "cursor",   p386_builtin_cursor },
    { P386_BUILTIN_TIME,     "time",     p386_builtin_time },
    { P386_BUILTIN_STAT,     "stat",     p386_builtin_stat },
    { P386_BUILTIN_FLIP,     "flip",     p386_builtin_flip },
    { P386_BUILTIN_PRINTH,   "printh",   p386_builtin_printh },
    { P386_BUILTIN_TYPE,     "type",     p386_builtin_type },
    { P386_BUILTIN_UNPACK,   "unpack",   p386_builtin_unpack },
    { P386_BUILTIN_PACK,     "pack",     p386_builtin_pack },
    { P386_BUILTIN_SELECT,   "select",   p386_builtin_select },
    { P386_BUILTIN_SPLIT,    "split",    p386_builtin_split },
    { P386_BUILTIN_RAWGET,   "rawget",   p386_builtin_rawget },
    { P386_BUILTIN_RAWSET,   "rawset",   p386_builtin_rawset },
    { P386_BUILTIN_RAWEQUAL, "rawequal", p386_builtin_rawequal },
    { P386_BUILTIN_RAWLEN,   "rawlen",   p386_builtin_rawlen },
    { P386_BUILTIN_MEMCPY,   "memcpy",   p386_builtin_memcpy },
    { P386_BUILTIN_MEMSET,   "memset",   p386_builtin_memset },
    { P386_BUILTIN_RELOAD,   "reload",   p386_builtin_reload },
    { P386_BUILTIN_CSTORE,   "cstore",   p386_builtin_cstore },
    { P386_BUILTIN_SETMETATABLE, "setmetatable", p386_builtin_setmetatable },
    { P386_BUILTIN_GETMETATABLE, "getmetatable", p386_builtin_getmetatable }
};

void p386_register_builtins(P386VMState *vm) {
    uint32_t i;
    if (!vm) return;
    for (i = 0; i < (uint32_t)P386_BUILTIN_COUNT; i++) {
        P386BuiltinSlot slot = p386_builtin_defs[i].slot;
        if ((uint32_t)slot >= 256U) continue;
        if (!p386_builtin_defs[i].func) continue; /* Lua-prelude builtin */
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
