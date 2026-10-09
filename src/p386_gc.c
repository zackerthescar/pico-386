/*
 * pico386 garbage collector: precise stop-the-world mark-sweep.
 * See include/p386_gc.h for when collection can run.
 */

#include <stdlib.h>
#include <string.h>
#include "p386_gc.h"
#include "p386_vm.h"
#include "p386_obj.h"
#include "p386_meta.h"
#include "p386_co.h"

/* Collect when the heap reaches this size or twice the live size after the
 * last collection, whichever is larger. */
#define GC_MIN_THRESHOLD (64UL * 1024UL)

uint32_t p386_gc_pending;
uint32_t p386_gc_bad_marks;
void (*p386_gc_hook)(int end);
int p386_gc_stress;
int p386_gc_poison;

static P386GCHeader *all_objects;
static P386GCHeader *gray_list;
static uint32_t heap_bytes;
static uint32_t threshold = GC_MIN_THRESHOLD;

static void check_threshold(void) {
    if (heap_bytes >= threshold || p386_gc_stress) p386_gc_pending = 1;
}

void *p386_gc_alloc(uint8_t type, uint32_t size) {
    uint32_t total = (uint32_t)sizeof(P386GCHeader) + size;
    P386GCHeader *h = (P386GCHeader *)calloc(1, total);
    if (!h) return 0;
    h->type = type;
    h->size = total;
    h->next = all_objects;
    all_objects = h;
    heap_bytes += total;
    check_threshold();
    return h + 1;
}

void p386_gc_account(int32_t delta) {
    heap_bytes += (uint32_t)delta;
    if (delta > 0) check_threshold();
}

uint32_t p386_gc_bytes(void) {
    return heap_bytes;
}

/* ---------- free ----------------------------------------------------- */

static void free_object(P386GCHeader *h) {
    if (h->type == P386_GC_TAB) {
        heap_bytes -= p386_table_free_parts((P386Table *)(h + 1), p386_gc_poison);
    }
    if (h->type == P386_GC_THREAD) p386_co_free_stacks((P386Thread *)(h + 1));
    heap_bytes -= h->size;
    if (p386_gc_poison) {
        /* Quarantine: keep the block, poisoned, so a later mark of a stale
         * pointer finds it (p386_gc_bad_marks) instead of a reused block. */
        memset(h, 0xDD, h->size);
        return;
    }
    free(h);
}

void p386_gc_reset(void) {
    P386GCHeader *h = all_objects;
    while (h) {
        P386GCHeader *next = h->next;
        free_object(h);
        h = next;
    }
    all_objects = 0;
    gray_list = 0;
    heap_bytes = 0;
    threshold = GC_MIN_THRESHOLD;
    p386_gc_pending = 0;
    p386_string_intern_reset();
    p386_meta_reset();
}

/* ---------- mark ----------------------------------------------------- */

void p386_gc_mark_object(void *obj) {
    P386GCHeader *h;
    if (!obj) return;
    h = P386_GC_HDR(obj);
    if (h->type == 0xDD) {          /* freed and poisoned: a missing root */
        p386_gc_bad_marks++;
        return;
    }
    if (h->marked) return;
    h->marked = 1;
    if (h->type == P386_GC_STR) return;     /* no references */
    h->gray = gray_list;
    gray_list = h;
}

void p386_gc_mark_value(const P386Value *v) {
    switch (v->tag) {
    case P386_TAG_STR:
    case P386_TAG_TAB:
    case P386_TAG_FUNC:
    case P386_TAG_THREAD:
        p386_gc_mark_object((void *)(uintptr_t)v->value);
        break;
    default:
        break;
    }
}

static void mark_range(const P386Value *from, const P386Value *to) {
    for (; from < to; from++) p386_gc_mark_value(from);
}

