#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "test.h"
#include "rust.h"
#include "p386_vm.h"
#include "builtins.h"
#include "mem.h"

/*
 * Tests for the PICO-8 Lua compiler (Rust FFI).
 * These exercise p8_compile() / p8_parse_rs() with various
 * PICO-8 Lua snippets.
 */

/* ── Parser validation (p8_parse_rs) ── */

TEST(parse_empty_program) {
    const char *code = "";
    ASSERT_EQ(0, p8_parse_rs((const unsigned char *)code, 0));
    PASS();
}

TEST(parse_simple_assignment) {
    const char *code = "x = 1";
    ASSERT_EQ(0, p8_parse_rs((const unsigned char *)code, strlen(code)));
    PASS();
}

TEST(parse_function_def) {
    const char *code = "function hello()\n print(\"hi\")\nend";
    ASSERT_EQ(0, p8_parse_rs((const unsigned char *)code, strlen(code)));
    PASS();
}

TEST(parse_pico8_shorthand_if) {
    const char *code = "if (x > 0) print(x)";
    ASSERT_EQ(0, p8_parse_rs((const unsigned char *)code, strlen(code)));
    PASS();
}

TEST(parse_long_string_eq_level) {
    /* [=[ ... ]=] long-bracket strings of arbitrary level must parse. */
    const char *code = "x = [=[a]]b]=]\ny = [==[c]==]";
    ASSERT_EQ(0, p8_parse_rs((const unsigned char *)code, strlen(code)));
    PASS();
}

TEST(parse_long_comment_eq_level) {
    const char *code = "--[=[\nmultiline ]] comment\n]=]\nx = 1";
    ASSERT_EQ(0, p8_parse_rs((const unsigned char *)code, strlen(code)));
    PASS();
}

TEST(parse_short_if_stops_at_newline) {
    /* The single-line if body ends at the newline; the next line is a
     * separate statement (not part of the conditional). */
    P8Program prog;
    const unsigned char *bc;
    unsigned long bc_len;
    P386VMState vm;
    const char *code = "a=0 b=0\nif (false) a=1\nb=2\nreturn a,b";
    prog = p8_compile((const unsigned char *)code, strlen(code));
    ASSERT_NOT_NULL(prog);
    bc_len = p8_program_bytecode(prog, &bc);
    ASSERT_TRUE(p386_vm_load(&vm, bc, bc_len));
    ASSERT_EQ(P386_VM_HALTED, p386_vm_run(&vm));
    /* a stays 0 (guarded), b becomes 2 (unconditional). */
    ASSERT_EQ(0, vm.value_stack[0].value);
    ASSERT_EQ(2 << 16, vm.value_stack[1].value);
    p8_free_program(prog);
    PASS();
}

TEST(parse_pico8_shorthand_print) {
    const char *code = "?\"hello world\"";
    ASSERT_EQ(0, p8_parse_rs((const unsigned char *)code, strlen(code)));
    PASS();
}

TEST(parse_pico8_compound_assign) {
    const char *code = "x += 1\ny -= 2\nz *= 3";
    ASSERT_EQ(0, p8_parse_rs((const unsigned char *)code, strlen(code)));
    PASS();
}

TEST(parse_pico8_bitwise_ops) {
    const char *code = "a = b & c | d ^^ e";
    ASSERT_EQ(0, p8_parse_rs((const unsigned char *)code, strlen(code)));
    PASS();
}

TEST(parse_pico8_shift_ops) {
    const char *code = "a = x << 2\nb = y >> 3\nc = z >>> 1\nd = w <<> 4\ne = v >>< 5";
    ASSERT_EQ(0, p8_parse_rs((const unsigned char *)code, strlen(code)));
    PASS();
}

TEST(parse_for_loop) {
    const char *code = "for i=1,10 do\n print(i)\nend";
    ASSERT_EQ(0, p8_parse_rs((const unsigned char *)code, strlen(code)));
    PASS();
}

