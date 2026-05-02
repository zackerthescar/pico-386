#ifndef P386_OBJ_H
#define P386_OBJ_H

/*
 * Minimal heap-object support for pico386 VM.
 *
 * v1 design choices (intentionally simple — see TODO_GC.md, BYTECODE.md §6–7):
 *   - No GC, leak allocator (malloc, never free).
 *   - String: length-prefixed, NUL-terminated, FNV-1a hash precomputed.
 *     Interned via a small open-addressed table; equality is pointer compare.
 *   - Table: linear array of (key,value) entries, plus a tracked
 *     contiguous-int array_len for `#t`. O(n) lookup is fine for the small
 *     tables PICO-8 carts produce; can be promoted to a real hash later.
 *   - LEN supports STR (byte length) and TAB (array_len).
 *   - CONCAT coerces NUM via a fixed-point->decimal stringifier.
 *
 * These helpers are pure C, host-testable, and called from the asm dispatcher
 * via cdecl trampolines (see src/p386_dispatch.asm).
 */

#include <stdint.h>
#include "p386_vm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct P386String {
    uint32_t len;
    uint32_t hash;
    /* data[len] (NUL-terminated for convenience). Allocated inline. */
    char     data[1];
} P386String;

typedef struct P386TableEntry {
    P386Value key;   /* tag=NIL marks empty slot */
    P386Value val;
} P386TableEntry;

typedef struct P386Table {
    P386TableEntry *entries;
    uint32_t        len;        /* populated entries */
    uint32_t        cap;
    uint32_t        array_len;  /* contiguous int keys 1..N (`#t`) */
} P386Table;

typedef struct P386Closure {
    uint32_t proto_index;
    const P386ProtoEntry *proto;
} P386Closure;

/* String construction / interning. */
P386String *p386_string_new(const char *data, uint32_t len);
P386String *p386_string_intern(const char *data, uint32_t len);
int         p386_string_eq(const P386String *a, const P386String *b);

/* CONCAT helper: returns NULL on type error (non-NUM/non-STR). */
P386String *p386_value_concat(const P386Value *a, const P386Value *b);

/* Coerce a NUM (16.16) to a freshly interned string. */
P386String *p386_num_to_string(int32_t fp);

/* Table operations. NEWTABLE(B,C): hints currently ignored. */
P386Table *p386_table_new(uint32_t array_hint, uint32_t hash_hint);

/* Closure construction. Upvalues are added later; v1 closure is a proto handle. */
P386Closure *p386_closure_new(uint32_t proto_index, const P386ProtoEntry *proto);

/* GET: writes nil if absent. Always succeeds. */
void p386_table_get(const P386Table *t, const P386Value *key, P386Value *out);

/* SET: nil value removes; nil key is a no-op (Lua would trap; we silently
 * ignore for v1 to keep the asm trampoline branchless). */
void p386_table_set(P386Table *t, const P386Value *key, const P386Value *val);

/* LEN for tables: array_len. */
uint32_t p386_table_len(const P386Table *t);

/* Generic-for helper: next table entry after key. If key is nil, starts at the
 * first live entry. Writes nil/nil at end or for an unknown key. Returns 1 when
 * an entry was produced, 0 at end. */
int p386_table_next(const P386Table *t, const P386Value *key,
                    P386Value *out_key, P386Value *out_val);

#ifdef __cplusplus
}
#endif

#endif /* P386_OBJ_H */
