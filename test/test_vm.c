#include <string.h>
#include "test.h"
#include "p386_vm.h"
#include "p386_builtins.h"
#include "mem.h"
#include "p386_obj.h"

P8Ram p8_ram;

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
    unsigned long string_data_len;
    int n_consts;
    int n_strings;
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

static int fx_string(VmFixture *f, const char *s) {
    unsigned long len = (unsigned long)strlen(s);
    unsigned long table_off = STR_OFF + (unsigned long)f->n_strings * STRING_SIZE;
    unsigned long data_off = BC_OFF + MAX_CODE * 4 + MAX_CONST * 8 + f->string_data_len;
    int idx = f->n_strings;
    put32(f->buf + table_off, data_off);
    put32(f->buf + table_off + 4, len);
    memcpy(f->buf + data_off, s, len);
    f->string_data_len += len;
    f->n_strings++;
    return idx;
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
    unsigned long strings_end = BC_OFF + MAX_CODE * 4 + MAX_CONST * 8 + f->string_data_len;
    unsigned long total = BC_OFF + upval_off;
    if (total < strings_end) total = strings_end;

    fx_header32(f, HDR_MAGIC_OFF, P386_BC_MAGIC);
    fx_header32(f, HDR_VERSION_OFF, P386_BC_VERSION);
    fx_header32(f, HDR_TOTAL_SIZE_OFF, total);
    fx_header32(f, HDR_N_PROTOS_OFF, 1);
    fx_header32(f, HDR_N_STRINGS_OFF, (unsigned long)f->n_strings);
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


static unsigned char *fx_finish_two_proto(VmFixture *f, unsigned long *out_len,
                                          const unsigned long *main_code, int main_n_code,
                                          const P386Value *main_consts, int main_n_consts,
                                          unsigned char main_n_regs,
                                          const unsigned long *child_code, int child_n_code,
                                          const P386Value *child_consts, int child_n_consts,
                                          unsigned char child_n_params,
                                          unsigned char child_n_regs) {
    unsigned long proto_table_size = 2UL * PROTO_SIZE;
    unsigned long str_off = PROTO_OFF + proto_table_size;
    unsigned long bc_off = str_off;
    unsigned long off = 0;
    unsigned long main_code_off = 0;
    unsigned long main_code_len = (unsigned long)main_n_code * 4UL;
    unsigned long main_consts_off;
    unsigned long main_upvals_off;
    unsigned long child_code_off;
    unsigned long child_code_len = (unsigned long)child_n_code * 4UL;
    unsigned long child_consts_off;
    unsigned long child_upvals_off;
    unsigned long total;
    int i;

    memset(f, 0, sizeof(*f));

    for (i = 0; i < main_n_code; i++) {
        put32(f->buf + bc_off + off, main_code[i]);
        off += 4;
    }
    main_consts_off = off;
    for (i = 0; i < main_n_consts; i++) {
        put32(f->buf + bc_off + off, (unsigned long)main_consts[i].value);
        put32(f->buf + bc_off + off + 4, main_consts[i].tag);
        off += 8;
    }
    main_upvals_off = off;

    child_code_off = off;
    for (i = 0; i < child_n_code; i++) {
        put32(f->buf + bc_off + off, child_code[i]);
        off += 4;
    }
    child_consts_off = off;
    for (i = 0; i < child_n_consts; i++) {
        put32(f->buf + bc_off + off, (unsigned long)child_consts[i].value);
        put32(f->buf + bc_off + off + 4, child_consts[i].tag);
        off += 8;
    }
    child_upvals_off = off;
    total = bc_off + off;

    fx_header32(f, HDR_MAGIC_OFF, P386_BC_MAGIC);
    fx_header32(f, HDR_VERSION_OFF, P386_BC_VERSION);
    fx_header32(f, HDR_TOTAL_SIZE_OFF, total);
    fx_header32(f, HDR_N_PROTOS_OFF, 2);
    fx_header32(f, HDR_N_STRINGS_OFF, 0);
    fx_header32(f, HDR_PROTO_TABLE_OFF, PROTO_OFF);
    fx_header32(f, HDR_STRING_TABLE_OFF, str_off);
    fx_header32(f, HDR_BYTECODE_OFF, bc_off);

    put32(f->buf + PROTO_OFF + 0 * PROTO_SIZE + PROTO_BYTECODE_OFF_OFF, main_code_off);
    put32(f->buf + PROTO_OFF + 0 * PROTO_SIZE + PROTO_BYTECODE_LEN_OFF, main_code_len);
    put32(f->buf + PROTO_OFF + 0 * PROTO_SIZE + PROTO_CONSTS_OFF_OFF, main_consts_off);
    put32(f->buf + PROTO_OFF + 0 * PROTO_SIZE + PROTO_UPVALS_OFF_OFF, main_upvals_off);
    f->buf[PROTO_OFF + 0 * PROTO_SIZE + PROTO_N_CONSTS_OFF] = (unsigned char)main_n_consts;
    f->buf[PROTO_OFF + 0 * PROTO_SIZE + PROTO_N_PARAMS_OFF] = 0;
    f->buf[PROTO_OFF + 0 * PROTO_SIZE + PROTO_N_REGS_OFF] = main_n_regs;
    f->buf[PROTO_OFF + 0 * PROTO_SIZE + PROTO_N_UPVALS_OFF] = 0;
    f->buf[PROTO_OFF + 0 * PROTO_SIZE + PROTO_FLAGS_OFF] = P386_PROTO_FLAG_MAIN;

    put32(f->buf + PROTO_OFF + 1 * PROTO_SIZE + PROTO_BYTECODE_OFF_OFF, child_code_off);
    put32(f->buf + PROTO_OFF + 1 * PROTO_SIZE + PROTO_BYTECODE_LEN_OFF, child_code_len);
    put32(f->buf + PROTO_OFF + 1 * PROTO_SIZE + PROTO_CONSTS_OFF_OFF, child_consts_off);
    put32(f->buf + PROTO_OFF + 1 * PROTO_SIZE + PROTO_UPVALS_OFF_OFF, child_upvals_off);
    f->buf[PROTO_OFF + 1 * PROTO_SIZE + PROTO_N_CONSTS_OFF] = (unsigned char)child_n_consts;
    f->buf[PROTO_OFF + 1 * PROTO_SIZE + PROTO_N_PARAMS_OFF] = child_n_params;
    f->buf[PROTO_OFF + 1 * PROTO_SIZE + PROTO_N_REGS_OFF] = child_n_regs;
    f->buf[PROTO_OFF + 1 * PROTO_SIZE + PROTO_N_UPVALS_OFF] = 0;
    f->buf[PROTO_OFF + 1 * PROTO_SIZE + PROTO_FLAGS_OFF] = 0;

    *out_len = total;
    return f->buf;
}

static int run_two_proto_fixture(VmFixture *f, P386VMState *vm,
                                 const unsigned long *main_code, int main_n_code,
                                 const P386Value *main_consts, int main_n_consts,
                                 unsigned char main_n_regs,
                                 const unsigned long *child_code, int child_n_code,
                                 const P386Value *child_consts, int child_n_consts,
                                 unsigned char child_n_params,
                                 unsigned char child_n_regs) {
    unsigned long len;
    unsigned char *buf = fx_finish_two_proto(f, &len, main_code, main_n_code, main_consts, main_n_consts, main_n_regs,
                                             child_code, child_n_code, child_consts, child_n_consts,
                                             child_n_params, child_n_regs);
    if (!p386_vm_load(vm, buf, len)) return vm->status;
    return p386_vm_run(vm);
}

/* Hand-built two-proto buffer with an upvalue descriptor on the child proto.
 * main: LOADK R0=K0; CLOSURE R1=child(captures parent local 0); CALL R1();
 *       MOVE R0,R1; RETURN R0.
 * child: GETUPVAL R0,0; RETURN R0 (b=2).  upvalue[0] = (source=0, index=0). */
static int run_upvalue_capture_fixture(VmFixture *f, P386VMState *vm,
                                       unsigned long *out_result) {
    unsigned long bc_off = PROTO_OFF + 2UL * PROTO_SIZE;
    unsigned long off = 0;
    unsigned long main_code_off, main_consts_off, main_upvals_off;
    unsigned long child_code_off, child_consts_off, child_upvals_off;
    unsigned long total, len;
    memset(f, 0, sizeof(*f));

    main_code_off = off;
    put32(f->buf + bc_off + off, P386_ABX(P386_OP_LOADK, 0, 0)); off += 4;
    put32(f->buf + bc_off + off, P386_ABX(P386_OP_CLOSURE, 1, 1)); off += 4;
    put32(f->buf + bc_off + off, P386_ABC(P386_OP_CALL, 1, 1, 2)); off += 4;
    put32(f->buf + bc_off + off, P386_ABC(P386_OP_MOVE, 0, 1, 0)); off += 4;
    put32(f->buf + bc_off + off, P386_ABC(P386_OP_RETURN, 0, 2, 0)); off += 4;
    main_consts_off = off;
    put32(f->buf + bc_off + off, (unsigned long)P386_FP_INT(7)); off += 4;
    put32(f->buf + bc_off + off, P386_TAG_NUM); off += 4;
    main_upvals_off = off;  /* main has no upvalues */

    child_code_off = off;
    put32(f->buf + bc_off + off, P386_ABC(P386_OP_GETUPVAL, 0, 0, 0)); off += 4;
    put32(f->buf + bc_off + off, P386_ABC(P386_OP_RETURN, 0, 2, 0)); off += 4;
    child_consts_off = off;
    child_upvals_off = off;
    f->buf[bc_off + off] = 0; /* source: parent local */
    f->buf[bc_off + off + 1] = 0; /* index 0 */
    off += 2;
    while (off & 3) off++;
    total = bc_off + off;

    fx_header32(f, HDR_MAGIC_OFF, P386_BC_MAGIC);
    fx_header32(f, HDR_VERSION_OFF, P386_BC_VERSION);
    fx_header32(f, HDR_TOTAL_SIZE_OFF, total);
    fx_header32(f, HDR_N_PROTOS_OFF, 2);
    fx_header32(f, HDR_N_STRINGS_OFF, 0);
    fx_header32(f, HDR_PROTO_TABLE_OFF, PROTO_OFF);
    fx_header32(f, HDR_STRING_TABLE_OFF, PROTO_OFF + 2UL * PROTO_SIZE);
    fx_header32(f, HDR_BYTECODE_OFF, bc_off);

    put32(f->buf + PROTO_OFF + PROTO_BYTECODE_OFF_OFF, main_code_off);
    put32(f->buf + PROTO_OFF + PROTO_BYTECODE_LEN_OFF, main_consts_off - main_code_off);
    put32(f->buf + PROTO_OFF + PROTO_CONSTS_OFF_OFF, main_consts_off);
    put32(f->buf + PROTO_OFF + PROTO_UPVALS_OFF_OFF, main_upvals_off);
    f->buf[PROTO_OFF + PROTO_N_CONSTS_OFF] = 1;
    f->buf[PROTO_OFF + PROTO_N_PARAMS_OFF] = 0;
    f->buf[PROTO_OFF + PROTO_N_REGS_OFF] = 4;
    f->buf[PROTO_OFF + PROTO_N_UPVALS_OFF] = 0;
    f->buf[PROTO_OFF + PROTO_FLAGS_OFF] = P386_PROTO_FLAG_MAIN;

    put32(f->buf + PROTO_OFF + PROTO_SIZE + PROTO_BYTECODE_OFF_OFF, child_code_off);
    put32(f->buf + PROTO_OFF + PROTO_SIZE + PROTO_BYTECODE_LEN_OFF, child_consts_off - child_code_off);
    put32(f->buf + PROTO_OFF + PROTO_SIZE + PROTO_CONSTS_OFF_OFF, child_consts_off);
    put32(f->buf + PROTO_OFF + PROTO_SIZE + PROTO_UPVALS_OFF_OFF, child_upvals_off);
    f->buf[PROTO_OFF + PROTO_SIZE + PROTO_N_CONSTS_OFF] = 0;
    f->buf[PROTO_OFF + PROTO_SIZE + PROTO_N_PARAMS_OFF] = 0;
    f->buf[PROTO_OFF + PROTO_SIZE + PROTO_N_REGS_OFF] = 1;
    f->buf[PROTO_OFF + PROTO_SIZE + PROTO_N_UPVALS_OFF] = 1;
    f->buf[PROTO_OFF + PROTO_SIZE + PROTO_FLAGS_OFF] = 0;

    if (!p386_vm_load(vm, f->buf, total)) return vm->status;
    (void)len;
    {
        int rc = p386_vm_run(vm);
        *out_result = (unsigned long)vm->value_stack[0].value;
        return rc;
    }
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
    fx_emit(&f, P386_ABC(P386_OP_SETGLOBAL, 0, 99, 0));
    fx_emit(&f, P386_ABC(P386_OP_LOADN, 0, 1, 0));
    fx_emit(&f, P386_ABC(P386_OP_GETGLOBAL, 3, 99, 0));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 3, 2, 0));
    ASSERT_EQ(P386_VM_HALTED, run_fixture(&f, &vm));
    ASSERT_NIL(vm, 0);
    ASSERT_NUM(vm, 3, P386_FP_INT(42));
    PASS();
}