TEST(parse_for_in_loop) {
    const char *code = "for k,v in pairs(t) do\n print(k,v)\nend";
    ASSERT_EQ(0, p8_parse_rs((const unsigned char *)code, strlen(code)));
    PASS();
}

TEST(parse_repeat_until) {
    const char *code = "repeat\n x += 1\nuntil x > 10";
    ASSERT_EQ(0, p8_parse_rs((const unsigned char *)code, strlen(code)));
    PASS();
}

TEST(parse_table_constructor) {
    const char *code = "t = {1, 2, 3, name=\"hello\", [4]=true}";
    ASSERT_EQ(0, p8_parse_rs((const unsigned char *)code, strlen(code)));
    PASS();
}

TEST(parse_nested_functions) {
    const char *code =
        "function outer()\n"
        " local function inner(x)\n"
        "  return x * 2\n"
        " end\n"
        " return inner(5)\n"
        "end";
    ASSERT_EQ(0, p8_parse_rs((const unsigned char *)code, strlen(code)));
    PASS();
}

TEST(parse_invalid_syntax) {
    const char *code = "function (((";
    ASSERT_EQ(-1, p8_parse_rs((const unsigned char *)code, strlen(code)));
    PASS();
}

TEST(parse_null_input) {
    ASSERT_EQ(-1, p8_parse_rs(0, 0));
    PASS();
}

/* ── Full compilation (p8_compile) ── */

TEST(compile_empty) {
    P8Program prog;
    const unsigned char *bc;
    unsigned long bc_len;

    prog = p8_compile((const unsigned char *)"", 0);
    ASSERT_NOT_NULL(prog);

    bc_len = p8_program_bytecode(prog, &bc);
    ASSERT_TRUE(bc_len > 0);

    p8_free_program(prog);
    PASS();
}

TEST(compile_hello_world) {
    const char *code = "print(\"hello world\")";
    P8Program prog;
    const unsigned char *bc;
    unsigned long bc_len;

    prog = p8_compile((const unsigned char *)code, strlen(code));
    ASSERT_NOT_NULL(prog);

    bc_len = p8_program_bytecode(prog, &bc);
    ASSERT_TRUE(bc_len > 0);
    ASSERT_NOT_NULL(bc);

    /* Should have at least one constant (the string "hello world") */
    ASSERT_TRUE(p8_program_num_constants(prog) >= 1);

    p8_free_program(prog);
    PASS();
}

TEST(compile_arithmetic) {
    const char *code = "x = 1 + 2 * 3 - 4 / 2";
    P8Program prog;
    const unsigned char *bc;
    unsigned long bc_len;

    prog = p8_compile((const unsigned char *)code, strlen(code));
    ASSERT_NOT_NULL(prog);

    bc_len = p8_program_bytecode(prog, &bc);
    ASSERT_TRUE(bc_len > 0);

    p8_free_program(prog);
    PASS();
}

TEST(compile_game_loop) {
    const char *code =
        "function _init()\n"
        " x = 64\n"
        " y = 64\n"
        "end\n"
        "\n"
        "function _update()\n"
        " if btn(0) then x -= 1 end\n"
        " if btn(1) then x += 1 end\n"
        " if btn(2) then y -= 1 end\n"
        " if btn(3) then y += 1 end\n"
        "end\n"
        "\n"
        "function _draw()\n"
        " cls()\n"
        " circfill(x, y, 4, 7)\n"
        "end";
    P8Program prog;
    const unsigned char *bc;
    unsigned long bc_len;

    prog = p8_compile((const unsigned char *)code, strlen(code));
    ASSERT_NOT_NULL(prog);

    bc_len = p8_program_bytecode(prog, &bc);
    ASSERT_TRUE(bc_len > 0);

    /* Game loop has 3 top-level function defs -> at least 3 protos */
    ASSERT_TRUE(p8_program_num_protos(prog) >= 3);

    p8_free_program(prog);
    PASS();
}

