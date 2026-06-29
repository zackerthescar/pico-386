#ifndef P386_VM_H
#define P386_VM_H

#include <stdint.h>
#include <stddef.h>
#include "p386_bytecode.h"
#include "p386_value.h"

#define P386_VALUE_STACK_SLOTS 4096
#define P386_CALL_STACK_DEPTH  256
#define P386_VARARG_STACK_SLOTS 256

#define P386_VM_OK          0
#define P386_VM_HALTED      1
#define P386_VM_ERR_BAD_BC -1
#define P386_VM_ERR_OPCODE -2
#define P386_VM_ERR_TYPE   -3
#define P386_VM_ERR_DIV0   -4
#define P386_VM_ERR_BOUNDS -5
#define P386_VM_ERR_UNIMPL -6

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
    uint8_t padding[2];
    /* Caller's vararg window, restored when this frame returns. */
    uint32_t saved_vararg_base;
    uint32_t saved_vararg_count;
    uint32_t saved_vararg_sp;
} P386CallFrame;
#pragma pack(pop)

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
    P386Value globals[256];
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
} P386VMState;
#pragma pack(pop)

#include "p386_layout.h"

int p386_program_load(const uint8_t *buf, uint32_t size, P386LoadedProgram *out);
void p386_vm_init(P386VMState *vm);
int p386_vm_load(P386VMState *vm, const uint8_t *buf, uint32_t size);
int p386_vm_run(P386VMState *vm);
int p386_vm_call_global(P386VMState *vm, uint8_t slot, uint8_t nargs, uint8_t want_rets);
const char *p386_vm_status_name(int status);

#endif