TEST(vm_init_registers_cfunc_builtins) {
    P386VMState vm;
    p386_vm_init(&vm);
    ASSERT_EQ(P386_TAG_CFUNC, vm.globals[P386_BUILTIN_PRINT].tag);
    ASSERT_EQ((int32_t)(uintptr_t)p386_builtin_print,
              vm.globals[P386_BUILTIN_PRINT].value);
    ASSERT_EQ(P386_TAG_CFUNC, vm.globals[P386_BUILTIN_CLS].tag);
    ASSERT_EQ((int32_t)(uintptr_t)p386_builtin_cls,
              vm.globals[P386_BUILTIN_CLS].value);
    ASSERT_EQ(P386_TAG_CFUNC, vm.globals[P386_BUILTIN_PSET].tag);
    ASSERT_EQ((int32_t)(uintptr_t)p386_builtin_pset,
              vm.globals[P386_BUILTIN_PSET].value);
    ASSERT_EQ(P386_TAG_CFUNC, vm.globals[P386_BUILTIN_PGET].tag);
    ASSERT_EQ((int32_t)(uintptr_t)p386_builtin_pget,
              vm.globals[P386_BUILTIN_PGET].value);
    ASSERT_EQ(P386_TAG_CFUNC, vm.globals[P386_BUILTIN_PAIRS].tag);
    ASSERT_EQ((int32_t)(uintptr_t)p386_builtin_pairs,
              vm.globals[P386_BUILTIN_PAIRS].value);
    /* Lua-prelude builtins get no CFUNC; their slots stay nil until the
     * compiled prelude assigns them. */
    ASSERT_EQ(P386_TAG_NIL, vm.globals[P386_BUILTIN_IPAIRS].tag);
    ASSERT_EQ(P386_TAG_NIL, vm.globals[P386_BUILTIN_ALL].tag);
    ASSERT_EQ(P386_TAG_NIL, vm.globals[P386_BUILTIN_FOREACH].tag);
    ASSERT_EQ(P386_TAG_NIL, vm.globals[P386_GLOBAL_INIT].tag);
    PASS();
}

TEST(vm_load_preserves_registered_cfunc_builtins) {
    VmFixture f;
    P386VMState vm;
    fx_init(&f);
    fx_emit(&f, P386_ABC(P386_OP_GETGLOBAL, 0, P386_BUILTIN_PRINT, 0));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 2, 0));
    ASSERT_EQ(P386_VM_HALTED, run_fixture(&f, &vm));
    ASSERT_EQ(P386_TAG_CFUNC, vm.value_stack[0].tag);
    ASSERT_EQ((int32_t)(uintptr_t)p386_builtin_print, vm.value_stack[0].value);
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

TEST(vm_jmp_sbx_zero_is_noop) {
    VmFixture f;
    P386VMState vm;
    fx_init(&f);
    fx_const(&f, P386_FP_INT(7), P386_TAG_NUM);
    fx_emit(&f, P386_ASBX(P386_OP_JMP, 0, 0));
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 0, 0));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 2, 0));
    ASSERT_EQ(P386_VM_HALTED, run_fixture(&f, &vm));
    ASSERT_NUM(vm, 0, P386_FP_INT(7));
    PASS();
}