static void traverse(P386GCHeader *h) {
    switch (h->type) {
    case P386_GC_TAB:
        p386_table_traverse((P386Table *)(h + 1));
        break;
    case P386_GC_CLOSURE: {
        const P386Closure *c = (const P386Closure *)(h + 1);
        uint32_t i;
        for (i = 0; i < c->n_upvalues; i++) p386_gc_mark_object(c->upvalues[i]);
        break;
    }
    case P386_GC_UPVAL: {
        const P386Upvalue *uv = (const P386Upvalue *)(h + 1);
        /* An open upvalue's value is a stack slot: the stack scan marks it
         * while its frame is live. */
        if (uv->slot == &uv->closed) p386_gc_mark_value(&uv->closed);
        break;
    }
    case P386_GC_THREAD:
        p386_co_traverse((const P386Thread *)(h + 1));
        break;
    default:
        break;
    }
}

static void propagate(void) {
    while (gray_list) {
        P386GCHeader *h = gray_list;
        gray_list = h->gray;
        h->gray = 0;
        traverse(h);
    }
}

static void mark_roots(P386VMState *vm, P386Value *live_end) {
    uint32_t i;
    const P386Upvalue *uv;

    mark_range(vm->globals, vm->globals + P386_GLOBAL_SLOTS);
    /* The running thread: its context is in the VM fields. */
    if (live_end) {
        mark_range(vm->stack_start, live_end);
        mark_range(vm->varargs, vm->varargs + vm->vararg_sp);
        p386_gc_mark_object((void *)(uintptr_t)vm->current_closure);
        for (i = 0; i < vm->call_depth; i++) {
            p386_gc_mark_object((void *)(uintptr_t)vm->frames[i].return_closure);
        }
    }
    /* A running coroutine, and through it the threads waiting for it. */
    if (vm->cur && !vm->cur->is_main) p386_gc_mark_object(vm->cur);
    /* The main thread waits in coresume while a coroutine runs. */
    if (vm->cur && vm->cur != &vm->main_thread) p386_co_mark_saved(&vm->main_thread);
    /* String constants of the program (vm->kstr). */
    for (i = 0; i < vm->n_kstr; i++) p386_gc_mark_object(vm->kstr[i]);
    /* Open upvalues stay on the list until closed; keep them allocated. */
    for (uv = (const P386Upvalue *)(uintptr_t)vm->open_upvalues; uv; uv = uv->next_open) {
        p386_gc_mark_object((void *)uv);
    }
    p386_meta_mark_roots();
}

/* ---------- sweep ---------------------------------------------------- */

static void sweep(void) {
    P386GCHeader **link = &all_objects;
    while (*link) {
        P386GCHeader *h = *link;
        if (h->marked) {
            h->marked = 0;
            link = &h->next;
        } else {
            *link = h->next;
            free_object(h);
        }
    }
}

void p386_gc_collect(P386VMState *vm, P386Value *live_end) {
    if (p386_gc_hook) p386_gc_hook(0);
    mark_roots(vm, live_end);
    propagate();
    /* The intern table is weak: drop strings that are about to be freed. */
    p386_string_intern_sweep();
    sweep();
    threshold = heap_bytes * 2;
    if (threshold < GC_MIN_THRESHOLD) threshold = GC_MIN_THRESHOLD;
    p386_gc_pending = p386_gc_stress;
    if (p386_gc_hook) p386_gc_hook(1);
}

P386Value *p386_frames_end(P386Value *base, const P386ProtoEntry *proto,
                           const P386CallFrame *frames, uint32_t depth) {
    P386Value *end = base + proto->n_regs;
    uint32_t i;
    for (i = 0; i < depth; i++) {
        P386Value *fb = (P386Value *)(uintptr_t)frames[i].return_base;
        const P386ProtoEntry *fp = (const P386ProtoEntry *)(uintptr_t)frames[i].return_proto;
        if (fb + fp->n_regs > end) end = fb + fp->n_regs;
    }
    return end;
}

void p386_gc_safepoint(P386VMState *vm) {
    /* A callee frame starts at its first argument inside the caller's
     * registers, so it can end below its caller. Scan to the highest frame
     * end: every slot in that range was written or cleared when it came
     * into range, and slots above it may hold stale pointers to freed
     * objects. */
    p386_gc_collect(vm, p386_frames_end(vm->base, vm->current_proto,
                                        vm->frames, vm->call_depth));
}
