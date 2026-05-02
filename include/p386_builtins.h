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
int p386_builtin_noop(P386VMState *vm, P386Value *args,
                      uint8_t nargs, uint8_t want_rets);
int p386_builtin_pairs(P386VMState *vm, P386Value *args,
                       uint8_t nargs, uint8_t want_rets);

#ifdef __cplusplus
}
#endif

#endif /* P386_BUILTINS_H */