TEST(vm_jmpt_sbx_negative_edge_takes_backedge) {
    VmFixture f;
    P386VMState vm;
    fx_init(&f);
    fx_const(&f, 0, P386_TAG_NUM);
    fx_const(&f, P386_FP_INT(1), P386_TAG_NUM);
    fx_const(&f, P386_FP_INT(3), P386_TAG_NUM);
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 0, 0));
    fx_emit(&f, P386_ABC(P386_OP_ADD, 0, P386_RK_REG(0), P386_RK_CONST(1)));
    fx_emit(&f, P386_ABC(P386_OP_LT, 1, P386_RK_REG(0), P386_RK_CONST(2)));
    fx_emit(&f, P386_ASBX(P386_OP_JMPT, 1, -3));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 2, 0));
    ASSERT_EQ(P386_VM_HALTED, run_fixture(&f, &vm));
    ASSERT_NUM(vm, 0, P386_FP_INT(3));
    PASS();
}

TEST(vm_jmpf_sbx_positive_edge_skips_unimplemented) {
    VmFixture f;
    P386VMState vm;
    fx_init(&f);
    fx_const(&f, P386_FP_INT(11), P386_TAG_NUM);
    fx_emit(&f, P386_ABC(P386_OP_LOADN, 0, 1, 0));
    fx_emit(&f, P386_ASBX(P386_OP_JMPF, 0, 2));
    fx_emit(&f, P386_ABC(P386_OP_TFORCALL, 0, 0, 0));
    fx_emit(&f, P386_ASBX(P386_OP_JMP, 0, 1));
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 1, 0));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 1, 2, 0));
    ASSERT_EQ(P386_VM_HALTED, run_fixture(&f, &vm));
    ASSERT_NUM(vm, 1, P386_FP_INT(11));
    PASS();
}

TEST(vm_forloop_positive_step_accumulates_sum) {
    VmFixture f;
    P386VMState vm;
    fx_init(&f);
    fx_const(&f, P386_FP_INT(1), P386_TAG_NUM);
    fx_const(&f, P386_FP_INT(3), P386_TAG_NUM);
    fx_const(&f, 0, P386_TAG_NUM);
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 0, 0));
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 1, 1));
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 2, 0));
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 4, 2));
    fx_emit(&f, P386_ASBX(P386_OP_FORPREP, 0, 1));
    fx_emit(&f, P386_ABC(P386_OP_ADD, 4, P386_RK_REG(4), P386_RK_REG(3)));
    fx_emit(&f, P386_ASBX(P386_OP_FORLOOP, 0, -2));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 4, 2, 0));
    ASSERT_EQ(P386_VM_HALTED, run_fixture(&f, &vm));
    ASSERT_NUM(vm, 0, P386_FP_INT(4));
    ASSERT_NUM(vm, 3, P386_FP_INT(3));
    ASSERT_NUM(vm, 4, P386_FP_INT(6));
    PASS();
}

TEST(vm_forloop_negative_step_accumulates_sum) {
    VmFixture f;
    P386VMState vm;
    fx_init(&f);
    fx_const(&f, P386_FP_INT(3), P386_TAG_NUM);
    fx_const(&f, P386_FP_INT(1), P386_TAG_NUM);
    fx_const(&f, -P386_FP_INT(1), P386_TAG_NUM);
    fx_const(&f, 0, P386_TAG_NUM);
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 0, 0));
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 1, 1));
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 2, 2));
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 4, 3));
    fx_emit(&f, P386_ASBX(P386_OP_FORPREP, 0, 1));
    fx_emit(&f, P386_ABC(P386_OP_ADD, 4, P386_RK_REG(4), P386_RK_REG(3)));
    fx_emit(&f, P386_ASBX(P386_OP_FORLOOP, 0, -2));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 4, 2, 0));
    ASSERT_EQ(P386_VM_HALTED, run_fixture(&f, &vm));
    ASSERT_NUM(vm, 0, 0);
    ASSERT_NUM(vm, 3, P386_FP_INT(1));
    ASSERT_NUM(vm, 4, P386_FP_INT(6));
    PASS();
}

TEST(vm_forprep_requires_numeric_regs) {
    VmFixture f;
    P386VMState vm;
    fx_init(&f);
    fx_const(&f, P386_FP_INT(1), P386_TAG_NUM);
    fx_emit(&f, P386_ABC(P386_OP_LOADT, 0, 0, 0));
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 1, 0));
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 2, 0));
    fx_emit(&f, P386_ASBX(P386_OP_FORPREP, 0, 0));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 1, 0));
    ASSERT_EQ(P386_VM_ERR_TYPE, run_fixture(&f, &vm));
    ASSERT_STR_EQ("expected number", vm.error_msg);
    PASS();
}

TEST(vm_tfor_table_iterates_key_value_pairs) {
    VmFixture f;
    P386VMState vm;
    fx_init(&f);
    fx_const(&f, P386_FP_INT(1), P386_TAG_NUM);
    fx_const(&f, P386_FP_INT(2), P386_TAG_NUM);
    fx_const(&f, P386_FP_INT(10), P386_TAG_NUM);
    fx_const(&f, P386_FP_INT(20), P386_TAG_NUM);
    fx_const(&f, 0, P386_TAG_NUM);
    fx_emit(&f, P386_ABC(P386_OP_NEWTABLE, 0, 0, 0));
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 6, 0));
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 7, 2));
    fx_emit(&f, P386_ABC(P386_OP_SETTABLE, 0, P386_RK_REG(6), P386_RK_REG(7)));
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 6, 1));
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 7, 3));
    fx_emit(&f, P386_ABC(P386_OP_SETTABLE, 0, P386_RK_REG(6), P386_RK_REG(7)));
    fx_emit(&f, P386_ABC(P386_OP_LOADN, 1, 1, 0));  /* iterator: nil table-next sentinel */
    fx_emit(&f, P386_ABC(P386_OP_MOVE, 2, 0, 0));   /* state: table */
    fx_emit(&f, P386_ABC(P386_OP_LOADN, 3, 1, 0));  /* control: nil */
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 8, 4));     /* sum = 0 */
    fx_emit(&f, P386_ABC(P386_OP_TFORCALL, 1, 0, 2));
    fx_emit(&f, P386_ASBX(P386_OP_TFORLOOP, 1, 1));
    fx_emit(&f, P386_ASBX(P386_OP_JMP, 0, 2));
    fx_emit(&f, P386_ABC(P386_OP_ADD, 8, P386_RK_REG(8), P386_RK_REG(5)));
    fx_emit(&f, P386_ASBX(P386_OP_JMP, 0, -5));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 8, 2, 0));
    ASSERT_EQ(P386_VM_HALTED, run_fixture(&f, &vm));
    ASSERT_NUM(vm, 3, P386_FP_INT(2));
    ASSERT_NIL(vm, 4);
    ASSERT_NIL(vm, 5);
    ASSERT_NUM(vm, 8, P386_FP_INT(30));
    PASS();
}

TEST(vm_tfor_ends_immediately_on_empty_table) {
    VmFixture f;
    P386VMState vm;
    fx_init(&f);
    fx_const(&f, P386_FP_INT(123), P386_TAG_NUM);
    fx_emit(&f, P386_ABC(P386_OP_NEWTABLE, 0, 0, 0));
    fx_emit(&f, P386_ABC(P386_OP_LOADN, 1, 1, 0));
    fx_emit(&f, P386_ABC(P386_OP_MOVE, 2, 0, 0));
    fx_emit(&f, P386_ABC(P386_OP_LOADN, 3, 1, 0));
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 6, 0));
    fx_emit(&f, P386_ABC(P386_OP_TFORCALL, 1, 0, 2));
    fx_emit(&f, P386_ASBX(P386_OP_TFORLOOP, 1, 1));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 6, 2, 0));
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 6, 0));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 6, 2, 0));
    ASSERT_EQ(P386_VM_HALTED, run_fixture(&f, &vm));
    ASSERT_NIL(vm, 4);
    ASSERT_NIL(vm, 5);
    ASSERT_NUM(vm, 6, P386_FP_INT(123));
    PASS();
}

TEST(vm_tfor_requires_table_state) {
    VmFixture f;
    P386VMState vm;
    fx_init(&f);
    fx_emit(&f, P386_ABC(P386_OP_LOADN, 0, 3, 0));
    fx_emit(&f, P386_ABC(P386_OP_TFORCALL, 0, 0, 2));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 1, 0));
    ASSERT_EQ(P386_VM_ERR_TYPE, run_fixture(&f, &vm));
    ASSERT_STR_EQ("expected table iterator state", vm.error_msg);
    PASS();
}

