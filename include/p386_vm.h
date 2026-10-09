#ifndef P386_VM_H
#define P386_VM_H

#include <stdint.h>
#include <stddef.h>
#include "p386_bytecode.h"
#include "p386_value.h"
#include "p386_obj.h"

#define P386_VALUE_STACK_SLOTS 4096
#define P386_CALL_STACK_DEPTH  256
#define P386_VARARG_STACK_SLOTS 256
#define P386_GLOBAL_SLOTS      1024   /* GETGLOBAL/SETGLOBAL Bx range */

#define P386_VM_OK          0
#define P386_VM_HALTED      1
#define P386_VM_ERR_BAD_BC -1
#define P386_VM_ERR_OPCODE -2
#define P386_VM_ERR_TYPE   -3
#define P386_VM_ERR_DIV0   -4
#define P386_VM_ERR_BOUNDS -5
#define P386_VM_ERR_UNIMPL -6
#define P386_VM_ERR_QUIT   -7    /* a builtin asked to stop (Esc in flip) */
#define P386_VM_SWITCH     -8    /* coresume/yield: switch threads (internal) */

/* Coroutine stacks. */
#define P386_CO_STACK_SLOTS   512
#define P386_CO_FRAMES        64
#define P386_CO_VARARG_SLOTS  64
#define P386_XFER_MAX         32    /* values passed by coresume/yield */

typedef struct P386LoadedProgram {
    const uint8_t *buf;
    uint32_t buf_size;
    const P386ProtoEntry *protos;
    const P386StringEntry *string_entries;
    const uint8_t *bytecode_section;
} P386LoadedProgram;

#pragma pack(push, 1)
typedef struct P386CallFrame {
    uint32_t return_ip;
    uint32_t return_base;
    uint32_t return_proto;
    uint32_t return_closure;
    uint8_t return_reg;
    uint8_t want_rets;
    /* Result fix-up on return (metamethods): 0 none, 1 to boolean,
     * 2 to negated boolean. */
    uint8_t post;
    uint8_t padding;
    /* Caller's vararg window, restored when this frame returns. */
    uint32_t saved_vararg_base;
    uint32_t saved_vararg_count;
    uint32_t saved_vararg_sp;
} P386CallFrame;
#pragma pack(pop)

/* Switch requests (vm->switch_kind). */
#define P386_SWITCH_RESUME 1
#define P386_SWITCH_YIELD  2

/* Thread status. */
#define P386_CO_SUSPENDED 0     /* not started yet, or stopped in yield */
#define P386_CO_RUNNING   1
#define P386_CO_NORMAL    2     /* resumed another coroutine and waits */
#define P386_CO_DEAD      3

/* A thread of execution: the main thread (inside P386VMState) or a
 * coroutine (a P386_GC_THREAD heap object). While a thread runs, its
 * context is in the VM fields; otherwise it is saved here. */
typedef struct P386Thread {
    uint8_t status;
    uint8_t is_main;
    uint8_t started;
    uint8_t ret_want;                   /* want_rets of the pending call */
    struct P386Thread *resumer;         /* thread to go back to on yield */
    P386Value fn;                       /* body (coroutines) */
    /* Stacks. A coroutine owns them; they are freed when it dies. */
    P386Value *stack;
    uint32_t stack_slots;
    P386CallFrame *frames;
    uint32_t frames_max;
    P386Value *varargs;
    uint32_t varargs_max;
    /* Saved context. */
    P386Value *base;
    P386Value *top;
    const P386ProtoEntry *proto;
    const uint32_t *ip;
    uint32_t closure;
    uint32_t open_upvalues;
    uint32_t call_depth;
    uint32_t vararg_base;
    uint32_t vararg_count;
    uint32_t vararg_sp;
    /* Where the results of the pending coresume/yield go. */
    P386Value *ret_dest;
    /* After a coresume/yield in tail position the thread continues at this
     * RETURN instruction (see p386_co.c). */
    uint32_t tail_insn;
} P386Thread;

#pragma pack(push, 1)
typedef struct P386VMState {
    int32_t status;
    const char *error_msg;
    uint32_t last_opcode;
    P386LoadedProgram program;
    P386Value value_stack[P386_VALUE_STACK_SLOTS];
    P386Value *base;
    P386Value *top;
    P386Value *value_stack_end;
    P386Value globals[P386_GLOBAL_SLOTS];
    const P386ProtoEntry *current_proto;
    const uint32_t *ip;
    uint32_t current_closure;
    uint32_t open_upvalues;
    P386CallFrame call_stack[P386_CALL_STACK_DEPTH];
    uint32_t call_depth;
    /* Varargs: each Lua frame that declares `...` owns a contiguous window
     * [vararg_base, vararg_base+vararg_count) inside vararg_stack. New windows
     * are pushed at vararg_sp. The VARARG opcode copies from this window. */
    uint32_t vararg_base;
    uint32_t vararg_count;
    uint32_t vararg_sp;
    P386Value vararg_stack[P386_VARARG_STACK_SLOTS];

    /* Stacks of the running thread. For the main thread they are the
     * arrays above. The dispatcher uses these pointers. */
    P386Value *stack_start;
    P386CallFrame *frames;
    uint32_t frames_max;
    P386Value *varargs;
    uint32_t varargs_max;
    P386Value *ret_base;            /* first value of a depth-0 RETURN */
    uint32_t tail_reg;              /* nonzero: SWITCH came from a TAILCALL */
    /* String constants, interned once at load (index = string table
     * index). LOADK and RK operands read them here; they are GC roots. */
    P386String **kstr;
    uint32_t n_kstr;

    /* Coroutines (C only). */
    P386Thread *cur;
    P386Thread main_thread;
    uint32_t switch_kind;           /* P386_SWITCH_* from coresume/yield */
    P386Thread *switch_target;
    uint32_t xfer_n;
    P386Value xfer[P386_XFER_MAX];
} P386VMState;
#pragma pack(pop)

#include "p386_layout.h"

int p386_program_load(const uint8_t *buf, uint32_t size, P386LoadedProgram *out);
void p386_vm_init(P386VMState *vm);
int p386_vm_load(P386VMState *vm, const uint8_t *buf, uint32_t size);
int p386_vm_run(P386VMState *vm);
/* The asm dispatcher. cont = 0: start the current function at its first
 * instruction; 1: continue at vm->ip. p386_vm_run is the entry for C. */
int p386_vm_exec(P386VMState *vm, int cont);
int p386_vm_call_global(P386VMState *vm, uint16_t slot, uint8_t nargs, uint8_t want_rets);
const char *p386_vm_status_name(int status);

#endif
