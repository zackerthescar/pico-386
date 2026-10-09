#include <stdlib.h>
#include <string.h>
#include "p386_vm.h"
#include "p386_builtins.h"
#include "p386_obj.h"
#include "p386_gc.h"

static int range_ok(uint32_t off, uint32_t len, uint32_t size) {
    return off <= size && len <= size - off;
}

static uint32_t align4(uint32_t x) { return (x + 3U) & ~3U; }

int p386_program_load(const uint8_t *buf, uint32_t size, P386LoadedProgram *out) {
    const P386BcHeader *h;
    uint32_t i;
    uint32_t proto_bytes;
    uint32_t string_bytes;
    uint32_t proto_end;
    uint32_t string_end;

    if (!buf || !out || size < sizeof(P386BcHeader)) return 0;
    h = (const P386BcHeader *)buf;
    if (h->magic != P386_BC_MAGIC || h->version != P386_BC_VERSION) return 0;
    if (h->total_size != size) return 0;
    if (h->n_protos == 0) return 0;
    if (h->n_protos > size / (uint32_t)sizeof(P386ProtoEntry)) return 0;
    if (h->n_strings > size / (uint32_t)sizeof(P386StringEntry)) return 0;

    proto_bytes = h->n_protos * (uint32_t)sizeof(P386ProtoEntry);
    string_bytes = h->n_strings * (uint32_t)sizeof(P386StringEntry);

    if ((h->proto_table_offset & 3U) || (h->string_table_offset & 3U) || (h->bytecode_section_offset & 3U)) return 0;
    if (!range_ok(h->proto_table_offset, proto_bytes, size)) return 0;
    if (!range_ok(h->string_table_offset, string_bytes, size)) return 0;
    if (!range_ok(h->bytecode_section_offset, 0, size)) return 0;

    proto_end = h->proto_table_offset + proto_bytes;
    string_end = h->string_table_offset + string_bytes;
    if (h->proto_table_offset < sizeof(P386BcHeader)) return 0;
    if (h->string_table_offset < proto_end) return 0;
    if (h->bytecode_section_offset < string_end) return 0;

    out->buf = buf;
    out->buf_size = size;
    out->protos = (const P386ProtoEntry *)(buf + h->proto_table_offset);
    out->string_entries = (const P386StringEntry *)(buf + h->string_table_offset);
    out->bytecode_section = buf + h->bytecode_section_offset;

    for (i = 0; i < h->n_strings; i++) {
        const P386StringEntry *s = &out->string_entries[i];
        if (!range_ok(s->data_off, s->len, size)) return 0;
    }

    for (i = 0; i < h->n_protos; i++) {
        const P386ProtoEntry *p = &out->protos[i];
        const uint8_t *consts;
        uint32_t code_end;
        uint32_t const_end;
        uint32_t upval_len;
        uint32_t const_len;
        uint32_t k;

        if ((p->bytecode_off & 3U) != 0) return 0;
        if ((p->bytecode_len & 3U) != 0) return 0;
        if ((p->consts_off & 3U) != 0) return 0;
        if (!range_ok(h->bytecode_section_offset + p->bytecode_off, p->bytecode_len, size)) return 0;
        code_end = align4(p->bytecode_off + p->bytecode_len);
        const_len = (uint32_t)p->n_consts * 8U;
        const_end = p->consts_off + const_len;
        upval_len = (uint32_t)p->n_upvalues * 2U;
        if (p->consts_off < code_end) return 0;
        if (!range_ok(h->bytecode_section_offset + p->consts_off, const_len, size)) return 0;
        if (p->upvals_off < const_end) return 0;
        if (!range_ok(h->bytecode_section_offset + p->upvals_off, upval_len, size)) return 0;
        if (p->n_regs == 0) return 0;
        /* GETFIELD is followed by two cache words: they must be there. */
        {
            const uint32_t *code = (const uint32_t *)(out->bytecode_section + p->bytecode_off);
            uint32_t n = p->bytecode_len / 4, pc = 0;
            while (pc < n) {
                if ((code[pc] & 0xFFU) == P386_OP_GETFIELD) {
                    if (n - pc < 3) return 0;
                    pc += 3;
                } else {
                    pc++;
                }
            }
        }

        consts = out->bytecode_section + p->consts_off;
        for (k = 0; k < p->n_consts; k++) {
            uint32_t cval = (uint32_t)consts[k*8] | ((uint32_t)consts[k*8+1]<<8) |
                            ((uint32_t)consts[k*8+2]<<16) | ((uint32_t)consts[k*8+3]<<24);
            uint32_t ctag = (uint32_t)consts[k*8+4] | ((uint32_t)consts[k*8+5]<<8) |
                            ((uint32_t)consts[k*8+6]<<16) | ((uint32_t)consts[k*8+7]<<24);
            switch (ctag) {
            case 0: /* NIL  */ if (cval != 0) return 0; break;
            case 1: /* BOOL */ if (cval > 1) return 0; break;
            case 2: /* NUM  */ break;
            case 3: /* STR  */ if (cval >= h->n_strings) return 0; break;
            case 6: /* CFUNC */ break;
            default: return 0;
            }
        }
    }

    return 1;
}