TEST(vm_len_string_constant_returns_fixed_point_length) {
    VmFixture f;
    P386VMState vm;
    int sid;
    fx_init(&f);
    sid = fx_string(&f, "pico386");
    fx_const(&f, sid, P386_TAG_STR);
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 0, 0));
    fx_emit(&f, P386_ABC(P386_OP_LEN, 1, 0, 0));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 1, 2, 0));
    ASSERT_EQ(P386_VM_HALTED, run_fixture(&f, &vm));
    ASSERT_NUM(vm, 1, P386_FP_INT(7));
    PASS();
}

TEST(vm_peek_reads_p8_ram_as_fixed_point_number) {
    VmFixture f;
    P386VMState vm;
    memset(&p8_ram, 0, sizeof(p8_ram));
    p8_ram.raw[0x4300] = 0xab;
    fx_init(&f);
    fx_const(&f, P386_FP_INT(0x4300), P386_TAG_NUM);
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 0, 0));
    fx_emit(&f, P386_ABC(P386_OP_PEEK, 1, 0, 0));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 1, 2, 0));
    ASSERT_EQ(P386_VM_HALTED, run_fixture(&f, &vm));
    ASSERT_NUM(vm, 1, P386_FP_INT(0xab));
    PASS();
}

TEST(vm_peek2_reads_little_endian_and_wraps_64k) {
    VmFixture f;
    P386VMState vm;
    memset(&p8_ram, 0, sizeof(p8_ram));
    p8_ram.raw[0xffff] = 0x34;
    p8_ram.raw[0x0000] = 0x12;
    fx_init(&f);
    fx_const(&f, P386_FP_INT(0xffff), P386_TAG_NUM);
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 0, 0));
    fx_emit(&f, P386_ABC(P386_OP_PEEK2, 1, 0, 0));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 1, 2, 0));
    ASSERT_EQ(P386_VM_HALTED, run_fixture(&f, &vm));
    ASSERT_NUM(vm, 1, P386_FP_INT(0x1234));
    PASS();
}

TEST(vm_peek_requires_numeric_address) {
    VmFixture f;
    P386VMState vm;
    fx_init(&f);
    fx_emit(&f, P386_ABC(P386_OP_LOADT, 0, 0, 0));
    fx_emit(&f, P386_ABC(P386_OP_PEEK, 1, 0, 0));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 1, 2, 0));
    ASSERT_EQ(P386_VM_ERR_TYPE, run_fixture(&f, &vm));
    ASSERT_STR_EQ("expected number", vm.error_msg);
    PASS();
}

TEST(vm_loader_rejects_unknown_const_tag) {
    VmFixture f;
    unsigned long len;
    P386LoadedProgram p;
    fx_init(&f);
    fx_const(&f, 0, 99); /* unknown tag */
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 1, 0));
    fx_finish(&f, &len);
    ASSERT_FALSE(p386_program_load(f.buf, len, &p));
    PASS();
}

TEST(vm_loader_rejects_str_const_index_out_of_range) {
    VmFixture f;
    unsigned long len;
    P386LoadedProgram p;
    fx_init(&f);
    /* STR const referring to nonexistent string idx 5; n_strings stays 0 */
    fx_const(&f, 5, P386_TAG_STR);
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 1, 0));
    fx_finish(&f, &len);
    ASSERT_FALSE(p386_program_load(f.buf, len, &p));
    PASS();
}

TEST(vm_forloop_zero_step_traps) {
    VmFixture f;
    P386VMState vm;
    fx_init(&f);
    fx_const(&f, P386_FP_INT(1), P386_TAG_NUM);
    fx_const(&f, P386_FP_INT(3), P386_TAG_NUM);
    fx_const(&f, 0, P386_TAG_NUM);
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 0, 0));
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 1, 1));
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 2, 2)); /* step = 0 */
    fx_emit(&f, P386_ASBX(P386_OP_FORPREP, 0, 0));
    fx_emit(&f, P386_ASBX(P386_OP_FORLOOP, 0, -1));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 1, 0));
    ASSERT_EQ(P386_VM_ERR_TYPE, run_fixture(&f, &vm));
    ASSERT_STR_EQ("for loop step must be non-zero", vm.error_msg);
    PASS();
}

TEST(vm_setfield_getfield_round_trip_string_key) {
    VmFixture f;
    P386VMState vm;
    int sid;
    fx_init(&f);
    sid = fx_string(&f, "hp");
    fx_const(&f, sid, P386_TAG_STR);
    fx_const(&f, P386_FP_INT(42), P386_TAG_NUM);
    fx_emit(&f, P386_ABC(P386_OP_NEWTABLE, 0, 0, 0));
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 1, 1)); /* R[1] = 42 */
    fx_emit(&f, P386_ABC(P386_OP_SETFIELD, 0, 0, 1)); /* t["hp"] = R[1] */
    fx_emit(&f, P386_ABC(P386_OP_GETFIELD, 2, 0, 0)); /* R[2] = t["hp"] */
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 2, 2, 0));
    ASSERT_EQ(P386_VM_HALTED, run_fixture(&f, &vm));
    ASSERT_NUM(vm, 2, P386_FP_INT(42));
    PASS();
}

TEST(vm_getfield_requires_string_const) {
    VmFixture f;
    P386VMState vm;
    fx_init(&f);
    fx_const(&f, P386_FP_INT(1), P386_TAG_NUM); /* not a string */
    fx_emit(&f, P386_ABC(P386_OP_NEWTABLE, 0, 0, 0));
    fx_emit(&f, P386_ABC(P386_OP_GETFIELD, 1, 0, 0));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 1, 2, 0));
    ASSERT_EQ(P386_VM_ERR_TYPE, run_fixture(&f, &vm));
    ASSERT_STR_EQ("expected string or number", vm.error_msg);
    PASS();
}

TEST(vm_call_cfunc_noop_pads_requested_results) {
    VmFixture f;
    P386VMState vm;
    fx_init(&f);
    fx_const(&f, P386_FP_INT(77), P386_TAG_NUM);
    fx_emit(&f, P386_ABC(P386_OP_GETGLOBAL, 0, P386_BUILTIN_CLS, 0));
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 1, 0));
    fx_emit(&f, P386_ABC(P386_OP_CALL, 0, 2, 3)); /* cls(77), want two results */
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 3, 0));
    ASSERT_EQ(P386_VM_HALTED, run_fixture(&f, &vm));
    ASSERT_NIL(vm, 0);
    ASSERT_NIL(vm, 1);
    PASS();
}

TEST(vm_call_cfunc_pairs_returns_iterator_state_nil) {
    VmFixture f;
    P386VMState vm;
    fx_init(&f);
    fx_emit(&f, P386_ABC(P386_OP_NEWTABLE, 0, 0, 0));
    fx_emit(&f, P386_ABC(P386_OP_GETGLOBAL, 1, P386_BUILTIN_PAIRS, 0));
    fx_emit(&f, P386_ABC(P386_OP_MOVE, 2, 0, 0));
    fx_emit(&f, P386_ABC(P386_OP_CALL, 1, 2, 4)); /* pairs(t), want 3 */
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 1, 4, 0));
    ASSERT_EQ(P386_VM_HALTED, run_fixture(&f, &vm));
    ASSERT_EQ(P386_TAG_CFUNC, vm.value_stack[1].tag);
    ASSERT_EQ((int32_t)(uintptr_t)p386_builtin_pairs, vm.value_stack[1].value);
    ASSERT_EQ(P386_TAG_TAB, vm.value_stack[2].tag);
    ASSERT_EQ(vm.value_stack[0].value, vm.value_stack[2].value);
    ASSERT_NIL(vm, 3);
    PASS();
}



TEST(vm_builtin_cls_pset_pget_mutate_framebuffer) {
    P386VMState vm;
    P386Value args[3];
    p8_ram_init();
    p386_vm_init(&vm);

    args[0].value = P386_FP_INT(2);
    args[0].tag = P386_TAG_NUM;
    ASSERT_EQ(0, p386_builtin_cls(&vm, args, 1, 0));
    ASSERT_EQ(0x22, p8_ram.mem.screen[0]);

    args[0].value = P386_FP_INT(3);
    args[0].tag = P386_TAG_NUM;
    args[1].value = P386_FP_INT(1);
    args[1].tag = P386_TAG_NUM;
    args[2].value = P386_FP_INT(5);
    args[2].tag = P386_TAG_NUM;
    ASSERT_EQ(0, p386_builtin_pset(&vm, args, 3, 0));
    ASSERT_EQ(0x52, p8_ram.mem.screen[1 * 64 + 1]);

    args[0].value = P386_FP_INT(3);
    args[0].tag = P386_TAG_NUM;
    args[1].value = P386_FP_INT(1);
    args[1].tag = P386_TAG_NUM;
    ASSERT_EQ(1, p386_builtin_pget(&vm, args, 2, 1));
    ASSERT_EQ(P386_TAG_NUM, args[0].tag);
    ASSERT_EQ(P386_FP_INT(5), args[0].value);
    PASS();
}