TEST(compile_invalid_returns_null) {
    const char *code = "end end end )))))";
    P8Program prog;

    prog = p8_compile((const unsigned char *)code, strlen(code));
    ASSERT_NULL(prog);
    PASS();
}

TEST(compile_free_null_safe) {
    /* Freeing NULL should not crash */
    p8_free_program(0);
    PASS();
}

TEST(compile_print_runs_vm) {
    const char *code = "print(\"0.1.10c\")";
    P8Program prog;
    const unsigned char *bc;
    unsigned long bc_len;
    P386VMState vm;

    prog = p8_compile((const unsigned char *)code, strlen(code));
    ASSERT_NOT_NULL(prog);
    bc_len = p8_program_bytecode(prog, &bc);
    ASSERT_TRUE(bc_len > 4);
    ASSERT_NOT_NULL(bc);
    ASSERT_TRUE(p386_vm_load(&vm, bc, bc_len));
    ASSERT_EQ(P386_VM_HALTED, p386_vm_run(&vm));
    p8_free_program(prog);
    PASS();
}

TEST(compile_arithmetic_global_runs_vm) {
    const char *code = "x = 1 + 2 * 3 - 4 / 2\nreturn x";
    P8Program prog;
    const unsigned char *bc;
    unsigned long bc_len;
    P386VMState vm;

    prog = p8_compile((const unsigned char *)code, strlen(code));
    ASSERT_NOT_NULL(prog);
    bc_len = p8_program_bytecode(prog, &bc);
    ASSERT_TRUE(p386_vm_load(&vm, bc, bc_len));
    ASSERT_EQ(P386_VM_HALTED, p386_vm_run(&vm));
    ASSERT_EQ(P386_TAG_NUM, vm.value_stack[0].tag);
    ASSERT_EQ(5 << 16, vm.value_stack[0].value);
    p8_free_program(prog);
    PASS();
}

TEST(compile_boolean_short_circuit_runs_vm) {
    const char *code = "return false and 9, nil or 7";
    P8Program prog;
    const unsigned char *bc;
    unsigned long bc_len;
    P386VMState vm;

    prog = p8_compile((const unsigned char *)code, strlen(code));
    ASSERT_NOT_NULL(prog);
    bc_len = p8_program_bytecode(prog, &bc);
    ASSERT_TRUE(p386_vm_load(&vm, bc, bc_len));
    ASSERT_EQ(P386_VM_HALTED, p386_vm_run(&vm));
    ASSERT_EQ(P386_TAG_BOOL, vm.value_stack[0].tag);
    ASSERT_EQ(0, vm.value_stack[0].value);
    ASSERT_EQ(P386_TAG_NUM, vm.value_stack[1].tag);
    ASSERT_EQ(7 << 16, vm.value_stack[1].value);
    p8_free_program(prog);
    PASS();
}

TEST(compile_multi_arg_call_preserves_args) {
    const char *code = "print(1+2, 3+4)";
    P8Program prog;
    const unsigned char *bc;
    unsigned long bc_len;
    P386VMState vm;

    prog = p8_compile((const unsigned char *)code, strlen(code));
    ASSERT_NOT_NULL(prog);
    bc_len = p8_program_bytecode(prog, &bc);
    ASSERT_TRUE(p386_vm_load(&vm, bc, bc_len));
    ASSERT_EQ(P386_VM_HALTED, p386_vm_run(&vm));
    p8_free_program(prog);
    PASS();
}

TEST(compile_local_scope_shadowing_runs_vm) {
    const char *code = "local x=1\ndo local x=2 end\nreturn x";
    P8Program prog;
    const unsigned char *bc;
    unsigned long bc_len;
    P386VMState vm;

    prog = p8_compile((const unsigned char *)code, strlen(code));
    ASSERT_NOT_NULL(prog);
    bc_len = p8_program_bytecode(prog, &bc);
    ASSERT_TRUE(p386_vm_load(&vm, bc, bc_len));
    ASSERT_EQ(P386_VM_HALTED, p386_vm_run(&vm));
    ASSERT_EQ(P386_TAG_NUM, vm.value_stack[0].tag);
    ASSERT_EQ(1 << 16, vm.value_stack[0].value);
    p8_free_program(prog);
    PASS();
}