/* Make the main thread the running thread, with its own stacks. */
static void use_main_thread(P386VMState *vm) {
    P386Thread *m = &vm->main_thread;
    m->is_main = 1;
    m->started = 1;
    m->status = P386_CO_RUNNING;
    m->resumer = 0;
    m->stack = vm->value_stack;
    m->stack_slots = P386_VALUE_STACK_SLOTS;
    m->frames = vm->call_stack;
    m->frames_max = P386_CALL_STACK_DEPTH;
    m->varargs = vm->vararg_stack;
    m->varargs_max = P386_VARARG_STACK_SLOTS;
    vm->cur = m;
    vm->stack_start = vm->value_stack;
    vm->value_stack_end = vm->value_stack + P386_VALUE_STACK_SLOTS;
    vm->frames = vm->call_stack;
    vm->frames_max = P386_CALL_STACK_DEPTH;
    vm->varargs = vm->vararg_stack;
    vm->varargs_max = P386_VARARG_STACK_SLOTS;
    vm->tail_reg = 0;
}

void p386_vm_init(P386VMState *vm) {
    /* One VM owns the heap: objects of an earlier VM are freed. */
    p386_gc_reset();
    memset(vm, 0, sizeof(*vm));
    vm->status = P386_VM_OK;
    vm->base = vm->value_stack;
    vm->top = vm->value_stack;
    use_main_thread(vm);
    vm->current_closure = 0;
    vm->open_upvalues = 0;
    vm->vararg_base = 0;
    vm->vararg_count = 0;
    vm->vararg_sp = 0;
    p386_register_builtins(vm);
}

/* Interned string constants of the loaded program. Like the heap, they
 * belong to the one VM: the array of an earlier VM is freed here. */
static P386String **g_kstr;

static int intern_constants(P386VMState *vm, const uint8_t *buf) {
    const P386BcHeader *h = (const P386BcHeader *)buf;
    uint32_t i;
    free(g_kstr);
    g_kstr = 0;
    if (h->n_strings) {
        g_kstr = (P386String **)malloc(h->n_strings * sizeof(P386String *));
        if (!g_kstr) return 0;
    }
    for (i = 0; i < h->n_strings; i++) {
        const P386StringEntry *e = &vm->program.string_entries[i];
        g_kstr[i] = p386_string_intern((const char *)buf + e->data_off, e->len);
        if (!g_kstr[i]) return 0;
    }
    vm->kstr = g_kstr;
    vm->n_kstr = h->n_strings;
    return 1;
}

/* Fill the inline cache of each GETFIELD: the word after it holds the
 * interned name (0 if K[C] is not a string), the next one the slot that the
 * dispatcher remembers (start at 0). The VM runs from the caller's buffer,
 * so the caches are written there: the buffer must be writable. */