TEST(vm_call_global_missing_is_noop) {
    P386VMState vm;
    p386_vm_init(&vm);
    ASSERT_EQ(P386_VM_HALTED, p386_vm_call_global(&vm, P386_GLOBAL_DRAW, 0, 0));
    PASS();
}
TEST(vm_closure_creates_func_value) {
    VmFixture f;
    P386VMState vm;
    unsigned long main_code[] = {
        P386_ABX(P386_OP_CLOSURE, 0, 1),
        P386_ABC(P386_OP_RETURN, 0, 2, 0)
    };
    unsigned long child_code[] = {
        P386_ABC(P386_OP_RETURN, 0, 1, 0)
    };

    ASSERT_EQ(P386_VM_HALTED, run_two_proto_fixture(&f, &vm,
        main_code, 2, 0, 0, 4,
        child_code, 1, 0, 0, 0, 1));
    ASSERT_TAG(vm, 0, P386_TAG_FUNC);
    ASSERT_NEQ(0, vm.value_stack[0].value);
    ASSERT_EQ(0, vm.call_depth);
    PASS();
}

TEST(vm_upvalue_runtime_smoke) {
    /* Directly exercise the C closure/upvalue helpers to localize failures. */
    P386VMState vm;
    P386Upvalue *head = 0;
    P386Upvalue *uv;
    P386Closure *c;
    P386Value slot;
    p386_vm_init(&vm);
    slot.value = P386_FP_INT(7); slot.tag = P386_TAG_NUM;
    uv = p386_upvalue_find_or_add(&head, &slot);
    ASSERT_NOT_NULL(uv);
    ASSERT_EQ((void *)&slot, (void *)uv->slot);
    c = p386_closure_new(1, vm.program.protos, 1);
    ASSERT_NOT_NULL(c);
    ASSERT_EQ(1, c->n_upvalues);
    c->upvalues[0] = uv;
    ASSERT_EQ((void *)uv, (void *)c->upvalues[0]);
    ASSERT_EQ(P386_FP_INT(7), c->upvalues[0]->slot->value);
    PASS();
}

TEST(vm_closure_captures_parent_local_via_upvalue) {
    VmFixture f;
    P386VMState vm;
    unsigned long result = 0;
    ASSERT_EQ(P386_VM_HALTED, run_upvalue_capture_fixture(&f, &vm, &result));
    ASSERT_EQ(P386_TAG_NUM, vm.value_stack[0].tag);
    ASSERT_EQ((unsigned long)P386_FP_INT(7), result);
    PASS();
}

TEST(vm_call_lua_fixed_args_returns_sum) {
    VmFixture f;
    P386VMState vm;
    P386Value main_consts[] = {
        { P386_FP_INT(10), P386_TAG_NUM },
        { P386_FP_INT(20), P386_TAG_NUM }
    };
    unsigned long main_code[] = {
        P386_ABX(P386_OP_CLOSURE, 0, 1),
        P386_ABX(P386_OP_LOADK, 1, 0),
        P386_ABX(P386_OP_LOADK, 2, 1),
        P386_ABC(P386_OP_CALL, 0, 3, 2),
        P386_ABC(P386_OP_RETURN, 0, 2, 0)
    };
    unsigned long child_code[] = {
        P386_ABC(P386_OP_ADD, 2, P386_RK_REG(0), P386_RK_REG(1)),
        P386_ABC(P386_OP_RETURN, 2, 2, 0)
    };

    ASSERT_EQ(P386_VM_HALTED, run_two_proto_fixture(&f, &vm,
        main_code, 5, main_consts, 2, 4,
        child_code, 2, 0, 0, 2, 4));
    ASSERT_NUM(vm, 0, P386_FP_INT(30));
    ASSERT_EQ(0, vm.call_depth);
    PASS();
}

TEST(vm_call_lua_missing_args_are_nil) {
    VmFixture f;
    P386VMState vm;
    P386Value main_consts[] = {
        { P386_FP_INT(99), P386_TAG_NUM }
    };
    unsigned long main_code[] = {
        P386_ABX(P386_OP_CLOSURE, 0, 1),
        P386_ABX(P386_OP_LOADK, 1, 0),
        P386_ABC(P386_OP_CALL, 0, 2, 2),
        P386_ABC(P386_OP_RETURN, 0, 2, 0)
    };
    unsigned long child_code[] = {
        P386_ABC(P386_OP_RETURN, 1, 2, 0)
    };

    ASSERT_EQ(P386_VM_HALTED, run_two_proto_fixture(&f, &vm,
        main_code, 4, main_consts, 1, 4,
        child_code, 1, 0, 0, 2, 3));
    ASSERT_NIL(vm, 0);
    PASS();
}

TEST(vm_call_lua_pads_requested_results) {
    VmFixture f;
    P386VMState vm;
    P386Value child_consts[] = {
        { P386_FP_INT(7), P386_TAG_NUM }
    };
    unsigned long main_code[] = {
        P386_ABX(P386_OP_CLOSURE, 0, 1),
        P386_ABC(P386_OP_CALL, 0, 1, 4),
        P386_ABC(P386_OP_RETURN, 0, 4, 0)
    };
    unsigned long child_code[] = {
        P386_ABX(P386_OP_LOADK, 0, 0),
        P386_ABC(P386_OP_RETURN, 0, 2, 0)
    };

    ASSERT_EQ(P386_VM_HALTED, run_two_proto_fixture(&f, &vm,
        main_code, 3, 0, 0, 4,
        child_code, 2, child_consts, 1, 0, 2));
    ASSERT_NUM(vm, 0, P386_FP_INT(7));
    ASSERT_NIL(vm, 1);
    ASSERT_NIL(vm, 2);
    PASS();
}

TEST(vm_call_lua_truncates_extra_results) {
    VmFixture f;
    P386VMState vm;
    P386Value child_consts[] = {
        { P386_FP_INT(1), P386_TAG_NUM },
        { P386_FP_INT(2), P386_TAG_NUM },
        { P386_FP_INT(3), P386_TAG_NUM }
    };
    unsigned long main_code[] = {
        P386_ABX(P386_OP_CLOSURE, 0, 1),
        P386_ABC(P386_OP_CALL, 0, 1, 2),
        P386_ABC(P386_OP_RETURN, 0, 2, 0)
    };
    unsigned long child_code[] = {
        P386_ABX(P386_OP_LOADK, 0, 0),
        P386_ABX(P386_OP_LOADK, 1, 1),
        P386_ABX(P386_OP_LOADK, 2, 2),
        P386_ABC(P386_OP_RETURN, 0, 4, 0)
    };

    ASSERT_EQ(P386_VM_HALTED, run_two_proto_fixture(&f, &vm,
        main_code, 3, 0, 0, 4,
        child_code, 4, child_consts, 3, 0, 3));
    ASSERT_NUM(vm, 0, P386_FP_INT(1));
    ASSERT_EQ((int)(vm.value_stack + 1), (int)vm.top);
    PASS();
}

TEST(vm_call_lua_want_all_returns_actual_count) {
    VmFixture f;
    P386VMState vm;
    P386Value child_consts[] = {
        { P386_FP_INT(4), P386_TAG_NUM },
        { P386_FP_INT(5), P386_TAG_NUM }
    };
    unsigned long main_code[] = {
        P386_ABX(P386_OP_CLOSURE, 0, 1),
        P386_ABC(P386_OP_CALL, 0, 1, 0),
        P386_ABC(P386_OP_RETURN, 0, 3, 0)
    };
    unsigned long child_code[] = {
        P386_ABX(P386_OP_LOADK, 0, 0),
        P386_ABX(P386_OP_LOADK, 1, 1),
        P386_ABC(P386_OP_RETURN, 0, 3, 0)
    };

    ASSERT_EQ(P386_VM_HALTED, run_two_proto_fixture(&f, &vm,
        main_code, 3, 0, 0, 4,
        child_code, 3, child_consts, 2, 0, 2));
    ASSERT_NUM(vm, 0, P386_FP_INT(4));
    ASSERT_NUM(vm, 1, P386_FP_INT(5));
    PASS();
}

