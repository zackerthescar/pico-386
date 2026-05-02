#include <string.h>
#include "test.h"
#include "p386_vm.h"

#define MAX_CODE 32
#define MAX_CONST 16
#define MAX_STRINGS 4
#define BUF_SIZE 1024

#define HDR_MAGIC_OFF        0
#define HDR_VERSION_OFF      4
#define HDR_TOTAL_SIZE_OFF   8
#define HDR_N_PROTOS_OFF     12
#define HDR_N_STRINGS_OFF    16
#define HDR_PROTO_TABLE_OFF  20
#define HDR_STRING_TABLE_OFF 24
#define HDR_BYTECODE_OFF     28

#define PROTO_BYTECODE_OFF_OFF 0
#define PROTO_BYTECODE_LEN_OFF 4
#define PROTO_CONSTS_OFF_OFF   8
#define PROTO_UPVALS_OFF_OFF   12
#define PROTO_N_CONSTS_OFF     16
#define PROTO_N_PARAMS_OFF     17
#define PROTO_N_REGS_OFF       18
#define PROTO_N_UPVALS_OFF     19
#define PROTO_FLAGS_OFF        20

#define STRING_DATA_OFF_OFF 0
#define STRING_LEN_OFF      4

#define HEADER_SIZE ((unsigned long)sizeof(P386BcHeader))
#define PROTO_SIZE  ((unsigned long)sizeof(P386ProtoEntry))
#define STRING_SIZE ((unsigned long)sizeof(P386StringEntry))
#define PROTO_OFF   HEADER_SIZE
#define STR_OFF     (PROTO_OFF + PROTO_SIZE)
#define BC_OFF      (STR_OFF + MAX_STRINGS * STRING_SIZE)

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
    put32(f->buf + BC_OFF + f->code_len, ins);
    f->code_len += 4;
}

static void fx_const(VmFixture *f, long value, unsigned long tag) {
    unsigned long off = BC_OFF + MAX_CODE * 4 + f->const_len;
    put32(f->buf + off, (unsigned long)value);
    put32(f->buf + off + 4, tag);
    f->const_len += 8;
    f->n_consts++;
}

static void fx_header32(VmFixture *f, unsigned long off, unsigned long v) {
    put32(f->buf + off, v);
}

static void fx_proto32(VmFixture *f, unsigned long off, unsigned long v) {
    put32(f->buf + PROTO_OFF + off, v);
}

static void fx_proto8(VmFixture *f, unsigned long off, unsigned char v) {
    f->buf[PROTO_OFF + off] = v;
}

static void fx_string32(VmFixture *f, int idx, unsigned long off, unsigned long v) {
    put32(f->buf + STR_OFF + (unsigned long)idx * STRING_SIZE + off, v);
}

static unsigned char *fx_finish(VmFixture *f, unsigned long *out_len) {
    unsigned long code_off = 0;
    unsigned long const_off = MAX_CODE * 4;
    unsigned long upval_off = const_off + f->const_len;
    unsigned long total = BC_OFF + upval_off;

    fx_header32(f, HDR_MAGIC_OFF, P386_BC_MAGIC);
    fx_header32(f, HDR_VERSION_OFF, P386_BC_VERSION);
    fx_header32(f, HDR_TOTAL_SIZE_OFF, total);
    fx_header32(f, HDR_N_PROTOS_OFF, 1);
    fx_header32(f, HDR_N_STRINGS_OFF, 0);
    fx_header32(f, HDR_PROTO_TABLE_OFF, PROTO_OFF);
    fx_header32(f, HDR_STRING_TABLE_OFF, STR_OFF);
    fx_header32(f, HDR_BYTECODE_OFF, BC_OFF);

    fx_proto32(f, PROTO_BYTECODE_OFF_OFF, code_off);
    fx_proto32(f, PROTO_BYTECODE_LEN_OFF, f->code_len);
    fx_proto32(f, PROTO_CONSTS_OFF_OFF, const_off);
    fx_proto32(f, PROTO_UPVALS_OFF_OFF, upval_off);
    fx_proto8(f, PROTO_N_CONSTS_OFF, (unsigned char)f->n_consts);
    fx_proto8(f, PROTO_N_PARAMS_OFF, 0);
    fx_proto8(f, PROTO_N_REGS_OFF, 32);
    fx_proto8(f, PROTO_N_UPVALS_OFF, 0);
    fx_proto8(f, PROTO_FLAGS_OFF, P386_PROTO_FLAG_MAIN);

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
    f.buf[HDR_MAGIC_OFF] = 'X';
    ASSERT_FALSE(p386_program_load(f.buf, len, &p));
    PASS();
}

