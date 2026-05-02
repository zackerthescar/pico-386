#include <string.h>
#include "p386_vm.h"

static int range_ok(uint32_t off, uint32_t len, uint32_t size) {
    return off <= size && len <= size - off;
}

static uint32_t align4(uint32_t x) { return (x + 3U) & ~3U; }

int p386_program_load(const uint8_t *buf, uint32_t size, P386LoadedProgram *out) {
    const P386BcHeader *h;
    uint32_t i;

    if (!buf || !out || size < sizeof(P386BcHeader)) return 0;
    h = (const P386BcHeader *)buf;
    if (h->magic != P386_BC_MAGIC || h->version != P386_BC_VERSION) return 0;
    if (h->total_size != size) return 0;
    if (h->n_protos == 0) return 0;
    if ((h->proto_table_offset & 3U) || (h->string_table_offset & 3U) || (h->bytecode_section_offset & 3U)) return 0;
    if (!range_ok(h->proto_table_offset, h->n_protos * (uint32_t)sizeof(P386ProtoEntry), size)) return 0;
    if (!range_ok(h->string_table_offset, h->n_strings * (uint32_t)sizeof(P386StringEntry), size)) return 0;
    if (!range_ok(h->bytecode_section_offset, 0, size)) return 0;

    out->buf = buf;
    out->buf_size = size;
    out->protos = (const P386ProtoEntry *)(buf + h->proto_table_offset);
    out->string_entries = (const P386StringEntry *)(buf + h->string_table_offset);
    out->bytecode_section = buf + h->bytecode_section_offset;

    for (i = 0; i < h->n_protos; i++) {
        const P386ProtoEntry *p = &out->protos[i];
        uint32_t code_end;
        uint32_t const_end;
        uint32_t upval_len;
        if ((p->bytecode_len & 3U) != 0) return 0;
        if (!range_ok(h->bytecode_section_offset + p->bytecode_off, p->bytecode_len, size)) return 0;
        code_end = align4(p->bytecode_off + p->bytecode_len);
        const_end = p->consts_off + (uint32_t)p->n_consts * 8U;
        upval_len = (uint32_t)p->n_upvalues * 2U;
        if (p->consts_off < code_end) return 0;
        if (!range_ok(h->bytecode_section_offset + p->consts_off, (uint32_t)p->n_consts * 8U, size)) return 0;
        if (p->upvals_off < const_end) return 0;
        if (!range_ok(h->bytecode_section_offset + p->upvals_off, upval_len, size)) return 0;
        if (p->n_regs == 0) return 0;
    }

    for (i = 0; i < h->n_strings; i++) {
        const P386StringEntry *s = &out->string_entries[i];
        if (!range_ok(s->data_off, s->len, size)) return 0;
    }

    return 1;
}

void p386_vm_init(P386VMState *vm) {
    memset(vm, 0, sizeof(*vm));
    vm->status = P386_VM_OK;
    vm->base = vm->value_stack;
    vm->top = vm->value_stack;
    vm->value_stack_end = vm->value_stack + P386_VALUE_STACK_SLOTS;
}

int p386_vm_load(P386VMState *vm, const uint8_t *buf, uint32_t size) {
    p386_vm_init(vm);
    if (!p386_program_load(buf, size, &vm->program)) {
        vm->status = P386_VM_ERR_BAD_BC;
        vm->error_msg = "bad bytecode container";
        return 0;
    }
    vm->current_proto = vm->program.protos;
    vm->base = vm->value_stack;
    vm->top = vm->base + vm->current_proto->n_regs;
    return 1;
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
    default: return "unknown";
    }
}