TEST(vm_call_lua_args_to_top) {
    /* CALL with B=0 takes arguments up to `top`. The fixture starts the main
     * frame with top = base + n_regs (= &R[4]); after loading the callee in
     * R0 and two args in R1,R2, the B=0 call sees nargs = (top-&R1)/8 = 3.
     * The 2-param callee binds the first two (10, 20) and sums them. */
    VmFixture f;
    P386VMState vm;
    P386Value main_consts[] = {
        { P386_FP_INT(10), P386_TAG_NUM },
        { P386_FP_INT(20), P386_TAG_NUM }
    };
    unsigned long main_code[] = {
        P386_ABX(P386_OP_CLOSURE, 0, 1),
        P386_ABX(P386_OP_LOADK, 1, 0),
        P386_ABX(P386_OP_LOADK, 2, 1),
        P386_ABC(P386_OP_CALL, 0, 0, 2),   /* B=0: args extend to top */
        P386_ABC(P386_OP_RETURN, 0, 2, 0)
    };
    unsigned long child_code[] = {
        P386_ABC(P386_OP_ADD, 2, P386_RK_REG(0), P386_RK_REG(1)),
        P386_ABC(P386_OP_RETURN, 2, 2, 0)
    };
    ASSERT_EQ(P386_VM_HALTED, run_two_proto_fixture(&f, &vm,
        main_code, 5, main_consts, 2, 4,
        child_code, 2, 0, 0, 2, 4));
    ASSERT_NUM(vm, 0, P386_FP_INT(30));
    PASS();
}

TEST(vm_return_all_values_from_lua_frame) {
    /* RETURN with B=0 returns every value from R[A] up to `top`. The child has
     * n_regs=2, so the fixture starts it with top = &R[2]; after loading two
     * values the B=0 return propagates exactly those two. The caller asks for
     * all results (CALL C=0) and re-returns them. */
    VmFixture f;
    P386VMState vm;
    P386Value child_consts[] = {
        { P386_FP_INT(5), P386_TAG_NUM },
        { P386_FP_INT(6), P386_TAG_NUM }
    };
    unsigned long main_code[] = {
        P386_ABX(P386_OP_CLOSURE, 0, 1),
        P386_ABC(P386_OP_CALL, 0, 1, 0),   /* want all results */
        P386_ABC(P386_OP_RETURN, 0, 3, 0)  /* return the two results */
    };
    unsigned long child_code[] = {
        P386_ABX(P386_OP_LOADK, 0, 0),
        P386_ABX(P386_OP_LOADK, 1, 1),
        P386_ABC(P386_OP_RETURN, 0, 0, 0)  /* B=0: return all up to top */
    };
    ASSERT_EQ(P386_VM_HALTED, run_two_proto_fixture(&f, &vm,
        main_code, 3, 0, 0, 4,
        child_code, 3, child_consts, 2, 0, 2));
    ASSERT_NUM(vm, 0, P386_FP_INT(5));
    ASSERT_NUM(vm, 1, P386_FP_INT(6));
    PASS();
}

TEST(vm_call_lua_depth_overflow_traps) {
    VmFixture f;
    P386VMState vm;
    unsigned long main_code[] = {
        P386_ABX(P386_OP_CLOSURE, 0, 1),
        P386_ABC(P386_OP_CALL, 0, 1, 1),
        P386_ABC(P386_OP_RETURN, 0, 1, 0)
    };
    unsigned long child_code[] = {
        P386_ABX(P386_OP_CLOSURE, 0, 1),
        P386_ABC(P386_OP_CALL, 0, 1, 1),
        P386_ABC(P386_OP_RETURN, 0, 1, 0)
    };

    ASSERT_EQ(P386_VM_ERR_BOUNDS, run_two_proto_fixture(&f, &vm,
        main_code, 3, 0, 0, 4,
        child_code, 3, 0, 0, 0, 4));
    ASSERT_STR_EQ("register/constant out of bounds", vm.error_msg);
    ASSERT_EQ(P386_CALL_STACK_DEPTH, vm.call_depth);
    PASS();
}
TEST(vm_call_requires_cfunc) {
    VmFixture f;
    P386VMState vm;
    fx_init(&f);
    fx_const(&f, P386_FP_INT(1), P386_TAG_NUM);
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 0, 0));
    fx_emit(&f, P386_ABC(P386_OP_CALL, 0, 1, 1));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 1, 0));
    ASSERT_EQ(P386_VM_ERR_TYPE, run_fixture(&f, &vm));
    ASSERT_STR_EQ("expected function", vm.error_msg);
    PASS();
}

TEST(vm_tailcall_lua_reuses_frame_and_returns_sum) {
    VmFixture f;
    P386VMState vm;
    P386Value main_consts[] = {
        { P386_FP_INT(10), P386_TAG_NUM },
        { P386_FP_INT(20), P386_TAG_NUM }
    };
    unsigned long main_code[] = {
        P386_ABX(P386_OP_CLOSURE, 0, 1),
        P386_ABX(P386_OP_LOADK, 1, 0),
        P386_ABX(P386_OP_LOADK, 2, 1),
        P386_ABC(P386_OP_TAILCALL, 0, 3, 0)
    };
    unsigned long child_code[] = {
        P386_ABC(P386_OP_ADD, 0, P386_RK_REG(0), P386_RK_REG(1)),
        P386_ABC(P386_OP_RETURN, 0, 2, 0)
    };

    ASSERT_EQ(P386_VM_HALTED, run_two_proto_fixture(&f, &vm,
        main_code, 4, main_consts, 2, 4,
        child_code, 2, 0, 0, 2, 4));
    ASSERT_NUM(vm, 0, P386_FP_INT(30));
    ASSERT_EQ(0, vm.call_depth);
    PASS();
}

TEST(vm_tailcall_cfunc_returns_value) {
    VmFixture f;
    P386VMState vm;

    fx_init(&f);
    fx_const(&f, P386_FP_INT(11), P386_TAG_NUM);
    fx_const(&f, P386_FP_INT(12), P386_TAG_NUM);
    fx_emit(&f, P386_ABC(P386_OP_GETGLOBAL, 0, P386_BUILTIN_PGET, 0));
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 1, 0));
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 2, 1));
    fx_emit(&f, P386_ABC(P386_OP_TAILCALL, 0, 3, 0));

    p8_ram_init();
    p8_ram.mem.screen[(12U * 64U) + (11U >> 1)] = 0x0a;

    ASSERT_EQ(P386_VM_HALTED, run_fixture(&f, &vm));
    ASSERT_TAG(vm, 1, P386_TAG_NUM);
    ASSERT_EQ(11 << 16, vm.value_stack[1].value);
    ASSERT_EQ(0, vm.call_depth);
    PASS();
}

/* ===================================================================== *
 * TFORCALL with a TAG_FUNC (Lua closure) iterator.                      *
 * ===================================================================== */

/* Generic-for over a closure iterator f(state, ctrl) that returns ctrl+1
 * while ctrl+1 <= state, else no values. Sums the loop variable: 1+2+3=6.
 * nvars=2 so the second loop var is exercised (always nil-padded), and
 * state is a NUM (not TAB), which validates the relaxed state type check. */
TEST(vm_tforcall_lua_iterator_sums_loop_vars) {
    VmFixture f;
    P386VMState vm;
    P386Value main_consts[] = {
        { P386_FP_INT(0), P386_TAG_NUM },
        { P386_FP_INT(3), P386_TAG_NUM }
    };
    P386Value child_consts[] = {
        { P386_FP_INT(1), P386_TAG_NUM }
    };
    unsigned long main_code[] = {
        P386_ABX(P386_OP_LOADK, 0, 0),                          /* sum = 0 */
        P386_ABX(P386_OP_CLOSURE, 1, 1),                        /* iterator */
        P386_ABX(P386_OP_LOADK, 2, 1),                          /* state = 3 */
        P386_ABX(P386_OP_LOADK, 3, 0),                          /* control = 0 */
        P386_ASBX(P386_OP_JMP, 0, 1),                           /* to TFORCALL */
        P386_ABC(P386_OP_ADD, 0, P386_RK_REG(0), P386_RK_REG(4)),
        P386_ABC(P386_OP_TFORCALL, 1, 2, 0),                    /* nvars = 2 */
        P386_ASBX(P386_OP_TFORLOOP, 1, -3),
        P386_ABC(P386_OP_RETURN, 0, 2, 0)
    };
    unsigned long child_code[] = {
        P386_ABC(P386_OP_ADD, 2, P386_RK_REG(1), P386_RK_CONST(0)),
        P386_ABC(P386_OP_LE, 3, P386_RK_REG(2), P386_RK_REG(0)),
        P386_ASBX(P386_OP_JMPT, 3, 1),
        P386_ABC(P386_OP_RETURN, 0, 1, 0),                      /* done: no values */
        P386_ABC(P386_OP_RETURN, 2, 2, 0)                       /* yield ctrl+1 */
    };

    ASSERT_EQ(P386_VM_HALTED, run_two_proto_fixture(&f, &vm,
        main_code, 9, main_consts, 2, 8,
        child_code, 5, child_consts, 1, 2, 4));
    ASSERT_NUM(vm, 0, P386_FP_INT(6));
    ASSERT_NIL(vm, 4);   /* first loop var nil after final call */
    ASSERT_NIL(vm, 5);   /* second loop var always nil-padded */
    ASSERT_EQ(0, vm.call_depth);
    PASS();
}

