/*
 * pico386 coroutines: cocreate / coresume / yield / costatus.
 *
 * Each coroutine is a P386Thread heap object with its own value stack,
 * call frames and varargs stack. The dispatcher works on the stacks of the
 * running thread through vm->stack_start / frames / varargs.
 *
 * The dispatcher never calls into C to switch threads. coresume and yield
 * are C builtins that store a request and return P386_VM_SWITCH. The
 * dispatcher then exits like on an error, and p386_vm_run (below) does the
 * switch and starts the dispatcher again at the saved ip of the other
 * thread. The C stack does not grow, so this is not re-entry.
 *
 * When a coroutine ends (RETURN at depth 0, or an error), it becomes dead:
 * its open upvalues are closed, its stacks are freed, and the resumer gets
 * true + results or false + message. An error in a coroutine does not stop
 * the cart.
 */

#include <stdlib.h>
#include <string.h>
#include "p386_co.h"
#include "p386_gc.h"
#include "p386_obj.h"

/* ---------- context ---------------------------------------------------- */

static void save_context(P386VMState *vm, P386Thread *t) {
    t->base = vm->base;
    t->top = vm->top;
    t->proto = vm->current_proto;
    t->ip = vm->ip;
    t->closure = vm->current_closure;
    t->open_upvalues = vm->open_upvalues;
    t->call_depth = vm->call_depth;
    t->vararg_base = vm->vararg_base;
    t->vararg_count = vm->vararg_count;
    t->vararg_sp = vm->vararg_sp;
}

static void load_context(P386VMState *vm, P386Thread *t) {
    vm->stack_start = t->stack;
    vm->value_stack_end = t->stack + t->stack_slots;
    vm->frames = t->frames;
    vm->frames_max = t->frames_max;
    vm->varargs = t->varargs;
    vm->varargs_max = t->varargs_max;
    vm->base = t->base;
    vm->top = t->top;
    vm->current_proto = t->proto;
    vm->ip = t->ip;
    vm->current_closure = t->closure;
    vm->open_upvalues = t->open_upvalues;
    vm->call_depth = t->call_depth;
    vm->vararg_base = t->vararg_base;
    vm->vararg_count = t->vararg_count;
    vm->vararg_sp = t->vararg_sp;
    vm->cur = t;
    t->status = P386_CO_RUNNING;
}

/* Write the results of t's pending coresume/yield call (t is running). */
static void deliver(P386VMState *vm, P386Thread *t, const P386Value *vals, uint32_t n) {
    P386Value *dest = t->ret_dest;
    uint32_t count = t->ret_want ? t->ret_want : n;
    uint32_t i;
    if (dest + count > vm->value_stack_end) count = (uint32_t)(vm->value_stack_end - dest);
    for (i = 0; i < count; i++) {
        if (i < n) dest[i] = vals[i];
        else { dest[i].value = 0; dest[i].tag = P386_TAG_NIL; }
    }
    vm->top = dest + count;
}

static void set_bool(P386Value *v, int b) {
    v->value = b ? 1 : 0;
    v->tag = P386_TAG_BOOL;
}

static void set_str(P386Value *v, const char *s) {
    P386String *str = p386_string_intern(s, (uint32_t)strlen(s));
    v->value = (int32_t)(uintptr_t)str;
    v->tag = str ? P386_TAG_STR : P386_TAG_NIL;
}

/* ---------- stacks ------------------------------------------------------ */

static uint32_t stack_bytes(void) {
    return P386_CO_STACK_SLOTS * (uint32_t)sizeof(P386Value)
         + P386_CO_FRAMES * (uint32_t)sizeof(P386CallFrame)
         + P386_CO_VARARG_SLOTS * (uint32_t)sizeof(P386Value);
}

static int alloc_stacks(P386Thread *t) {
    t->stack = (P386Value *)calloc(P386_CO_STACK_SLOTS, sizeof(P386Value));
    t->frames = (P386CallFrame *)calloc(P386_CO_FRAMES, sizeof(P386CallFrame));
    t->varargs = (P386Value *)calloc(P386_CO_VARARG_SLOTS, sizeof(P386Value));
    if (!t->stack || !t->frames || !t->varargs) {
        free(t->stack); free(t->frames); free(t->varargs);
        t->stack = 0; t->frames = 0; t->varargs = 0;
        return 0;
    }
    t->stack_slots = P386_CO_STACK_SLOTS;
    t->frames_max = P386_CO_FRAMES;
    t->varargs_max = P386_CO_VARARG_SLOTS;
    p386_gc_account((int32_t)stack_bytes());
    return 1;
}

