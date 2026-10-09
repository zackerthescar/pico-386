#ifndef P386_CO_H
#define P386_CO_H

/* Coroutines: see src/p386_co.c. */

#include "p386_vm.h"

int p386_builtin_cocreate(P386VMState *vm, P386Value *args, uint8_t nargs, uint8_t want_rets);
int p386_builtin_coresume(P386VMState *vm, P386Value *args, uint8_t nargs, uint8_t want_rets);
int p386_builtin_yield(P386VMState *vm, P386Value *args, uint8_t nargs, uint8_t want_rets);
int p386_builtin_costatus(P386VMState *vm, P386Value *args, uint8_t nargs, uint8_t want_rets);

/* Collector hooks. */
void p386_co_traverse(const P386Thread *t);       /* a thread object */
void p386_co_mark_saved(const P386Thread *t);     /* a saved context */
void p386_co_free_stacks(P386Thread *t);

#endif /* P386_CO_H */
