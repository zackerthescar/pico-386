/*
 * pico386 metatables: slow paths for the dispatcher (see p386_meta.h).
 *
 * The dispatcher calls these only when the fast path cannot give a result:
 * a table read gave nil, a table with a metatable is written, or an
 * operator got an operand of the wrong type. Plain tables and numbers do
 * not come here.
 */

#include <stdint.h>
#include <string.h>
#include "p386_meta.h"
#include "p386_builtins.h"
#include "p386_gc.h"

/* Limit for __index / __newindex chains. A cycle stops with an error. */
#define META_CHAIN_MAX 32

P386Value p386_meta_call[1 + P386_META_MAX_ARGS];
uint32_t  p386_meta_nargs;

static const char *const event_names[P386_EV_COUNT] = {
    "__add", "__sub", "__mul", "__div", "__idiv", "__mod", "__pow",
    "__unm", "__concat", "__len", "__eq", "__lt", "__le"
};

static P386String *event_str[P386_EV_COUNT];
static P386String *index_str, *newindex_str, *call_str;

/* p386_gc_reset frees all strings: forget the cached names. */
void p386_meta_reset(void) {
    memset(event_str, 0, sizeof(event_str));
    index_str = newindex_str = call_str = 0;
}

void p386_meta_mark_roots(void) {
    uint32_t i;
    for (i = 0; i < P386_EV_COUNT; i++) p386_gc_mark_object(event_str[i]);
    p386_gc_mark_object(index_str);
    p386_gc_mark_object(newindex_str);
    p386_gc_mark_object(call_str);
}

static P386String *intern_name(P386String **slot, const char *name) {
    if (!*slot) *slot = p386_string_intern(name, (uint32_t)strlen(name));
    return *slot;
}

static int fail(P386VMState *vm, int status, const char *msg) {
    vm->status = status;
    vm->error_msg = msg;
    return status;
}

static void set_tab(P386Value *v, P386Table *t) {
    v->value = (int32_t)(uintptr_t)t;
    v->tag = P386_TAG_TAB;
}

static void set_nil(P386Value *v) {
    v->value = 0;
    v->tag = P386_TAG_NIL;
}

/* Raw read of mt[name]. Writes nil if mt or the name is missing. */
static void get_handler(const P386Table *mt, P386String *name, P386Value *out) {
    P386Value key;
    set_nil(out);
    if (!mt || !name) return;
    key.value = (int32_t)(uintptr_t)name;
    key.tag = P386_TAG_STR;
    p386_table_get(mt, &key, out);
}

static const P386Table *metatable_of(const P386Value *v) {
    if (v->tag != P386_TAG_TAB) return 0;
    return ((const P386Table *)(uintptr_t)v->value)->metatable;
}

/* Use handler h with args[0..nargs). A Lua function is staged for the
 * dispatcher; a CFUNC runs now and its first result goes to *out. */
static int invoke(P386VMState *vm, const P386Value *h, const P386Value *args,
                  uint32_t nargs, P386Value *out) {
    uint32_t i;
    if (nargs > P386_META_MAX_ARGS)
        return fail(vm, P386_VM_ERR_BOUNDS, "too many metamethod arguments");
    if (h->tag == P386_TAG_FUNC) {
        p386_meta_call[0] = *h;
        for (i = 0; i < nargs; i++) p386_meta_call[1 + i] = args[i];
        p386_meta_nargs = nargs;
        return P386_META_CALL;
    }
    if (h->tag == P386_TAG_CFUNC) {
        P386Value buf[P386_META_MAX_ARGS];
        P386CFunc fn = (P386CFunc)(uintptr_t)h->value;
        int n;
        for (i = 0; i < nargs; i++) buf[i] = args[i];
        n = fn(vm, buf, (uint8_t)nargs, 1);
        if (n < 0) return fail(vm, n, "metamethod failed");
        if (out) {
            if (n > 0) *out = buf[0];
            else set_nil(out);
        }
        return P386_META_DONE;
    }
    return fail(vm, P386_VM_ERR_TYPE, "expected function");
}