void p386_co_free_stacks(P386Thread *t) {
    if (!t->stack) return;
    free(t->stack); free(t->frames); free(t->varargs);
    t->stack = 0; t->frames = 0; t->varargs = 0;
    t->base = t->top = 0;
    p386_gc_account(-(int32_t)stack_bytes());
}

/* Set up t to run its body with args. t's stacks become the VM's. */
static int start_thread(P386VMState *vm, P386Thread *t, const P386Value *args, uint32_t n) {
    const P386Closure *c = (const P386Closure *)(uintptr_t)t->fn.value;
    const P386ProtoEntry *p = c->proto;
    uint32_t i, nparams = p->n_params;

    if (!alloc_stacks(t)) return 0;         /* n_regs <= 255 < stack slots */
    t->started = 1;
    t->base = t->stack;
    t->top = t->stack + p->n_regs;
    t->proto = p;
    t->ip = (const uint32_t *)(vm->program.bytecode_section + p->bytecode_off);
    t->closure = (uint32_t)(uintptr_t)c;
    t->open_upvalues = 0;
    t->call_depth = 0;
    t->vararg_base = t->vararg_count = t->vararg_sp = 0;
    for (i = 0; i < n && i < nparams; i++) t->stack[i] = args[i];
    if ((p->flags & P386_PROTO_FLAG_VARARG) && n > nparams) {
        uint32_t extra = n - nparams;
        if (extra > P386_CO_VARARG_SLOTS) extra = P386_CO_VARARG_SLOTS;
        for (i = 0; i < extra; i++) t->varargs[i] = args[nparams + i];
        t->vararg_count = t->vararg_sp = extra;
    }
    load_context(vm, t);
    return 1;
}

/* ---------- switching --------------------------------------------------- */

/* Save the running thread after its coresume/yield call. */
static void suspend_current(P386VMState *vm) {
    P386Thread *t = vm->cur;
    save_context(vm, t);
    if (vm->tail_reg) {
        /* `return coresume(...)`: the frame is already gone. Results go to
         * R[A+1..] and the thread continues with RETURN A+1, B=0. */
        t->ret_dest += 1;
        t->ret_want = 0;
        t->tail_insn = P386_OP_RETURN | (vm->tail_reg << 8);
        t->ip = &t->tail_insn;
        vm->tail_reg = 0;
    }
}

/* The running coroutine ended. st: HALTED or an error. */
static void finish_current(P386VMState *vm, int st) {
    P386Thread *t = vm->cur;
    P386Thread *r = t->resumer;
    P386Value res[1 + P386_XFER_MAX];
    uint32_t n = 1;

    if (st == P386_VM_HALTED) {
        P386Value *v = vm->ret_base;
        set_bool(&res[0], 1);
        for (; v < vm->top && n < 1 + P386_XFER_MAX; v++) res[n++] = *v;
    } else {
        set_bool(&res[0], 0);
        set_str(&res[1], vm->error_msg ? vm->error_msg : p386_vm_status_name(st));
        n = 2;
    }
    /* Closures made in the coroutine keep the values of its locals. */
    p386_close_upvalues((P386Upvalue **)&vm->open_upvalues, t->stack);
    t->status = P386_CO_DEAD;
    t->resumer = 0;
    p386_co_free_stacks(t);
    vm->status = P386_VM_OK;
    vm->error_msg = 0;
    load_context(vm, r);
    deliver(vm, r, res, n);
}

static void do_switch(P386VMState *vm) {
    P386Thread *from = vm->cur;
    P386Value vals[1 + P386_XFER_MAX];
    uint32_t i, n = vm->xfer_n;

    suspend_current(vm);
    vm->status = P386_VM_OK;
    vm->error_msg = 0;
    if (vm->switch_kind == P386_SWITCH_RESUME) {
        P386Thread *to = vm->switch_target;
        from->status = P386_CO_NORMAL;
        to->resumer = from;
        if (!to->started) {
            if (!start_thread(vm, to, vm->xfer, n)) {
                to->resumer = 0;
                to->status = P386_CO_DEAD;
                load_context(vm, from);
                set_bool(&vals[0], 0);
                set_str(&vals[1], "out of memory");
                deliver(vm, from, vals, 2);
            }
            return;
        }
        load_context(vm, to);
        deliver(vm, to, vm->xfer, n);        /* results of its yield */
    } else {
        P386Thread *to = from->resumer;
        from->status = P386_CO_SUSPENDED;
        from->resumer = 0;
        set_bool(&vals[0], 1);
        for (i = 0; i < n; i++) vals[1 + i] = vm->xfer[i];
        load_context(vm, to);
        deliver(vm, to, vals, n + 1);
    }
}

