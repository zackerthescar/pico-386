#include <stdint.h>
#include <stdlib.h>
#include "p386_builtins.h"
#include "p386_obj.h"
#include "mem.h"

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

/* ===================================================================== *
 * Trivial-tier builtins: pure value-in / value-out, no rendering, no VM  *
 * re-entrancy. All numbers are 16.16 fixed point; no FPU is used.        *
 * ===================================================================== */

/* Write a single NUM result (16.16) if one was requested. Returns the count. */
static int ret_num(P386Value *a, uint8_t w, int32_t fp) {
    if (a && w > 0) { a[0].value = fp; a[0].tag = P386_TAG_NUM; }
    return (w > 0) ? 1 : 0;
}

static int ret_nil(P386Value *a, uint8_t w) {
    set_nil_results(a, w);
    return (w > 0) ? 1 : 0;
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
    return ret_nil(a, w);
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
    return ret_nil(a, w);
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
    return ret_nil(a, w);
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
    return ret_nil(a, w);
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
    if (a && w > 0) { a[0] = val; return 1; }
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
        if (a && w > 0) { a[0] = target; return 1; }
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
    if (a && w > 0) { a[0] = removed; return 1; }
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
    if (a && w > 0) {
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
    if (!a || w == 0) return 0;
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
    if (!a || w == 0) return 0;
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
    if (!a || w == 0 || n == 0 || a[0].tag != P386_TAG_STR) return ret_nil(a, w);
    s = (P386String *)(uintptr_t)a[0].value;
    if (n >= 2 && a[1].tag == P386_TAG_NUM) idx = (uint32_t)(a[1].value >> 16);
    if (idx < 1 || idx > s->len) return ret_nil(a, w);
    return ret_num(a, w, (int32_t)((uint32_t)(uint8_t)s->data[idx - 1] << 16));
}

int p386_builtin_sub(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    P386String *s, *r;
    int32_t len, i0, i1;
    (void)vm;
    if (!a || w == 0 || n == 0 || a[0].tag != P386_TAG_STR) return ret_nil(a, w);
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
    { P386_BUILTIN_IPAIRS,   "ipairs",   p386_builtin_pairs },
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
    { P386_BUILTIN_SUB,      "sub",      p386_builtin_sub }
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
