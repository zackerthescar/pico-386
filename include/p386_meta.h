#ifndef P386_META_H
#define P386_META_H

/*
 * Metatables: lookup of metamethods for the dispatcher's slow paths.
 *
 * The VM cannot re-enter p386_vm_run from C. So when a metamethod is a Lua
 * function, these helpers do not call it. They put the function and its
 * arguments in p386_meta_call[] and return P386_META_CALL. The dispatcher
 * then pushes a normal Lua frame whose result goes to the destination
 * register. A CFUNC metamethod is called directly.
 *
 * Return values: P386_META_DONE (result is in *out), P386_META_CALL (a Lua
 * call is staged), or a negative VM status (vm->status and vm->error_msg
 * are set).
 */

#include <stdint.h>
#include "p386_vm.h"
#include "p386_obj.h"

#define P386_META_DONE 0
#define P386_META_CALL 1

/* Staged Lua call: [0] = function (TAG_FUNC), [1..nargs] = arguments. */
#define P386_META_MAX_ARGS 32
extern P386Value p386_meta_call[1 + P386_META_MAX_ARGS];
extern uint32_t  p386_meta_nargs;

/* Events for p386_meta_arith. The order is shared with p386_dispatch.asm. */
enum {
    P386_EV_ADD, P386_EV_SUB, P386_EV_MUL, P386_EV_DIV, P386_EV_IDIV,
    P386_EV_MOD, P386_EV_POW, P386_EV_UNM, P386_EV_CONCAT, P386_EV_LEN,
    P386_EV_EQ, P386_EV_LT, P386_EV_LE,
    P386_EV_COUNT
};

/* t[key] was nil in t and t has a metatable: follow __index. */
int p386_meta_index(P386VMState *vm, P386Table *t, const P386Value *key,
                    P386Value *out);

/* t[key] = val where t has a metatable: follow __newindex. */
int p386_meta_newindex(P386VMState *vm, P386Table *t, const P386Value *key,
                       const P386Value *val);

/* Binary or unary operator event on a, b (for a unary event, b == a).
 * For P386_EV_EQ and no handler, *out is false and the result is DONE. */
int p386_meta_arith(P386VMState *vm, uint32_t event, const P386Value *a,
                    const P386Value *b, P386Value *out);

/* Nonzero when t has a __len handler. */
int p386_meta_has_len(const P386Table *t);

/* Call of a value that is not a function: stage __call(obj, args...).
 * Only Lua-function handlers are supported. */
int p386_meta_call_value(P386VMState *vm, const P386Value *obj,
                         const P386Value *args, uint32_t nargs);

#endif /* P386_META_H */