/* Iterator that returns no values: want_rets nil-padding makes R[A+3] nil,
 * so TFORLOOP exits immediately and the body never runs. */
TEST(vm_tforcall_lua_iterator_empty_returns_skip_body) {
    VmFixture f;
    P386VMState vm;
    P386Value main_consts[] = {
        { P386_FP_INT(0), P386_TAG_NUM },
        { P386_FP_INT(99), P386_TAG_NUM }
    };
    unsigned long main_code[] = {
        P386_ABX(P386_OP_LOADK, 0, 1),                          /* marker = 99 */
        P386_ABX(P386_OP_CLOSURE, 1, 1),
        P386_ABX(P386_OP_LOADK, 2, 0),
        P386_ABX(P386_OP_LOADK, 3, 0),
        P386_ASBX(P386_OP_JMP, 0, 1),
        P386_ABX(P386_OP_LOADK, 0, 0),                          /* body clobbers */
        P386_ABC(P386_OP_TFORCALL, 1, 1, 0),
        P386_ASBX(P386_OP_TFORLOOP, 1, -3),
        P386_ABC(P386_OP_RETURN, 0, 2, 0)
    };
    unsigned long child_code[] = {
        P386_ABC(P386_OP_RETURN, 0, 1, 0)
    };

    ASSERT_EQ(P386_VM_HALTED, run_two_proto_fixture(&f, &vm,
        main_code, 9, main_consts, 2, 8,
        child_code, 1, 0, 0, 2, 4));
    ASSERT_NUM(vm, 0, P386_FP_INT(99));
    ASSERT_NIL(vm, 4);
    ASSERT_EQ(0, vm.call_depth);
    PASS();
}

/* Non-callable iterator (NUM) still traps err_type_iter. */
TEST(vm_tforcall_non_callable_iterator_traps) {
    VmFixture f;
    P386VMState vm;
    fx_init(&f);
    fx_const(&f, P386_FP_INT(7), P386_TAG_NUM);
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 0, 0));
    fx_emit(&f, P386_ABC(P386_OP_TFORCALL, 0, 1, 0));
    fx_emit(&f, P386_ASBX(P386_OP_TFORLOOP, 0, -2));
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 1, 0));
    ASSERT_EQ(P386_VM_ERR_TYPE, run_fixture(&f, &vm));
    ASSERT_STR_EQ("expected table iterator state", vm.error_msg);
    PASS();
}

/* Iterator whose body runs its own TFORCALL on a fresh copy of itself:
 * recursion through TFORCALL's frame push must hit the call-depth limit
 * and trap cleanly (ERR_BOUNDS), not smash the stack. */
TEST(vm_tforcall_lua_iterator_depth_overflow_traps) {
    VmFixture f;
    P386VMState vm;
    unsigned long main_code[] = {
        P386_ABX(P386_OP_CLOSURE, 0, 1),
        P386_ABC(P386_OP_LOADN, 1, 2, 0),                       /* state, ctrl = nil */
        P386_ABC(P386_OP_TFORCALL, 0, 1, 0),
        P386_ASBX(P386_OP_TFORLOOP, 0, -2),
        P386_ABC(P386_OP_RETURN, 0, 1, 0)
    };
    unsigned long child_code[] = {
        P386_ABX(P386_OP_CLOSURE, 2, 1),
        P386_ABC(P386_OP_LOADN, 3, 2, 0),
        P386_ABC(P386_OP_TFORCALL, 2, 1, 0),
        P386_ASBX(P386_OP_TFORLOOP, 2, -2),
        P386_ABC(P386_OP_RETURN, 0, 1, 0)
    };

    ASSERT_EQ(P386_VM_ERR_BOUNDS, run_two_proto_fixture(&f, &vm,
        main_code, 5, 0, 0, 8,
        child_code, 5, 0, 0, 2, 8));
    ASSERT_STR_EQ("register/constant out of bounds", vm.error_msg);
    ASSERT_EQ(P386_CALL_STACK_DEPTH, vm.call_depth);
    PASS();
}

/* ===================================================================== *
 * Trivial-tier builtin tests. These call the C builtins directly (same  *
 * pattern as vm_builtin_cls_pset_pget) since they're pure value ops.    *
 * ===================================================================== */

#define BNUM(a, i, n) do { (a)[i].value = (int32_t)((n) << 16); (a)[i].tag = P386_TAG_NUM; } while(0)
#define BFP(a, i, fp) do { (a)[i].value = (int32_t)(fp); (a)[i].tag = P386_TAG_NUM; } while(0)

TEST(vm_builtin_math_basic) {
    P386VMState vm;
    P386Value a[3];
    p386_vm_init(&vm);

    BNUM(a, 0, -5);
    ASSERT_EQ(1, p386_builtin_abs(&vm, a, 1, 1));
    ASSERT_EQ(P386_FP_INT(5), a[0].value);

    BFP(a, 0, P386_FP_INT(3) | 0x8000); /* 3.5 */
    ASSERT_EQ(1, p386_builtin_flr(&vm, a, 1, 1));
    ASSERT_EQ(P386_FP_INT(3), a[0].value);

    BFP(a, 0, P386_FP_INT(3) | 0x8000); /* 3.5 */
    ASSERT_EQ(1, p386_builtin_ceil(&vm, a, 1, 1));
    ASSERT_EQ(P386_FP_INT(4), a[0].value);

    BNUM(a, 0, -9);
    ASSERT_EQ(1, p386_builtin_sgn(&vm, a, 1, 1));
    ASSERT_EQ(P386_FP_INT(-1), a[0].value);
    BNUM(a, 0, 0);
    ASSERT_EQ(1, p386_builtin_sgn(&vm, a, 1, 1));
    ASSERT_EQ(P386_FP_INT(1), a[0].value); /* sgn(0)==1 */

    BNUM(a, 0, 3); BNUM(a, 1, 7);
    ASSERT_EQ(1, p386_builtin_min(&vm, a, 2, 1));
    ASSERT_EQ(P386_FP_INT(3), a[0].value);
    BNUM(a, 0, 3); BNUM(a, 1, 7);
    ASSERT_EQ(1, p386_builtin_max(&vm, a, 2, 1));
    ASSERT_EQ(P386_FP_INT(7), a[0].value);

    BNUM(a, 0, 1); BNUM(a, 1, 9); BNUM(a, 2, 5);
    ASSERT_EQ(1, p386_builtin_mid(&vm, a, 3, 1));
    ASSERT_EQ(P386_FP_INT(5), a[0].value);
    PASS();
}

TEST(vm_builtin_sqrt) {
    P386VMState vm;
    P386Value a[1];
    p386_vm_init(&vm);
    BNUM(a, 0, 144);
    ASSERT_EQ(1, p386_builtin_sqrt(&vm, a, 1, 1));
    ASSERT_EQ(P386_FP_INT(12), a[0].value);
    BNUM(a, 0, 2);
    ASSERT_EQ(1, p386_builtin_sqrt(&vm, a, 1, 1));
    /* sqrt(2) ~= 1.41421 -> 92681/65536. Allow +-2 lsb. */
    ASSERT_TRUE(a[0].value > 92679 && a[0].value < 92683);
    BNUM(a, 0, -4);
    ASSERT_EQ(1, p386_builtin_sqrt(&vm, a, 1, 1));
    ASSERT_EQ(0, a[0].value);
    PASS();
}

