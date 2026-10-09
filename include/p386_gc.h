#ifndef P386_GC_H
#define P386_GC_H

/*
 * Precise stop-the-world mark-sweep collector for VM heap objects.
 *
 * Every heap object (string, table, closure, upvalue) has a P386GCHeader
 * immediately before it, so object pointers and struct layouts do not
 * change. The collector reads only tagged P386Values and known object
 * fields. It never scans raw memory.
 *
 * The collector never runs inside an allocation. An allocation only sets
 * p386_gc_pending when the heap has grown past the threshold. Collection
 * happens at safe points, where every live value is in a root:
 *   - the dispatcher, at backward jumps, loop steps and Lua function entry
 *     (p386_gc_safepoint);
 *   - the host, between callbacks (p386_gc_collect with no stack).
 * C code (builtins, metamethod helpers) can thus keep object pointers in
 * locals: no collection can occur until it returns to the dispatcher.
 */

#include <stdint.h>
#include "p386_value.h"

/* Object types. */
#define P386_GC_STR     1
#define P386_GC_TAB     2
#define P386_GC_CLOSURE 3
#define P386_GC_UPVAL   4
#define P386_GC_THREAD  5

/* Header flags. */
#define P386_GC_INTERNED 0x01   /* string is in the intern table */

typedef struct P386GCHeader {
    struct P386GCHeader *next;  /* all objects */
    struct P386GCHeader *gray;  /* mark work list */
    uint32_t size;              /* bytes, header included */
    uint8_t  type;
    uint8_t  marked;
    uint8_t  flags;
    uint8_t  pad;
} P386GCHeader;

#define P386_GC_HDR(obj) (((P386GCHeader *)(void *)(obj)) - 1)

/* Collection is due at the next safe point. Read by the dispatcher. */
extern uint32_t p386_gc_pending;

/* Test aids. stress: collect at every safe point. poison: fill freed
 * objects with 0xDD and do not give them back to malloc, so that a use
 * after free shows quickly. */
extern int p386_gc_stress;
extern int p386_gc_poison;
/* Marks of a freed object seen in poison mode (freed objects are kept,
 * poisoned, while it is on). Not 0: a stale pointer was scanned. */
extern uint32_t p386_gc_bad_marks;

/* Profiling: called at the start (0) and end (1) of each collection. */
extern void (*p386_gc_hook)(int end);

/* Allocate an object of `size` bytes (header not included). Memory is
 * zeroed. Returns NULL when out of memory. */
void *p386_gc_alloc(uint8_t type, uint32_t size);

/* Account memory an object owns outside its own block (table entries). */
void p386_gc_account(int32_t delta);

/* Bytes in use: objects and the memory they own. */
uint32_t p386_gc_bytes(void);

/* Free every object. The VM must not be used with old objects after this. */
void p386_gc_reset(void);

struct P386VMState;

/* Full collection. Live stack slots are vm->value_stack .. live_end
 * (exclusive). live_end NULL: no frame is active (host between callbacks). */
void p386_gc_collect(struct P386VMState *vm, P386Value *live_end);

/* End of the live stack: the highest end (base + n_regs) of the current
 * frame and of every caller frame. */
struct P386CallFrame;
struct P386ProtoEntry;
P386Value *p386_frames_end(P386Value *base, const struct P386ProtoEntry *proto,
                           const struct P386CallFrame *frames, uint32_t depth);

/* Dispatcher safe point: collect with the current frame as the stack top. */
void p386_gc_safepoint(struct P386VMState *vm);

/* Mark one value or object (for root sets outside p386_gc.c). */
void p386_gc_mark_value(const P386Value *v);
void p386_gc_mark_object(void *obj);

#endif /* P386_GC_H */
