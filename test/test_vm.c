#include <string.h>
#include "test.h"
#include "p386_vm.h"

#define MAX_CODE 32
#define MAX_CONST 16
#define BUF_SIZE 1024

typedef struct VmFixture {
    unsigned char buf[BUF_SIZE];
    unsigned long code_len;
    unsigned long const_len;
    int n_consts;
} VmFixture;

static void put32(unsigned char *p, unsigned long v) {
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
    p[2] = (unsigned char)((v >> 16) & 0xff);
    p[3] = (unsigned char)((v >> 24) & 0xff);
}

static void fx_init(VmFixture *f) {
    memset(f, 0, sizeof(*f));
}

static void fx_emit(VmFixture *f, unsigned long ins) {
    put32(f->buf + 32 + 24 + f->code_len, ins);
    f->code_len += 4;
}

static void fx_const(VmFixture *f, long value, unsigned long tag) {
    unsigned long off = 32 + 24 + MAX_CODE * 4 + f->const_len;
    put32(f->buf + off, (unsigned long)value);
    put32(f->buf + off + 4, tag);
    f->const_len += 8;
    f->n_consts++;
}

static unsigned char *fx_finish(VmFixture *f, unsigned long *out_len) {
    unsigned long proto_off = 32;
    unsigned long str_off = 32 + 24;
    unsigned long bc_off = 32 + 24;
    unsigned long code_off = 0;
    unsigned long const_off = MAX_CODE * 4;
    unsigned long upval_off = const_off + f->const_len;
    unsigned long total = bc_off + upval_off;

    put32(f->buf + 0, P386_BC_MAGIC);
    put32(f->buf + 4, P386_BC_VERSION);
    put32(f->buf + 8, total);
    put32(f->buf + 12, 1);
    put32(f->buf + 16, 0);
    put32(f->buf + 20, proto_off);
    put32(f->buf + 24, str_off);
    put32(f->buf + 28, bc_off);

    put32(f->buf + proto_off + 0, code_off);
    put32(f->buf + proto_off + 4, f->code_len);
    put32(f->buf + proto_off + 8, const_off);
    put32(f->buf + proto_off + 12, upval_off);
    f->buf[proto_off + 16] = (unsigned char)f->n_consts;
    f->buf[proto_off + 17] = 0;
    f->buf[proto_off + 18] = 32;
    f->buf[proto_off + 19] = 0;
    f->buf[proto_off + 20] = P386_PROTO_FLAG_MAIN;

    *out_len = total;
    return f->buf;
}

static int run_fixture(VmFixture *f, P386VMState *vm) {
    unsigned long len;
    unsigned char *buf = fx_finish(f, &len);
    if (!p386_vm_load(vm, buf, len)) return vm->status;
    return p386_vm_run(vm);
}

#define ASSERT_TAG(vm, reg, t) ASSERT_EQ((t), (vm).value_stack[(reg)].tag)
#define ASSERT_VAL(vm, reg, v) ASSERT_EQ((v), (vm).value_stack[(reg)].value)
#define ASSERT_NUM(vm, reg, v) do { ASSERT_TAG(vm, reg, P386_TAG_NUM); ASSERT_VAL(vm, reg, v); } while(0)
#define ASSERT_BOOL(vm, reg, v) do { ASSERT_TAG(vm, reg, P386_TAG_BOOL); ASSERT_VAL(vm, reg, v); } while(0)
#define ASSERT_NIL(vm, reg) do { ASSERT_TAG(vm, reg, P386_TAG_NIL); ASSERT_VAL(vm, reg, 0); } while(0)

TEST(vm_loader_rejects_bad_magic) {
    VmFixture f;
    unsigned long len;
    P386LoadedProgram p;
    fx_init(&f);
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 1, 0));
    fx_finish(&f, &len);
    f.buf[0] = 'X';
    ASSERT_FALSE(p386_program_load(f.buf, len, &p));
    PASS();
}

TEST(vm_loadk_add_return_constants) {
    VmFixture f;
    P386VMState vm;
    fx_init(&f);
    fx_const(&f, P386_FP_INT(2), P386_TAG_NUM);
    fx_const(&f, P386_FP_INT(3), P386_TAG_NUM);
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 0, 0));
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 1, 1));
    fx_emit(&f, P386_ABC(P386_OP_ADD, 2, P386_RK_REG(0), P386_RK_CONST(1)));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 2, 2, 0));
    ASSERT_EQ(P386_VM_HALTED, run_fixture(&f, &vm));
    ASSERT_NUM(vm, 2, P386_FP_INT(5));
    PASS();
}

