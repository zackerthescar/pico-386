#ifndef P386_BUILTINS_H
#define P386_BUILTINS_H

#include <stdint.h>
#include "builtins.h"
#include "p386_vm.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * C builtin ABI.
 *
 * A CFUNC receives the VM plus a contiguous argument/result window. Arguments
 * begin at args[0] and there are nargs live argument values. The function may
 * write up to want_rets results back to args[0..]. It returns the number of
 * results written, or a negative P386_VM_ERR_* status on failure.
 *
 * This is intentionally small and C-callable so the NASM dispatcher can grow a
 * CALL-to-CFUNC path without depending on any host runtime details.
 */
typedef int (*P386CFunc)(P386VMState *vm, P386Value *args,
                         uint8_t nargs, uint8_t want_rets);

typedef struct P386BuiltinDef {
    P386BuiltinSlot slot;
    const char *name;
    P386CFunc func;
} P386BuiltinDef;

extern const P386BuiltinDef p386_builtin_defs[P386_BUILTIN_COUNT];

void p386_register_builtins(P386VMState *vm);
P386CFunc p386_builtin_func(P386BuiltinSlot slot);
const char *p386_builtin_name(P386BuiltinSlot slot);

/* Initial simple stubs. */
int p386_builtin_print(P386VMState *vm, P386Value *args,
                       uint8_t nargs, uint8_t want_rets);
int p386_builtin_cls(P386VMState *vm, P386Value *args,
                     uint8_t nargs, uint8_t want_rets);
int p386_builtin_pset(P386VMState *vm, P386Value *args,
                      uint8_t nargs, uint8_t want_rets);
int p386_builtin_pget(P386VMState *vm, P386Value *args,
                      uint8_t nargs, uint8_t want_rets);
int p386_builtin_noop(P386VMState *vm, P386Value *args,
                      uint8_t nargs, uint8_t want_rets);
int p386_builtin_pairs(P386VMState *vm, P386Value *args,
                       uint8_t nargs, uint8_t want_rets);

/* Trivial-tier builtins: math, bitwise, memory, table, string. Each obeys the
 * CFUNC ABI (read args[0..nargs), write up to want_rets results to args[0..],
 * return result count). All math is 16.16 fixed point; no FPU is used. */
int p386_builtin_abs(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_flr(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_ceil(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_sgn(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_min(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_max(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_mid(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_sqrt(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_sin(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_cos(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_atan2(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_rnd(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_srand(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_band(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_bor(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_bxor(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_bnot(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_shl(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_shr(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_lshr(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_rotl(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_rotr(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_peek(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_poke(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_peek2(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_poke2(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_peek4(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_poke4(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_add(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_del(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_deli(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_count(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_tostr(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_tonum(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_chr(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_ord(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);
int p386_builtin_sub(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w);

#ifdef __cplusplus
}
#endif

#endif /* P386_BUILTINS_H */