TEST(vm_builtin_trig) {
    P386VMState vm;
    P386Value a[1];
    p386_vm_init(&vm);
    /* PICO-8: cos(0)=1, sin(0)=0, cos(0.25)=0, sin(0.25)=-1. */
    BNUM(a, 0, 0);
    ASSERT_EQ(1, p386_builtin_cos(&vm, a, 1, 1));
    ASSERT_EQ(P386_FP_INT(1), a[0].value);
    BNUM(a, 0, 0);
    ASSERT_EQ(1, p386_builtin_sin(&vm, a, 1, 1));
    ASSERT_EQ(0, a[0].value);
    BFP(a, 0, 0x4000); /* 0.25 turn */
    ASSERT_EQ(1, p386_builtin_sin(&vm, a, 1, 1));
    ASSERT_EQ(P386_FP_INT(-1), a[0].value);
    BFP(a, 0, 0x4000); /* 0.25 turn */
    ASSERT_EQ(1, p386_builtin_cos(&vm, a, 1, 1));
    ASSERT_TRUE(a[0].value > -4 && a[0].value < 4); /* ~0 */
    PASS();
}

TEST(vm_builtin_bitwise) {
    P386VMState vm;
    P386Value a[2];
    p386_vm_init(&vm);
    BNUM(a, 0, 12); BNUM(a, 1, 10);
    ASSERT_EQ(1, p386_builtin_band(&vm, a, 2, 1));
    ASSERT_EQ(P386_FP_INT(8), a[0].value);
    BNUM(a, 0, 12); BNUM(a, 1, 10);
    ASSERT_EQ(1, p386_builtin_bor(&vm, a, 2, 1));
    ASSERT_EQ(P386_FP_INT(14), a[0].value);
    BNUM(a, 0, 12); BNUM(a, 1, 10);
    ASSERT_EQ(1, p386_builtin_bxor(&vm, a, 2, 1));
    ASSERT_EQ(P386_FP_INT(6), a[0].value);
    /* shl: 1 << 4 = 16 (operates on raw bits) */
    BNUM(a, 0, 1); BNUM(a, 1, 4);
    ASSERT_EQ(1, p386_builtin_shl(&vm, a, 2, 1));
    ASSERT_EQ(P386_FP_INT(16), a[0].value);
    /* shr arithmetic: 16 >> 2 = 4 */
    BNUM(a, 0, 16); BNUM(a, 1, 2);
    ASSERT_EQ(1, p386_builtin_shr(&vm, a, 2, 1));
    ASSERT_EQ(P386_FP_INT(4), a[0].value);
    PASS();
}

TEST(vm_builtin_peek_poke) {
    P386VMState vm;
    P386Value a[2];
    p8_ram_init();
    p386_vm_init(&vm);
    /* poke then peek */
    BNUM(a, 0, 0x4300); BNUM(a, 1, 0xab);
    ASSERT_EQ(0, p386_builtin_poke(&vm, a, 2, 0));
    ASSERT_EQ(0xab, p8_ram.raw[0x4300]);
    BNUM(a, 0, 0x4300);
    ASSERT_EQ(1, p386_builtin_peek(&vm, a, 1, 1));
    ASSERT_EQ(P386_FP_INT(0xab), a[0].value);
    /* poke2 little-endian */
    BNUM(a, 0, 0x4310); BNUM(a, 1, 0x1234);
    ASSERT_EQ(0, p386_builtin_poke2(&vm, a, 2, 0));
    ASSERT_EQ(0x34, p8_ram.raw[0x4310]);
    ASSERT_EQ(0x12, p8_ram.raw[0x4311]);
    BNUM(a, 0, 0x4310);
    ASSERT_EQ(1, p386_builtin_peek2(&vm, a, 1, 1));
    ASSERT_EQ(P386_FP_INT(0x1234), a[0].value);
    PASS();
}

TEST(vm_builtin_table_add_count_del) {
    P386VMState vm;
    P386Value a[2];
    P386Table *t;
    p386_vm_init(&vm);
    t = p386_table_new(0, 0);
    ASSERT_NOT_NULL(t);

    a[0].value = (int32_t)(uintptr_t)t; a[0].tag = P386_TAG_TAB;
    BNUM(a, 1, 100);
    ASSERT_EQ(1, p386_builtin_add(&vm, a, 2, 1)); /* returns value */
    ASSERT_EQ(P386_FP_INT(100), a[0].value);

    a[0].value = (int32_t)(uintptr_t)t; a[0].tag = P386_TAG_TAB;
    BNUM(a, 1, 200);
    p386_builtin_add(&vm, a, 2, 0);

    a[0].value = (int32_t)(uintptr_t)t; a[0].tag = P386_TAG_TAB;
    ASSERT_EQ(1, p386_builtin_count(&vm, a, 1, 1));
    ASSERT_EQ(P386_FP_INT(2), a[0].value);

    /* del value 100 -> shifts 200 down, count becomes 1 */
    a[0].value = (int32_t)(uintptr_t)t; a[0].tag = P386_TAG_TAB;
    BNUM(a, 1, 100);
    ASSERT_EQ(1, p386_builtin_del(&vm, a, 2, 1));
    a[0].value = (int32_t)(uintptr_t)t; a[0].tag = P386_TAG_TAB;
    ASSERT_EQ(1, p386_builtin_count(&vm, a, 1, 1));
    ASSERT_EQ(P386_FP_INT(1), a[0].value);
    PASS();
}

TEST(vm_builtin_string_ops) {
    P386VMState vm;
    P386Value a[3];
    P386String *s;
    p386_vm_init(&vm);

    /* tostr(42) -> "42" */
    BNUM(a, 0, 42);
    ASSERT_EQ(1, p386_builtin_tostr(&vm, a, 1, 1));
    ASSERT_EQ(P386_TAG_STR, a[0].tag);
    s = (P386String *)(uintptr_t)a[0].value;
    ASSERT_STR_EQ("42", s->data);

    /* tonum("3.5") -> 3.5 */
    s = p386_string_intern("3.5", 3);
    a[0].value = (int32_t)(uintptr_t)s; a[0].tag = P386_TAG_STR;
    ASSERT_EQ(1, p386_builtin_tonum(&vm, a, 1, 1));
    ASSERT_EQ(P386_TAG_NUM, a[0].tag);
    ASSERT_EQ((int32_t)(P386_FP_INT(3) | 0x8000), a[0].value);

    /* chr(65) -> "A"; ord("A") -> 65 */
    BNUM(a, 0, 65);
    ASSERT_EQ(1, p386_builtin_chr(&vm, a, 1, 1));
    ASSERT_EQ(P386_TAG_STR, a[0].tag);
    s = (P386String *)(uintptr_t)a[0].value;
    ASSERT_STR_EQ("A", s->data);
    ASSERT_EQ(1, p386_builtin_ord(&vm, a, 1, 1));
    ASSERT_EQ(P386_FP_INT(65), a[0].value);

    /* sub("hello", 2, 4) -> "ell" */
    s = p386_string_intern("hello", 5);
    a[0].value = (int32_t)(uintptr_t)s; a[0].tag = P386_TAG_STR;
    BNUM(a, 1, 2); BNUM(a, 2, 4);
    ASSERT_EQ(1, p386_builtin_sub(&vm, a, 3, 1));
    s = (P386String *)(uintptr_t)a[0].value;
    ASSERT_STR_EQ("ell", s->data);
    PASS();
}

TEST(vm_builtin_call_via_dispatch) {
    /* End-to-end: GETGLOBAL flr; LOADK 3.9; CALL want 1; RETURN. Exercises the
     * CFUNC call path through the asm dispatcher, not just a direct C call. */
    VmFixture f;
    P386VMState vm;
    fx_init(&f);
    fx_const(&f, (int32_t)(P386_FP_INT(3) | 0xe666), P386_TAG_NUM); /* ~3.9 */
    fx_emit(&f, P386_ABC(P386_OP_GETGLOBAL, 0, P386_BUILTIN_FLR, 0));
    fx_emit(&f, P386_ABX(P386_OP_LOADK, 1, 0));
    fx_emit(&f, P386_ABC(P386_OP_CALL, 0, 2, 2)); /* flr(3.9), want 1 */
    fx_emit(&f, P386_ABC(P386_OP_RETURN, 0, 2, 0));
    ASSERT_EQ(P386_VM_HALTED, run_fixture(&f, &vm));
    ASSERT_NUM(vm, 0, P386_FP_INT(3));
    PASS();
}