TEST(vm_loader_rejects_bad_version) {
    VmFixture f;
    unsigned long len;
    P386LoadedProgram p;
    fx_init(&f);
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 1, 0));
    fx_finish(&f, &len);
    fx_header32(&f, HDR_VERSION_OFF, P386_BC_VERSION + 1);
    ASSERT_FALSE(p386_program_load(f.buf, len, &p));
    PASS();
}

TEST(vm_loader_rejects_proto_table_before_header_end) {
    VmFixture f;
    unsigned long len;
    P386LoadedProgram p;
    fx_init(&f);
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 1, 0));
    fx_finish(&f, &len);
    fx_header32(&f, HDR_PROTO_TABLE_OFF, HEADER_SIZE - 4);
    ASSERT_FALSE(p386_program_load(f.buf, len, &p));
    PASS();
}

TEST(vm_loader_rejects_proto_table_misaligned) {
    VmFixture f;
    unsigned long len;
    P386LoadedProgram p;
    fx_init(&f);
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 1, 0));
    fx_finish(&f, &len);
    fx_header32(&f, HDR_PROTO_TABLE_OFF, PROTO_OFF + 2);
    ASSERT_FALSE(p386_program_load(f.buf, len, &p));
    PASS();
}

TEST(vm_loader_rejects_string_table_before_proto_end) {
    VmFixture f;
    unsigned long len;
    P386LoadedProgram p;
    fx_init(&f);
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 1, 0));
    fx_finish(&f, &len);
    fx_header32(&f, HDR_STRING_TABLE_OFF, PROTO_OFF + PROTO_SIZE - 4);
    ASSERT_FALSE(p386_program_load(f.buf, len, &p));
    PASS();
}

TEST(vm_loader_rejects_string_table_misaligned) {
    VmFixture f;
    unsigned long len;
    P386LoadedProgram p;
    fx_init(&f);
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 1, 0));
    fx_finish(&f, &len);
    fx_header32(&f, HDR_STRING_TABLE_OFF, STR_OFF + 2);
    ASSERT_FALSE(p386_program_load(f.buf, len, &p));
    PASS();
}

TEST(vm_loader_rejects_bytecode_section_before_string_table_end) {
    VmFixture f;
    unsigned long len;
    P386LoadedProgram p;
    fx_init(&f);
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 1, 0));
    fx_finish(&f, &len);
    fx_header32(&f, HDR_N_STRINGS_OFF, 1);
    fx_string32(&f, 0, STRING_DATA_OFF_OFF, 0);
    fx_string32(&f, 0, STRING_LEN_OFF, 0);
    fx_header32(&f, HDR_BYTECODE_OFF, STR_OFF);
    ASSERT_FALSE(p386_program_load(f.buf, len, &p));
    PASS();
}

TEST(vm_loader_rejects_bytecode_section_misaligned) {
    VmFixture f;
    unsigned long len;
    P386LoadedProgram p;
    fx_init(&f);
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 1, 0));
    fx_finish(&f, &len);
    fx_header32(&f, HDR_BYTECODE_OFF, BC_OFF + 2);
    ASSERT_FALSE(p386_program_load(f.buf, len, &p));
    PASS();
}

TEST(vm_loader_rejects_unaligned_code_len) {
    VmFixture f;
    unsigned long len;
    P386LoadedProgram p;
    fx_init(&f);
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 1, 0));
    fx_finish(&f, &len);
    fx_proto32(&f, PROTO_BYTECODE_LEN_OFF, 6);
    ASSERT_FALSE(p386_program_load(f.buf, len, &p));
    PASS();
}

TEST(vm_loader_rejects_code_out_of_range) {
    VmFixture f;
    unsigned long len;
    P386LoadedProgram p;
    fx_init(&f);
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 1, 0));
    fx_finish(&f, &len);
    fx_proto32(&f, PROTO_BYTECODE_OFF_OFF, (unsigned long)(len - BC_OFF));
    fx_proto32(&f, PROTO_BYTECODE_LEN_OFF, 8);
    ASSERT_FALSE(p386_program_load(f.buf, len, &p));
    PASS();
}

TEST(vm_loader_rejects_consts_before_aligned_code_end) {
    VmFixture f;
    unsigned long len;
    P386LoadedProgram p;
    fx_init(&f);
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 1, 0));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 1, 0));
    fx_const(&f, P386_FP_INT(1), P386_TAG_NUM);
    fx_finish(&f, &len);
    fx_proto32(&f, PROTO_CONSTS_OFF_OFF, 4);
    ASSERT_FALSE(p386_program_load(f.buf, len, &p));
    PASS();
}