int p386_vm_run(P386VMState *vm) {
    int st = p386_vm_exec(vm, 0);
    for (;;) {
        if (st == P386_VM_SWITCH) {
            do_switch(vm);
        } else if (vm->cur->is_main || st == P386_VM_ERR_QUIT) {
            return st;
        } else {
            finish_current(vm, st);
        }
        st = p386_vm_exec(vm, 1);
    }
}

/* ---------- builtins ---------------------------------------------------- */

static P386Thread *arg_thread(P386Value *a, uint8_t n) {
    if (!a || n == 0 || a[0].tag != P386_TAG_THREAD) return 0;
    return (P386Thread *)(uintptr_t)a[0].value;
}

static void take_xfer(P386VMState *vm, const P386Value *v, uint32_t n) {
    uint32_t i;
    if (n > P386_XFER_MAX) n = P386_XFER_MAX;
    for (i = 0; i < n; i++) vm->xfer[i] = v[i];
    vm->xfer_n = n;
}

/* cocreate(f) -> coroutine */
int p386_builtin_cocreate(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    P386Thread *t;
    (void)w;
    if (!a || n == 0 || a[0].tag != P386_TAG_FUNC) {
        vm->error_msg = "expected function";
        return P386_VM_ERR_TYPE;
    }
    t = (P386Thread *)p386_gc_alloc(P386_GC_THREAD, (uint32_t)sizeof(P386Thread));
    if (!t) {
        vm->error_msg = "out of memory";
        return P386_VM_ERR_BOUNDS;
    }
    t->fn = a[0];
    t->status = P386_CO_SUSPENDED;
    a[0].value = (int32_t)(uintptr_t)t;
    a[0].tag = P386_TAG_THREAD;
    return 1;
}

/* coresume(co, ...) -> true, values... | false, message */
int p386_builtin_coresume(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    P386Thread *t = arg_thread(a, n);
    if (!t) {
        vm->error_msg = "expected coroutine";
        return P386_VM_ERR_TYPE;
    }
    if (t->status != P386_CO_SUSPENDED) {
        set_bool(&a[0], 0);
        set_str(&a[1], t->status == P386_CO_DEAD ? "cannot resume dead coroutine"
                                                 : "cannot resume non-suspended coroutine");
        return 2;
    }
    take_xfer(vm, a + 1, n - 1u);
    vm->cur->ret_dest = a - 1;              /* the CALL's R[A] */
    vm->cur->ret_want = w;
    vm->switch_kind = P386_SWITCH_RESUME;
    vm->switch_target = t;
    return P386_VM_SWITCH;
}

/* yield(...) -> the arguments of the next coresume */
int p386_builtin_yield(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    if (vm->cur->is_main) {
        vm->error_msg = "attempt to yield from outside a coroutine";
        return P386_VM_ERR_TYPE;
    }
    take_xfer(vm, a, n);
    vm->cur->ret_dest = a - 1;
    vm->cur->ret_want = w;
    vm->switch_kind = P386_SWITCH_YIELD;
    return P386_VM_SWITCH;
}

/* costatus(co) -> "suspended" | "running" | "normal" | "dead" */
int p386_builtin_costatus(P386VMState *vm, P386Value *a, uint8_t n, uint8_t w) {
    static const char *const names[] = { "suspended", "running", "normal", "dead" };
    P386Thread *t = arg_thread(a, n);
    (void)w;
    if (!t) {
        vm->error_msg = "expected coroutine";
        return P386_VM_ERR_TYPE;
    }
    set_str(&a[0], names[t->status & 3]);
    return 1;
}

/* ---------- collector --------------------------------------------------- */

static void mark_range(const P386Value *from, const P386Value *to) {
    for (; from < to; from++) p386_gc_mark_value(from);
}

void p386_co_mark_saved(const P386Thread *t) {
    const P386Upvalue *uv;
    uint32_t i;
    if (!t->started || t->status == P386_CO_DEAD || !t->stack) return;
    if (t->base && t->proto) {
        mark_range(t->stack, p386_frames_end(t->base, t->proto, t->frames, t->call_depth));
    }
    mark_range(t->varargs, t->varargs + t->vararg_sp);
    p386_gc_mark_object((void *)(uintptr_t)t->closure);
    for (i = 0; i < t->call_depth; i++) {
        p386_gc_mark_object((void *)(uintptr_t)t->frames[i].return_closure);
    }
    for (uv = (const P386Upvalue *)(uintptr_t)t->open_upvalues; uv; uv = uv->next_open) {
        p386_gc_mark_object((void *)uv);
    }
}

void p386_co_traverse(const P386Thread *t) {
    p386_gc_mark_value(&t->fn);
    if (t->resumer && !t->resumer->is_main) p386_gc_mark_object(t->resumer);
    /* A running thread's context is in the VM: the roots cover it. */
    if (t->status != P386_CO_RUNNING) p386_co_mark_saved(t);
}