TEST(compile_multi_assign_rhs_first_runs_vm) {
    const char *code = "local a=1\nlocal b=2\na,b=b,a\nreturn a,b";
    P8Program prog;
    const unsigned char *bc;
    unsigned long bc_len;
    P386VMState vm;

    prog = p8_compile((const unsigned char *)code, strlen(code));
    ASSERT_NOT_NULL(prog);
    bc_len = p8_program_bytecode(prog, &bc);
    ASSERT_TRUE(p386_vm_load(&vm, bc, bc_len));
    ASSERT_EQ(P386_VM_HALTED, p386_vm_run(&vm));
    ASSERT_EQ(P386_TAG_NUM, vm.value_stack[0].tag);
    ASSERT_EQ(2 << 16, vm.value_stack[0].value);
    ASSERT_EQ(P386_TAG_NUM, vm.value_stack[1].tag);
    ASSERT_EQ(1 << 16, vm.value_stack[1].value);
    p8_free_program(prog);
    PASS();
}

TEST(compile_constant_overflow_returns_null) {
    char code[2048];
    int off = 0;
    int i;
    off += sprintf(code + off, "return ");
    for (i = 0; i < 140; i++) {
        off += sprintf(code + off, "%d%s", i, (i == 139) ? "" : ",");
    }
    ASSERT_NULL(p8_compile((const unsigned char *)code, strlen(code)));
    PASS();
}

TEST(compile_branch_local_scope_does_not_leak) {
    const char *code = "if true then local y=2 end\nreturn y";
    P8Program prog;
    const unsigned char *bc;
    unsigned long bc_len;
    P386VMState vm;

    prog = p8_compile((const unsigned char *)code, strlen(code));
    ASSERT_NOT_NULL(prog);
    bc_len = p8_program_bytecode(prog, &bc);
    ASSERT_TRUE(p386_vm_load(&vm, bc, bc_len));
    ASSERT_EQ(P386_VM_HALTED, p386_vm_run(&vm));
    ASSERT_EQ(P386_TAG_NIL, vm.value_stack[0].tag);
    p8_free_program(prog);
    PASS();
}

TEST(compile_local_function_call_runs_vm) {
    const char *code = "local function add1(x) return x+1 end\nreturn add1(41)";
    P8Program prog;
    const unsigned char *bc;
    unsigned long bc_len;
    P386VMState vm;

    prog = p8_compile((const unsigned char *)code, strlen(code));
    ASSERT_NOT_NULL(prog);
    bc_len = p8_program_bytecode(prog, &bc);
    ASSERT_TRUE(p386_vm_load(&vm, bc, bc_len));
    ASSERT_EQ(P386_VM_HALTED, p386_vm_run(&vm));
    ASSERT_EQ(P386_TAG_NUM, vm.value_stack[0].tag);
    ASSERT_EQ(42 << 16, vm.value_stack[0].value);
    p8_free_program(prog);
    PASS();
}

TEST(compile_function_literal_call_runs_vm) {
    const char *code = "local add1=function(x) return x+1 end\nreturn add1(41)";
    P8Program prog;
    const unsigned char *bc;
    unsigned long bc_len;
    P386VMState vm;

    prog = p8_compile((const unsigned char *)code, strlen(code));
    ASSERT_NOT_NULL(prog);
    bc_len = p8_program_bytecode(prog, &bc);
    ASSERT_TRUE(p386_vm_load(&vm, bc, bc_len));
    ASSERT_EQ(P386_VM_HALTED, p386_vm_run(&vm));
    ASSERT_EQ(P386_TAG_NUM, vm.value_stack[0].tag);
    ASSERT_EQ(42 << 16, vm.value_stack[0].value);
    p8_free_program(prog);
    PASS();
}