TEST(vm_move_booleans_and_loadn) {
    VmFixture f;
    P386VMState vm;
    fx_init(&f);
    fx_emit(&f, P386_ABC(P386_OP_LOADT, 0, 0, 0));
    fx_emit(&f, P386_ABC(P386_OP_LOADF, 1, 0, 0));
    fx_emit(&f, P386_ABC(P386_OP_MOVE, 2, 0, 0));
    fx_emit(&f, P386_ABC(P386_OP_LOADN, 3, 3, 0));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 1, 0));
    ASSERT_EQ(P386_VM_HALTED, run_fixture(&f, &vm));
    ASSERT_BOOL(vm, 0, 1);
    ASSERT_BOOL(vm, 1, 0);
    ASSERT_BOOL(vm, 2, 1);
    ASSERT_NIL(vm, 3);
    ASSERT_NIL(vm, 4);
    ASSERT_NIL(vm, 5);
    PASS();
}

TEST(vm_globals_round_trip) {
    VmFixture f;
    P386VMState vm;
    fx_init(&f);
    fx_const(&f, P386_FP_INT(42), P386_TAG_NUM);
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 0, 0));
    fx_emit(&f, P386_ABC(P386_OP_SETGLOBAL, 0, 7, 0));
    fx_emit(&f, P386_ABC(P386_OP_LOADN, 0, 1, 0));
    fx_emit(&f, P386_ABC(P386_OP_GETGLOBAL, 3, 7, 0));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 3, 2, 0));
    ASSERT_EQ(P386_VM_HALTED, run_fixture(&f, &vm));
    ASSERT_NIL(vm, 0);
    ASSERT_NUM(vm, 3, P386_FP_INT(42));
    PASS();
}

TEST(vm_branch_false_skips_poison) {
    VmFixture f;
    P386VMState vm;
    fx_init(&f);
    fx_const(&f, P386_FP_INT(9), P386_TAG_NUM);
    fx_const(&f, P386_FP_INT(666), P386_TAG_NUM);
    fx_emit(&f, P386_ABC(P386_OP_LOADF, 0, 0, 0));
    fx_emit(&f, P386_ASBX(P386_OP_JMPF, 0, 2));
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 1, 1));
    fx_emit(&f, P386_ASBX(P386_OP_JMP, 0, 1));
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 1, 0));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 1, 2, 0));
    ASSERT_EQ(P386_VM_HALTED, run_fixture(&f, &vm));
    ASSERT_NUM(vm, 1, P386_FP_INT(9));
    PASS();
}

TEST(vm_not_and_comparisons) {
    VmFixture f;
    P386VMState vm;
    fx_init(&f);
    fx_const(&f, P386_FP_INT(4), P386_TAG_NUM);
    fx_const(&f, P386_FP_INT(5), P386_TAG_NUM);
    fx_emit(&f, P386_ABC(P386_OP_LOADN, 0, 1, 0));
    fx_emit(&f, P386_ABC(P386_OP_NOT, 1, 0, 0));
    fx_emit(&f, P386_ABC(P386_OP_LT, 2, P386_RK_CONST(0), P386_RK_CONST(1)));
    fx_emit(&f, P386_ABC(P386_OP_EQ, 3, 1, 2));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 1, 4, 0));
    ASSERT_EQ(P386_VM_HALTED, run_fixture(&f, &vm));
    ASSERT_BOOL(vm, 1, 1);
    ASSERT_BOOL(vm, 2, 1);
    ASSERT_BOOL(vm, 3, 1);
    PASS();
}

TEST(vm_type_trap_add_bool) {
    VmFixture f;
    P386VMState vm;
    fx_init(&f);
    fx_const(&f, P386_FP_INT(1), P386_TAG_NUM);
    fx_emit(&f, P386_ABC(P386_OP_LOADT, 0, 0, 0));
    fx_emit(&f, P386_ABC(P386_OP_ADD, 1, P386_RK_REG(0), P386_RK_CONST(0)));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 1, 2, 0));
    ASSERT_EQ(P386_VM_ERR_TYPE, run_fixture(&f, &vm));
    ASSERT_STR_EQ("expected number", vm.error_msg);
    PASS();
}

TEST(vm_division_by_zero_traps) {
    VmFixture f;
    P386VMState vm;
    fx_init(&f);
    fx_const(&f, P386_FP_INT(1), P386_TAG_NUM);
    fx_const(&f, 0, P386_TAG_NUM);
    fx_emit(&f, P386_ABC(P386_OP_DIV, 0, P386_RK_CONST(0), P386_RK_CONST(1)));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 2, 0));
    ASSERT_EQ(P386_VM_ERR_DIV0, run_fixture(&f, &vm));
    PASS();
}