static void init_field_caches(P386VMState *vm) {
    uint32_t i, n_protos = ((const P386BcHeader *)vm->program.buf)->n_protos;
    for (i = 0; i < n_protos; i++) {
        const P386ProtoEntry *p = &vm->program.protos[i];
        uint32_t *code = (uint32_t *)(vm->program.bytecode_section + p->bytecode_off);
        const uint32_t *consts = (const uint32_t *)(vm->program.bytecode_section + p->consts_off);
        uint32_t n = p->bytecode_len / 4, pc = 0;
        while (pc < n) {
            uint32_t ins = code[pc];
            if ((ins & 0xFFU) == P386_OP_GETFIELD) {
                uint32_t k = ins >> 24;
                code[pc + 1] = (k < p->n_consts && consts[k * 2 + 1] == P386_TAG_STR)
                    ? (uint32_t)vm->kstr[consts[k * 2]] : 0;
                code[pc + 2] = 0;
                pc += 3;
            } else {
                pc++;
            }
        }
    }
}

int p386_vm_load(P386VMState *vm, const uint8_t *buf, uint32_t size) {
    p386_vm_init(vm);
    if (!p386_program_load(buf, size, &vm->program)) {
        vm->status = P386_VM_ERR_BAD_BC;
        vm->error_msg = "bad bytecode container";
        return 0;
    }
    if (!intern_constants(vm, buf)) {
        vm->status = P386_VM_ERR_BOUNDS;
        vm->error_msg = "out of memory";
        return 0;
    }
    init_field_caches(vm);
    vm->current_proto = vm->program.protos;
    vm->base = vm->value_stack;
    vm->top = vm->base + vm->current_proto->n_regs;
    return 1;
}

int p386_vm_call_global(P386VMState *vm, uint16_t slot, uint8_t nargs, uint8_t want_rets) {
    P386Closure *closure;
    uint8_t i;

    if (!vm) return P386_VM_ERR_TYPE;
    if (vm->globals[slot].tag == P386_TAG_NIL) return P386_VM_HALTED;
    if (vm->globals[slot].tag != P386_TAG_FUNC) {
        vm->status = P386_VM_ERR_TYPE;
        vm->error_msg = "expected function";
        return vm->status;
    }
    closure = (P386Closure *)(uintptr_t)vm->globals[slot].value;
    if (!closure || !closure->proto) {
        vm->status = P386_VM_ERR_TYPE;
        vm->error_msg = "expected function";
        return vm->status;
    }
    if (nargs > closure->proto->n_params) nargs = closure->proto->n_params;

    /* No frame is active between host calls: a safe point with no stack. */
    if (p386_gc_pending) p386_gc_collect(vm, 0);
    if ((uint32_t)closure->proto->n_regs > P386_VALUE_STACK_SLOTS) {
        vm->status = P386_VM_ERR_BOUNDS;
        vm->error_msg = "register/constant out of bounds";
        return vm->status;
    }

    vm->status = P386_VM_OK;
    vm->error_msg = 0;
    /* A host call always runs on the main thread, even if an earlier call
     * stopped inside a coroutine (Esc in flip). */
    use_main_thread(vm);
    vm->call_depth = 0;
    vm->base = vm->value_stack;
    vm->current_proto = closure->proto;
    vm->current_closure = (uint32_t)(uintptr_t)closure;
    vm->open_upvalues = 0;
    vm->vararg_base = 0;
    vm->vararg_count = 0;
    vm->vararg_sp = 0;
    vm->top = vm->base + closure->proto->n_regs;
    vm->ip = (const uint32_t *)(vm->program.bytecode_section + closure->proto->bytecode_off);

    for (i = 0; i < closure->proto->n_regs; i++) {
        vm->base[i].value = 0;
        vm->base[i].tag = P386_TAG_NIL;
    }
    (void)want_rets;
    return p386_vm_run(vm);
}

const char *p386_vm_status_name(int status) {
    switch (status) {
    case P386_VM_OK: return "ok";
    case P386_VM_HALTED: return "halted";
    case P386_VM_ERR_BAD_BC: return "bad bytecode";
    case P386_VM_ERR_OPCODE: return "bad opcode";
    case P386_VM_ERR_TYPE: return "type error";
    case P386_VM_ERR_DIV0: return "division by zero";
    case P386_VM_ERR_BOUNDS: return "bounds error";
    case P386_VM_ERR_UNIMPL: return "unimplemented opcode";
    case P386_VM_ERR_QUIT: return "quit requested";
    default: return "unknown";
    }
}