TEST(compile_nested_noncapturing_function_call_runs_vm) {
    const char *code = "function outer() local function inner(x) return x*2 end return inner(21) end\nreturn outer()";
    P8Program prog;
    const unsigned char *bc;
    unsigned long bc_len;
    P386VMState vm;

    prog = p8_compile((const unsigned char *)code, strlen(code));
    ASSERT_NOT_NULL(prog);
    bc_len = p8_program_bytecode(prog, &bc);
    ASSERT_TRUE(p386_vm_load(&vm, bc, bc_len));
    ASSERT_EQ(P386_VM_HALTED, p386_vm_run(&vm));
    ASSERT_EQ(P386_TAG_NUM, vm.value_stack[0].tag);
    ASSERT_EQ(42 << 16, vm.value_stack[0].value);
    p8_free_program(prog);
    PASS();
}

TEST(compile_captured_local_runs_vm) {
    /* Upvalue capture is now supported: inner() closes over outer's y. */
    const char *code = "function outer() local y=7 local function inner() return y end return inner() end\nreturn outer()";
    P8Program prog;
    const unsigned char *bc;
    unsigned long bc_len;
    P386VMState vm;

    prog = p8_compile((const unsigned char *)code, strlen(code));
    ASSERT_NOT_NULL(prog);
    bc_len = p8_program_bytecode(prog, &bc);
    ASSERT_TRUE(p386_vm_load(&vm, bc, bc_len));
    ASSERT_EQ(P386_VM_HALTED, p386_vm_run(&vm));
    ASSERT_EQ(P386_TAG_NUM, vm.value_stack[0].tag);
    ASSERT_EQ(7 << 16, vm.value_stack[0].value);
    p8_free_program(prog);
    PASS();
}

TEST(compile_lua_function_call_runs_vm) {
    const char *code = "function add1(x) return x+1 end\nreturn add1(41)";
    P8Program prog;
    const unsigned char *bc;
    unsigned long bc_len;
    P386VMState vm;

    prog = p8_compile((const unsigned char *)code, strlen(code));
    ASSERT_NOT_NULL(prog);
    ASSERT_TRUE(p8_program_num_protos(prog) >= 1);
    bc_len = p8_program_bytecode(prog, &bc);
    ASSERT_TRUE(p386_vm_load(&vm, bc, bc_len));
    ASSERT_EQ(P386_VM_HALTED, p386_vm_run(&vm));
    ASSERT_EQ(P386_TAG_NUM, vm.value_stack[0].tag);
    ASSERT_EQ(42 << 16, vm.value_stack[0].value);
    p8_free_program(prog);
    PASS();
}

TEST(compile_lifecycle_slots_and_host_call_draw_pixels) {
    const char *code = "function _init() cls(1) end\nfunction _draw() pset(3,4,7) end";
    P8Program prog;
    const unsigned char *bc;
    unsigned long bc_len;
    P386VMState vm;

    p8_ram_init();
    prog = p8_compile((const unsigned char *)code, strlen(code));
    ASSERT_NOT_NULL(prog);
    bc_len = p8_program_bytecode(prog, &bc);
    ASSERT_TRUE(p386_vm_load(&vm, bc, bc_len));
    ASSERT_EQ(P386_VM_HALTED, p386_vm_run(&vm));
    ASSERT_EQ(P386_TAG_FUNC, vm.globals[P386_GLOBAL_INIT].tag);
    ASSERT_EQ(P386_TAG_FUNC, vm.globals[P386_GLOBAL_DRAW].tag);
    ASSERT_EQ(P386_VM_HALTED, p386_vm_call_global(&vm, P386_GLOBAL_INIT, 0, 0));
    ASSERT_EQ(0x11, p8_ram.mem.screen[0]);
    ASSERT_EQ(P386_VM_HALTED, p386_vm_call_global(&vm, P386_GLOBAL_DRAW, 0, 0));
    ASSERT_EQ(0x71, p8_ram.mem.screen[4 * 64 + 1]);
    p8_free_program(prog);
    PASS();
}