TEST(vm_loader_rejects_consts_out_of_range) {
    VmFixture f;
    unsigned long len;
    P386LoadedProgram p;
    fx_init(&f);
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 1, 0));
    fx_const(&f, P386_FP_INT(1), P386_TAG_NUM);
    fx_finish(&f, &len);
    fx_proto32(&f, PROTO_CONSTS_OFF_OFF, (unsigned long)(len - BC_OFF - 4));
    ASSERT_FALSE(p386_program_load(f.buf, len, &p));
    PASS();
}

TEST(vm_loader_rejects_upvalues_before_consts_end) {
    VmFixture f;
    unsigned long len;
    P386LoadedProgram p;
    fx_init(&f);
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 1, 0));
    fx_const(&f, P386_FP_INT(1), P386_TAG_NUM);
    fx_finish(&f, &len);
    fx_proto8(&f, PROTO_N_UPVALS_OFF, 1);
    fx_proto32(&f, PROTO_UPVALS_OFF_OFF, MAX_CODE * 4 + 6);
    ASSERT_FALSE(p386_program_load(f.buf, len, &p));
    PASS();
}

TEST(vm_loader_rejects_upvalues_out_of_range) {
    VmFixture f;
    unsigned long len;
    P386LoadedProgram p;
    fx_init(&f);
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 1, 0));
    fx_finish(&f, &len);
    fx_proto8(&f, PROTO_N_UPVALS_OFF, 4);
    fx_proto32(&f, PROTO_UPVALS_OFF_OFF, (unsigned long)(len - BC_OFF - 2));
    ASSERT_FALSE(p386_program_load(f.buf, len, &p));
    PASS();
}

TEST(vm_loader_rejects_zero_registers) {
    VmFixture f;
    unsigned long len;
    P386LoadedProgram p;
    fx_init(&f);
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 1, 0));
    fx_finish(&f, &len);
    fx_proto8(&f, PROTO_N_REGS_OFF, 0);
    ASSERT_FALSE(p386_program_load(f.buf, len, &p));
    PASS();
}

TEST(vm_loader_rejects_string_out_of_range) {
    VmFixture f;
    unsigned long len;
    P386LoadedProgram p;
    fx_init(&f);
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 1, 0));
    fx_finish(&f, &len);
    fx_header32(&f, HDR_N_STRINGS_OFF, 1);
    fx_string32(&f, 0, STRING_DATA_OFF_OFF, len - 2);
    fx_string32(&f, 0, STRING_LEN_OFF, 8);
    ASSERT_FALSE(p386_program_load(f.buf, len, &p));
    PASS();
}

TEST(vm_vm_load_sets_bad_bc_status) {
    VmFixture f;
    unsigned long len;
    P386VMState vm;
    fx_init(&f);
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 1, 0));
    fx_finish(&f, &len);
    fx_header32(&f, HDR_VERSION_OFF, P386_BC_VERSION + 99);
    ASSERT_FALSE(p386_vm_load(&vm, f.buf, len));
    ASSERT_EQ(P386_VM_ERR_BAD_BC, vm.status);
    ASSERT_STR_EQ("bad bytecode container", vm.error_msg);
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

TEST(vm_loadk_out_of_bounds_traps) {
    VmFixture f;
    P386VMState vm;
    fx_init(&f);
    fx_const(&f, P386_FP_INT(2), P386_TAG_NUM);
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 0, 1));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 2, 0));
    ASSERT_EQ(P386_VM_ERR_BOUNDS, run_fixture(&f, &vm));
    ASSERT_STR_EQ("register/constant out of bounds", vm.error_msg);
    PASS();
}

TEST(vm_rk_const_out_of_bounds_traps) {
    VmFixture f;
    P386VMState vm;
    fx_init(&f);
    fx_const(&f, P386_FP_INT(2), P386_TAG_NUM);
    fx_emit(&f, P386_ABC(P386_OP_ADD, 0, P386_RK_CONST(0), P386_RK_CONST(1)));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 2, 0));
    ASSERT_EQ(P386_VM_ERR_BOUNDS, run_fixture(&f, &vm));
    ASSERT_STR_EQ("register/constant out of bounds", vm.error_msg);
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