int p386_meta_index(P386VMState *vm, P386Table *t, const P386Value *key,
                    P386Value *out) {
    P386String *name = intern_name(&index_str, "__index");
    int depth;
    for (depth = 0; depth < META_CHAIN_MAX; depth++) {
        P386Value h;
        get_handler(t->metatable, name, &h);
        if (h.tag == P386_TAG_TAB) {
            t = (P386Table *)(uintptr_t)h.value;
            p386_table_get(t, key, out);
            if (out->tag != P386_TAG_NIL || !t->metatable) return P386_META_DONE;
            continue;
        }
        if (h.tag == P386_TAG_NIL) {
            set_nil(out);
            return P386_META_DONE;
        }
        {
            P386Value args[2];
            set_tab(&args[0], t);
            args[1] = *key;
            return invoke(vm, &h, args, 2, out);
        }
    }
    return fail(vm, P386_VM_ERR_TYPE, "__index chain too long");
}

int p386_meta_newindex(P386VMState *vm, P386Table *t, const P386Value *key,
                       const P386Value *val) {
    P386String *name = intern_name(&newindex_str, "__newindex");
    int depth;
    for (depth = 0; depth < META_CHAIN_MAX; depth++) {
        P386Value cur, h;
        if (!t->metatable) {
            p386_table_set(t, key, val);
            return P386_META_DONE;
        }
        /* An existing key is a plain write. */
        p386_table_get(t, key, &cur);
        get_handler(t->metatable, name, &h);
        if (cur.tag != P386_TAG_NIL || h.tag == P386_TAG_NIL) {
            p386_table_set(t, key, val);
            return P386_META_DONE;
        }
        if (h.tag == P386_TAG_TAB) {
            t = (P386Table *)(uintptr_t)h.value;
            continue;
        }
        {
            P386Value args[3];
            set_tab(&args[0], t);
            args[1] = *key;
            args[2] = *val;
            return invoke(vm, &h, args, 3, 0);
        }
    }
    return fail(vm, P386_VM_ERR_TYPE, "__newindex chain too long");
}

int p386_meta_arith(P386VMState *vm, uint32_t event, const P386Value *a,
                    const P386Value *b, P386Value *out) {
    P386String *name;
    P386Value h, args[2];
    if (event >= P386_EV_COUNT) return fail(vm, P386_VM_ERR_TYPE, "bad metamethod event");
    name = intern_name(&event_str[event], event_names[event]);
    get_handler(metatable_of(a), name, &h);
    if (h.tag == P386_TAG_NIL) get_handler(metatable_of(b), name, &h);
    if (h.tag == P386_TAG_NIL) {
        if (event == P386_EV_EQ) {
            out->value = 0;
            out->tag = P386_TAG_BOOL;
            return P386_META_DONE;
        }
        if (event == P386_EV_CONCAT)
            return fail(vm, P386_VM_ERR_TYPE, "expected string or number");
        return fail(vm, P386_VM_ERR_TYPE, "expected number");
    }
    args[0] = *a;
    args[1] = *b;
    return invoke(vm, &h, args, 2, out);
}

int p386_meta_has_len(const P386Table *t) {
    P386Value h;
    get_handler(t->metatable, intern_name(&event_str[P386_EV_LEN], "__len"), &h);
    return h.tag != P386_TAG_NIL;
}

int p386_meta_call_value(P386VMState *vm, const P386Value *obj,
                         const P386Value *args, uint32_t nargs) {
    P386Value h;
    uint32_t i;
    get_handler(metatable_of(obj), intern_name(&call_str, "__call"), &h);
    if (h.tag != P386_TAG_FUNC) return fail(vm, P386_VM_ERR_TYPE, "expected function");
    if (nargs + 1 > P386_META_MAX_ARGS)
        return fail(vm, P386_VM_ERR_BOUNDS, "too many metamethod arguments");
    p386_meta_call[0] = h;
    p386_meta_call[1] = *obj;
    for (i = 0; i < nargs; i++) p386_meta_call[2 + i] = args[i];
    p386_meta_nargs = nargs + 1;
    return P386_META_CALL;
}