TEST(vm_idiv_mod_truncate) {
    VmFixture f;
    P386VMState vm;
    fx_init(&f);
    fx_const(&f, P386_FP_INT(7), P386_TAG_NUM);
    fx_const(&f, P386_FP_INT(2), P386_TAG_NUM);
    fx_emit(&f, P386_ABC(P386_OP_IDIV, 0, P386_RK_CONST(0), P386_RK_CONST(1)));
    fx_emit(&f, P386_ABC(P386_OP_MOD,  1, P386_RK_CONST(0), P386_RK_CONST(1)));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 3, 0));
    ASSERT_EQ(P386_VM_HALTED, run_fixture(&f, &vm));
    ASSERT_NUM(vm, 0, P386_FP_INT(3));
    ASSERT_NUM(vm, 1, P386_FP_INT(1));
    PASS();
}

TEST(vm_pow_integer_exponent) {
    VmFixture f;
    P386VMState vm;
    fx_init(&f);
    fx_const(&f, P386_FP_INT(3), P386_TAG_NUM);
    fx_const(&f, P386_FP_INT(4), P386_TAG_NUM);
    fx_emit(&f, P386_ABC(P386_OP_POW, 0, P386_RK_CONST(0), P386_RK_CONST(1)));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 2, 0));
    ASSERT_EQ(P386_VM_HALTED, run_fixture(&f, &vm));
    ASSERT_NUM(vm, 0, P386_FP_INT(81));
    PASS();
}

TEST(vm_bitwise_and_or_xor_not) {
    VmFixture f;
    P386VMState vm;
    fx_init(&f);
    fx_const(&f, P386_FP_INT(0x6), P386_TAG_NUM);
    fx_const(&f, P386_FP_INT(0x3), P386_TAG_NUM);
    fx_emit(&f, P386_ABC(P386_OP_BAND, 0, P386_RK_CONST(0), P386_RK_CONST(1)));
    fx_emit(&f, P386_ABC(P386_OP_BOR,  1, P386_RK_CONST(0), P386_RK_CONST(1)));
    fx_emit(&f, P386_ABC(P386_OP_BXOR, 2, P386_RK_CONST(0), P386_RK_CONST(1)));
    fx_emit(&f, P386_ABC(P386_OP_BNOT, 3, P386_RK_CONST(0), 0));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 5, 0));
    ASSERT_EQ(P386_VM_HALTED, run_fixture(&f, &vm));
    ASSERT_NUM(vm, 0, P386_FP_INT(2));
    ASSERT_NUM(vm, 1, P386_FP_INT(7));
    ASSERT_NUM(vm, 2, P386_FP_INT(5));
    ASSERT_NUM(vm, 3, P386_FP_INT(~6));
    PASS();
}

TEST(vm_shifts_and_rotates) {
    VmFixture f;
    P386VMState vm;
    fx_init(&f);
    fx_const(&f, P386_FP_INT(0x10), P386_TAG_NUM);
    fx_const(&f, P386_FP_INT(2), P386_TAG_NUM);
    fx_const(&f, P386_FP_INT(-8), P386_TAG_NUM);
    fx_emit(&f, P386_ABC(P386_OP_SHL,  0, P386_RK_CONST(0), P386_RK_CONST(1)));
    fx_emit(&f, P386_ABC(P386_OP_SHR,  1, P386_RK_CONST(2), P386_RK_CONST(1)));
    fx_emit(&f, P386_ABC(P386_OP_LSHR, 2, P386_RK_CONST(0), P386_RK_CONST(1)));
    fx_emit(&f, P386_ABC(P386_OP_ROTL, 3, P386_RK_CONST(0), P386_RK_CONST(1)));
    fx_emit(&f, P386_ABC(P386_OP_ROTR, 4, P386_RK_CONST(0), P386_RK_CONST(1)));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 6, 0));
    ASSERT_EQ(P386_VM_HALTED, run_fixture(&f, &vm));
    ASSERT_NUM(vm, 0, P386_FP_INT(0x40));
    ASSERT_NUM(vm, 1, P386_FP_INT(-2));
    ASSERT_NUM(vm, 2, P386_FP_INT(0x4));
    ASSERT_NUM(vm, 3, P386_FP_INT(0x40));
    ASSERT_NUM(vm, 4, P386_FP_INT(0x4));
    PASS();
}

TEST(vm_idiv_div_zero_traps) {
    VmFixture f;
    P386VMState vm;
    fx_init(&f);
    fx_const(&f, P386_FP_INT(5), P386_TAG_NUM);
    fx_const(&f, 0, P386_TAG_NUM);
    fx_emit(&f, P386_ABC(P386_OP_MOD, 0, P386_RK_CONST(0), P386_RK_CONST(1)));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 2, 0));
    ASSERT_EQ(P386_VM_ERR_DIV0, run_fixture(&f, &vm));
    PASS();
}
