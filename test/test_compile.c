#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "test.h"
#include "rust.h"
#include "p386_vm.h"
#include "builtins.h"
#include "mem.h"
#include "vga.h"

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

TEST(parse_tilde_is_binary_xor) {
    /* PICO-8 accepts both ^^ and ~ as bitwise xor. */
    const char *code = "a = 6 ~ 3\nb = 6 ^^ 3";
    ASSERT_EQ(0, p8_parse_rs((const unsigned char *)code, strlen(code)));
    PASS();
}

TEST(parse_deeply_nested_tables_no_blowup) {
    /* Right-associative concat/pow rules previously re-parsed their left
     * operand, giving exponential time on nested tables. This 12-deep table
     * must compile quickly (it would hang the old parser). */
    const char *code =
        "t={{{{{{{{{{{{1}}}}}}}}}}}}";
    P8Program prog = p8_compile((const unsigned char *)code, strlen(code));
    ASSERT_NOT_NULL(prog);
    p8_free_program(prog);
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

TEST(compile_constants_past_old_limit_compile) {
    char code[2048];
    int off = 0;
    int i;
    off += sprintf(code + off, "return ");
    for (i = 0; i < 140; i++) {
        off += sprintf(code + off, "%d%s", i, (i == 139) ? "" : ",");
    }
    ASSERT_NOT_NULL(p8_compile((const unsigned char *)code, strlen(code)));
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

TEST(compile_multi_return_three_values_runs_vm) {
    /* Regression: returning >=3 values and binding them all exercised an
     * ebx-clobber in the RETURN copy loop that dropped the 3rd value. */
    const char *code =
        "function trip() return 7,14,21 end\n"
        "local a,b,c = trip()\n"
        "return c";
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
    ASSERT_EQ(21 << 16, vm.value_stack[0].value);
    p8_free_program(prog);
    PASS();
}

TEST(compile_varargs_sum_runs_vm) {
    /* sum(...) folds an arbitrary number of arguments via select-style access
     * using a fixed unpack. Here we forward varargs into locals and add. */
    const char *code =
        "function add3(...)\n"
        "  local a,b,c = ...\n"
        "  return a+b+c\n"
        "end\n"
        "return add3(10,20,12)";
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

TEST(compile_varargs_forwarding_runs_vm) {
    /* g forwards its varargs to f; f returns the first two summed. */
    const char *code =
        "function f(a,b) return a+b end\n"
        "function g(...) return f(...) end\n"
        "return g(30,12)";
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

TEST(compile_varargs_missing_are_nil_runs_vm) {
    /* Requesting more varargs than supplied yields nil for the extras. */
    const char *code =
        "function pick(...)\n"
        "  local a,b,c = ...\n"
        "  if (c == nil) return 99\n"
        "  return 0\n"
        "end\n"
        "return pick(1,2)";
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
    ASSERT_EQ(99 << 16, vm.value_stack[0].value);
    p8_free_program(prog);
    PASS();
}

TEST(compile_varargs_return_single_runs_vm) {
    const char *code =
        "function pt(...) return ... end\n"
        "local x = pt(42)\n"
        "return x";
    P8Program prog;
    const unsigned char *bc;
    unsigned long bc_len;
    P386VMState vm;
    prog = p8_compile((const unsigned char *)code, strlen(code));
    ASSERT_NOT_NULL(prog);
    bc_len = p8_program_bytecode(prog, &bc);
    ASSERT_TRUE(p386_vm_load(&vm, bc, bc_len));
    ASSERT_EQ(P386_VM_HALTED, p386_vm_run(&vm));
    ASSERT_EQ(42 << 16, vm.value_stack[0].value);
    p8_free_program(prog);
    PASS();
}

TEST(compile_varargs_return_spread_runs_vm) {
    /* `return ...` propagates every vararg to the caller. */
    const char *code =
        "function passthru(...) return ... end\n"
        "local x,y,z = passthru(7,14,21)\n"
        "return x,y,z";
    P8Program prog;
    const unsigned char *bc;
    unsigned long bc_len;
    P386VMState vm;
    prog = p8_compile((const unsigned char *)code, strlen(code));
    ASSERT_NOT_NULL(prog);
    bc_len = p8_program_bytecode(prog, &bc);
    ASSERT_TRUE(p386_vm_load(&vm, bc, bc_len));
    ASSERT_EQ(P386_VM_HALTED, p386_vm_run(&vm));
    ASSERT_EQ(7 << 16, vm.value_stack[0].value);
    ASSERT_EQ(14 << 16, vm.value_stack[1].value);
    ASSERT_EQ(21 << 16, vm.value_stack[2].value);
    p8_free_program(prog);
    PASS();
}

TEST(compile_foreach_sums_elements) {
    /* foreach comes from the compiler's Lua prelude; the callback is a Lua
     * closure invoked through a plain CALL inside the prelude's loop. */
    const char *code =
        "local s=0\n"
        "foreach({1,2,3}, function(v) s+=v end)\n"
        "return s";
    P8Program prog;
    const unsigned char *bc;
    unsigned long bc_len;
    P386VMState vm;

    prog = p8_compile((const unsigned char *)code, strlen(code));
    ASSERT_NOT_NULL(prog);
    bc_len = p8_program_bytecode(prog, &bc);
    ASSERT_TRUE(p386_vm_load(&vm, bc, bc_len));
    if (p386_vm_run(&vm) != P386_VM_HALTED) {
        FAIL(vm.error_msg ? vm.error_msg : "vm error");
    }
    ASSERT_EQ(P386_TAG_NUM, vm.value_stack[0].tag);
    ASSERT_EQ(6 << 16, vm.value_stack[0].value);
    p8_free_program(prog);
    PASS();
}

TEST(compile_all_iterates_generic_for) {
    /* `for v in all(t)` exercises TFORCALL's TAG_FUNC iterator path. */
    const char *code =
        "local s=0\n"
        "for v in all({4,5,6}) do s+=v end\n"
        "return s";
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
    ASSERT_EQ(15 << 16, vm.value_stack[0].value);
    p8_free_program(prog);
    PASS();
}

TEST(compile_foreach_del_during_iteration) {
    /* PICO-8 semantics: del(t,v) of the current element during foreach must
     * not skip the following element (the prelude's prev-compare handles
     * the shift-down). 1+2+3 with every element deleted = 6, and the table
     * ends up empty. */
    const char *code =
        "local s=0\n"
        "local t={1,2,3}\n"
        "foreach(t, function(v) s+=v del(t,v) end)\n"
        "return s,#t";
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
    ASSERT_EQ(6 << 16, vm.value_stack[0].value);
    ASSERT_EQ(P386_TAG_NUM, vm.value_stack[1].tag);
    ASSERT_EQ(0, vm.value_stack[1].value);
    p8_free_program(prog);
    PASS();
}

TEST(compile_ipairs_yields_index_and_value) {
    const char *code =
        "local s=0\n"
        "for i,v in ipairs({7,8}) do s+=i+v end\n"
        "return s";
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
    ASSERT_EQ(18 << 16, vm.value_stack[0].value);
    p8_free_program(prog);
    PASS();
}

TEST(compile_comment_headed_cart_with_prelude) {
    /* Regression guard: prepending the prelude must not break carts whose
     * source starts with header comment lines. */
    const char *code =
        "-- my cart\n"
        "-- by author\n"
        "x = 12\n"
        "return x";
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
    ASSERT_EQ(12 << 16, vm.value_stack[0].value);
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

/* Compile + run `code` as the body of a function and return its first
 * result in *out. The wrapper `local r = (function() ... end)()` puts the
 * result in the main chunk's R0 (a plain `return f()` would leave the
 * callee in R0). Returns 0 on success, or a message on failure. */
static const char *run_chunk(const char *code, P386VMState *vm, P386Value *out) {
    static const char head[] = "local r = (function()\n";
    static const char tail[] = "\nend)()";
    char *src;
    P8Program prog;
    const unsigned char *bc;
    unsigned long bc_len;

    src = (char *)malloc(strlen(head) + strlen(code) + strlen(tail) + 1);
    if (!src) return "out of memory";
    strcpy(src, head);
    strcat(src, code);
    strcat(src, tail);
    prog = p8_compile((const unsigned char *)src, strlen(src));
    free(src);
    if (!prog) return "compile failed";
    bc_len = p8_program_bytecode(prog, &bc);
    if (!p386_vm_load(vm, bc, bc_len)) return "load failed";
    if (p386_vm_run(vm) != P386_VM_HALTED) {
        return vm->error_msg ? vm->error_msg : "vm error";
    }
    *out = vm->value_stack[0];
    /* prog is kept alive: the VM's closures reference its bytecode. */
    return 0;
}

TEST(compile_builtin_result_feeds_last_arg) {
    /* abs() is the last argument, so CALL asks it for "all" results
     * (want_rets 0). It must still return its value. */
    P386VMState vm;
    P386Value r;
    const char *err = run_chunk("return flr(abs(-5.5))", &vm, &r);
    if (err) FAIL(err);
    ASSERT_EQ(P386_TAG_NUM, r.tag);
    ASSERT_EQ(5 << 16, r.value);
    PASS();
}

/* Run `return <expr>` and check that it gives the BOOL `want`. */
static int str_cmp_is(const char *expr, int want) {
    P386VMState vm;
    P386Value r;
    char code[160];
    const char *err;
    sprintf(code, "return %s", expr);
    err = run_chunk(code, &vm, &r);
    if (err) return 0;
    return r.tag == P386_TAG_BOOL && (r.value != 0) == want;
}

TEST(compile_string_compare) {
    ASSERT_EQ(1, str_cmp_is("\"a\" < \"b\"", 1));
    ASSERT_EQ(1, str_cmp_is("\"b\" < \"a\"", 0));
    ASSERT_EQ(1, str_cmp_is("\"abc\" < \"abd\"", 1));
    ASSERT_EQ(1, str_cmp_is("\"ab\" < \"abc\"", 1));     /* prefix */
    ASSERT_EQ(1, str_cmp_is("\"abc\" < \"ab\"", 0));
    ASSERT_EQ(1, str_cmp_is("\"b\" > \"a\"", 1));
    ASSERT_EQ(1, str_cmp_is("\"a\" > \"a\"", 0));
    ASSERT_EQ(1, str_cmp_is("\"a\" <= \"a\"", 1));
    ASSERT_EQ(1, str_cmp_is("\"b\" <= \"a\"", 0));
    ASSERT_EQ(1, str_cmp_is("\"a\" >= \"a\"", 1));
    ASSERT_EQ(1, str_cmp_is("\"a\" >= \"b\"", 0));
    ASSERT_EQ(1, str_cmp_is("\"\" < \"a\"", 1));         /* empty */
    ASSERT_EQ(1, str_cmp_is("\"a\" < \"\"", 0));
    ASSERT_EQ(1, str_cmp_is("\"\" <= \"\"", 1));
    ASSERT_EQ(1, str_cmp_is("\"\" < \"\"", 0));
    ASSERT_EQ(1, str_cmp_is("\"a\" < \"\\x80\"", 1));    /* unsigned bytes */
    ASSERT_EQ(1, str_cmp_is("\"\\xff\" > \"z\"", 1));
    PASS();
}

TEST(compile_string_compare_mixed_traps) {
    P386VMState vm;
    P386Value r;
    ASSERT_EQ(1, run_chunk("return 1 < \"2\"", &vm, &r) != 0);
    ASSERT_EQ(1, run_chunk("return \"1\" < 2", &vm, &r) != 0);
    ASSERT_EQ(1, run_chunk("return \"1\" >= nil", &vm, &r) != 0);
    ASSERT_EQ(1, run_chunk("return {} <= \"a\"", &vm, &r) != 0);
    PASS();
}

TEST(compile_string_insertion_sort) {
    P386VMState vm;
    P386Value r;
    const char *err = run_chunk(
        "local t = {\"mia\", \"bob\", \"al\", \"bobby\", \"\", \"zed\"}\n"
        "for i = 2, #t do\n"
        "  local v, j = t[i], i - 1\n"
        "  while j >= 1 and t[j] > v do t[j + 1] = t[j] j -= 1 end\n"
        "  t[j + 1] = v\n"
        "end\n"
        "return t[1] == \"\" and t[2] == \"al\" and t[3] == \"bob\""
        " and t[4] == \"bobby\" and t[5] == \"mia\" and t[6] == \"zed\"",
        &vm, &r);
    if (err) FAIL(err);
    ASSERT_EQ(P386_TAG_BOOL, r.tag);
    ASSERT_EQ(1, r.value);
    PASS();
}

TEST(vga_screen_pal_maps_to_dac_rgb) {
    uint8_t pal[16], rgb[48];
    int i;
    for (i = 0; i < 16; i++) pal[i] = (uint8_t)i;
    pal[1] = 8;                         /* standard color */
    pal[2] = 128;                       /* secret color 128 -> DAC 16 */
    pal[3] = 0x9F;                      /* 143 -> DAC 31 */
    pal[4] = 0x7C;                      /* bits 4-6 masked: -> 12 */
    vga_screen_pal_to_rgb6(pal, rgb);
    ASSERT_EQ(0, memcmp(&rgb[0], &p8_palette_rgb6[0], 3));
    ASSERT_EQ(0, memcmp(&rgb[1 * 3], &p8_palette_rgb6[8 * 3], 3));
    ASSERT_EQ(0, memcmp(&rgb[2 * 3], &p8_palette_rgb6[16 * 3], 3));
    ASSERT_EQ(0, memcmp(&rgb[3 * 3], &p8_palette_rgb6[31 * 3], 3));
    ASSERT_EQ(0, memcmp(&rgb[4 * 3], &p8_palette_rgb6[12 * 3], 3));
    PASS();
}

/* Fill sprite n with solid color c. */
static void fill_sprite(int n, uint8_t c) {
    int y;
    for (y = 0; y < 8; y++) {
        memset(&p8_ram.mem.gfx[((n >> 4) * 8 + y) * 64 + (n & 15) * 4],
               (c << 4) | c, 4);
    }
}

TEST(compile_gfx_spr_and_map_draw) {
    P386VMState vm;
    P386Value r;
    const char *err;
    p8_ram_init();
    fill_sprite(1, 9);
    fill_sprite(2, 12);
    err = run_chunk(
        "cls(0)\n"
        "spr(1, 10, 20)\n"                     /* 8x8 of color 9 at 10,20 */
        "mset(0, 0, 2) mset(1, 0, 0)\n"
        "map(0, 0, 64, 64, 2, 1)\n"            /* tile 2 at 64,64; tile 0 skipped */
        "return mget(0, 0)", &vm, &r);
    if (err) FAIL(err);
    ASSERT_EQ(2 << 16, r.value);
    ASSERT_EQ(0x99, p8_ram.mem.screen[20 * 64 + 5]);    /* x 10-11 */
    ASSERT_EQ(0x99, p8_ram.mem.screen[27 * 64 + 8]);    /* x 16-17, last row */
    ASSERT_EQ(0x00, p8_ram.mem.screen[28 * 64 + 5]);    /* below sprite */
    ASSERT_EQ(0xCC, p8_ram.mem.screen[64 * 64 + 32]);   /* map tile */
    ASSERT_EQ(0x00, p8_ram.mem.screen[64 * 64 + 36]);   /* tile 0 cell */
    PASS();
}

TEST(compile_gfx_multi_value_returns) {
    P386VMState vm;
    P386Value r;
    const char *err;
    p8_ram_init();
    err = run_chunk("camera(5, 6) local x, y = camera() return x*10 + y", &vm, &r);
    if (err) FAIL(err);
    ASSERT_EQ(56 << 16, r.value);
    err = run_chunk("clip(1, 2, 3, 4) local a, b, c, d = clip()"
                    " return a + b*10 + c*100 + d*1000", &vm, &r);
    if (err) FAIL(err);
    ASSERT_EQ(6421 << 16, r.value);
    err = run_chunk("pal(1, 2) return pal(1, 3)", &vm, &r);
    if (err) FAIL(err);
    ASSERT_EQ(2 << 16, r.value);
    err = run_chunk("fset(7, 3, true) if fget(7, 3) then return fget(7) end"
                    " return -1", &vm, &r);
    if (err) FAIL(err);
    ASSERT_EQ(8 << 16, r.value);
    PASS();
}

TEST(compile_many_globals_past_old_limit) {
    /* 400 user globals: the old 8-bit slots allowed only 163. Every global
     * gets 0. */
    static char code[400 * 16 + 64];
    char *w = code;
    int i;
    P386VMState vm;
    P386Value r;
    const char *err;
    for (i = 1; i <= 400; i++) w += sprintf(w, "g%d=0 ", i);
    sprintf(w, "g1=1 g200=200 g400=400 return g1 + g200 + g400");
    err = run_chunk(code, &vm, &r);
    if (err) FAIL(err);
    ASSERT_EQ(P386_TAG_NUM, r.tag);
    ASSERT_EQ(601 << 16, r.value);
    PASS();
}

/* ---- metatables ---------------------------------------------------- */

/* Run code (a function body) and check that it returns the integer want. */
static const char *expect_int(const char *code, int32_t want) {
    static char msg[96];
    P386VMState vm;
    P386Value r;
    const char *err = run_chunk(code, &vm, &r);
    if (err) return err;
    if (r.tag != P386_TAG_NUM) return "result is not a number";
    if (r.value != (want << 16)) {
        sprintf(msg, "got %ld, want %ld", (long)(r.value >> 16), (long)want);
        return msg;
    }
    return 0;
}

#define EXPECT_INT(code, want) do { \
    const char *e_ = expect_int(code, want); \
    if (e_) FAIL(e_); \
} while (0)

TEST(meta_index_table_class) {
    EXPECT_INT(
        "local base={} base.__index=base\n"
        "function base.new(x) return setmetatable({x=x},base) end\n"
        "function base:get() return self.x*2 end\n"
        "local o=base.new(21) return o:get()", 42);
    /* Three levels of inheritance. */
    EXPECT_INT(
        "local a={} a.__index=a function a.f() return 7 end\n"
        "local b=setmetatable({},a) b.__index=b\n"
        "local c=setmetatable({},b) c.__index=c\n"
        "local o=setmetatable({},c) return o.f()+o.f()", 14);
    /* A miss with no __index gives nil; rawget ignores __index. */
    EXPECT_INT("local t=setmetatable({},{}) return t.x==nil and 1 or 0", 1);
    EXPECT_INT("local t=setmetatable({},{__index=function() return 1 end})\n"
               "return rawget(t,'x')==nil and 1 or 0", 1);
    PASS();
}

TEST(meta_index_function) {
    /* GETTABLE (number key) and GETFIELD (string key) paths. */
    EXPECT_INT("local t=setmetatable({}, {__index=function(t,k) return k*3 end})\n"
               "return t[5]+t[1]", 18);
    EXPECT_INT("local t=setmetatable({}, {__index=function(t,k) return #k end})\n"
               "local n=t.hello return n", 5);
    /* The destination register is also the table register. */
    EXPECT_INT("local t=setmetatable({}, {__index=function(t,k) return 9 end})\n"
               "t=t.x return t", 9);
    PASS();
}

TEST(meta_index_cycle_traps) {
    P386VMState vm;
    P386Value r;
    const char *err = run_chunk(
        "local a,b={},{} setmetatable(a,{__index=b}) setmetatable(b,{__index=a})\n"
        "return a.x", &vm, &r);
    if (!err) FAIL("expected an error");
    ASSERT_EQ(P386_VM_ERR_TYPE, vm.status);
    PASS();
}

TEST(meta_newindex) {
    /* Function: only new keys go through it. Its results are discarded and
     * do not change live registers. */
    EXPECT_INT(
        "local x,y=1,2\n"
        "local t=setmetatable({}, {__newindex=function(t,k,v) rawset(t,k,v*2) return 99 end})\n"
        "t.a=5 t.a=7 t[3]=1 return t.a+t[3]+x+y", 12);
    /* Table: the write goes to the other table. */
    EXPECT_INT("local store={} local t=setmetatable({},{__newindex=store})\n"
               "t.k=4 t[2]=3 return (rawget(t,'k')==nil and 1 or 0)+store.k+store[2]", 8);
    PASS();
}

TEST(meta_arith_vector) {
    EXPECT_INT(
        "local v={} v.__index=v\n"
        "v.__add=function(a,b) return setmetatable({x=a.x+b.x},v) end\n"
        "v.__mul=function(a,s) return setmetatable({x=a.x*s},v) end\n"
        "v.__unm=function(a) return setmetatable({x=-a.x},v) end\n"
        "v.__sub=function(a,b) return a+(-b) end\n"
        "local a=setmetatable({x=3},v) local b=setmetatable({x=4},v)\n"
        "local c=(a+b)*2-a return c.x", 11);
    /* The handler can come from the right operand. */
    EXPECT_INT(
        "local m=setmetatable({},{__add=function(p,q)\n"
        "  return (type(p)=='number' and p or 0)+(type(q)=='number' and q or 0) end})\n"
        "return (5+m)+(m+6)", 11);
    EXPECT_INT("local m=setmetatable({},{__div=function() return 1 end,\n"
               " __mod=function() return 2 end, __pow=function() return 4 end,\n"
               " __idiv=function() return 8 end})\n"
               "return m/1 + m%1 + m^2 + m\\1", 15);
    PASS();
}

TEST(meta_arith_without_handler_traps) {
    P386VMState vm;
    P386Value r;
    const char *err = run_chunk("local t={} return t+1", &vm, &r);
    if (!err) FAIL("expected an error");
    ASSERT_EQ(P386_VM_ERR_TYPE, vm.status);
    err = run_chunk("local t=setmetatable({},{}) return -t", &vm, &r);
    if (!err) FAIL("expected an error");
    ASSERT_EQ(P386_VM_ERR_TYPE, vm.status);
    PASS();
}

TEST(meta_eq) {
    EXPECT_INT(
        "local mt={__eq=function(a,b) return a.id==b.id end}\n"
        "local a=setmetatable({id=1},mt) local b=setmetatable({id=1},mt)\n"
        "local c=setmetatable({id=2},mt) local r=0\n"
        "if a==b then r+=1 end if a~=c then r+=10 end\n"
        "if not (a~=b) then r+=100 end if a==a then r+=1000 end\n"
        "if {}=={} then r+=10000 end return r", 1111);
    /* A result that is not a boolean is made one. */
    EXPECT_INT("local mt={__eq=function() return 5 end}\n"
               "local a,b=setmetatable({},mt),setmetatable({},mt)\n"
               "local e=a==b local n=a~=b\n"
               "return (e==true and 1 or 0)+(n==false and 2 or 0)", 3);
    PASS();
}

TEST(meta_len_concat_call) {
    EXPECT_INT("local t=setmetatable({1,2,3},{__len=function() return 42 end})\n"
               "return #t + #setmetatable({1,2},{})", 44);
    EXPECT_INT("local t=setmetatable({},{__concat=function(a,b) return 'xy' end})\n"
               "local s=t..'a' return #s + #('a'..t)", 4);
    EXPECT_INT("local t=setmetatable({n=5},{__call=function(self,a,b) return self.n+a+b end})\n"
               "return t(1,2)", 8);
    /* Tail call and several results. */
    EXPECT_INT("local t=setmetatable({},{__call=function(self,a) return a,a*2 end})\n"
               "local function f(x) return t(x) end\n"
               "local p,q=f(3) local u,w=t(4) return p+q+u+w", 21);
    PASS();
}

TEST(meta_get_set_metatable) {
    EXPECT_INT("local mt={} local t=setmetatable({},mt)\n"
               "return (getmetatable(t)==mt and 1 or 0)+(getmetatable({})==nil and 2 or 0)", 3);
    /* setmetatable(t, nil) removes it. */
    EXPECT_INT("local t=setmetatable({},{__index=function() return 1 end})\n"
               "setmetatable(t,nil) return t.x==nil and 1 or 0", 1);
    PASS();
}


TEST(compile_constructor_constant_fields) {
    /* {name=constant} emits SETFIELD with a constant RK value. */
    EXPECT_INT("local t={id=2,name='abc',f=true,g=false}\n"
               "return t.id + #t.name + (t.f and 10 or 0) + (t.g==false and 100 or 0)", 115);
    PASS();
}

TEST(compile_many_number_constants) {
    /* 450 distinct numbers, used as RK operands in arithmetic and
     * comparisons. Indices past 127 go through LOADK into a temp. */
    static char code[450 * 64 + 64];
    char *w = code;
    int i;
    P386VMState vm;
    P386Value r;
    const char *err;
    w += sprintf(w, "local s,c=0,0 ");
    for (i = 1; i <= 450; i++) {
        w += sprintf(w, "s=s+%d if s==%d then c=c+1 end if s<%d then c=c+1 end s=s-%d ",
                     i, i, i + 1, i);
    }
    sprintf(w, "return c*10 + s");
    err = run_chunk(code, &vm, &r);
    if (err) FAIL(err);
    ASSERT_EQ(P386_TAG_NUM, r.tag);
    ASSERT_EQ(9000 << 16, r.value);
    PASS();
}

TEST(compile_many_field_names) {
    /* 320 distinct field names: GETFIELD/SETFIELD only reach name 255.
     * Higher names use GETTABLE/SETTABLE with a LOADK key. */
    static char code[320 * 48 + 256];
    char *w = code;
    int i;
    P386VMState vm;
    P386Value r;
    const char *err;
    w += sprintf(w, "local t={} ");
    for (i = 1; i <= 320; i++) w += sprintf(w, "t.f%d=%d ", i, i % 7);
    w += sprintf(w, "t.f300=t.f300+t.f1 t.f2=t.f319 local s=0 ");
    for (i = 1; i <= 320; i++) w += sprintf(w, "s=s+t.f%d ", i);
    sprintf(w, "return s");
    err = run_chunk(code, &vm, &r);
    if (err) FAIL(err);
    ASSERT_EQ(P386_TAG_NUM, r.tag);
    {
        int expect = 0;
        for (i = 1; i <= 320; i++) expect += i % 7;
        expect += 1;                    /* f300 += f1 */
        expect += (319 % 7) - 2;        /* f2 = f319 */
        ASSERT_EQ(expect << 16, r.value);
    }
    PASS();
}

TEST(compile_high_index_method_call) {
    /* The method name and the dotted function name come after 300
     * other constants, so their indices are above 255. */
    static char code[300 * 24 + 512];
    char *w = code;
    int i;
    P386VMState vm;
    P386Value r;
    const char *err;
    w += sprintf(w, "local t={base=10} ");
    for (i = 1; i <= 300; i++) w += sprintf(w, "t.n%d=%d ", i, i % 5);
    w += sprintf(w, "function t:hi(a) return self.base + a end ");
    w += sprintf(w, "t.sub={} function t.sub.deep(a) return a*2 end ");
    sprintf(w, "return t:hi(5) + t.sub.deep(3)");
    err = run_chunk(code, &vm, &r);
    if (err) FAIL(err);
    ASSERT_EQ(P386_TAG_NUM, r.tag);
    ASSERT_EQ(21 << 16, r.value);
    PASS();
}

TEST(compile_big_table_constructor) {
    /* Named and positional entries, with distinct constants for both
     * the names and the values. */
    static char code[300 * 40 + 256];
    char *w = code;
    int i;
    P386VMState vm;
    P386Value r;
    const char *err;
    w += sprintf(w, "local t={");
    for (i = 1; i <= 300; i++) w += sprintf(w, "k%d=%d,%d,", i, 1000 + i, 2000 + i);
    sprintf(w, "} return t.k1 + t.k300 + t[1] + t[300] + t.k270");
    err = run_chunk(code, &vm, &r);
    if (err) FAIL(err);
    ASSERT_EQ(P386_TAG_NUM, r.tag);
    ASSERT_EQ((1001 + 1300 + 2001 + 2300 + 1270) << 16, r.value);
    PASS();
}

TEST(meta_lt_le) {
    /* a > b is __lt(b, a); a >= b is __le(b, a). */
    EXPECT_INT(
        "local mt={__lt=function(a,b) return a.v<b.v end,\n"
        "          __le=function(a,b) return a.v<=b.v end}\n"
        "local a,b=setmetatable({v=1},mt),setmetatable({v=2},mt) local r=0\n"
        "if a<b then r+=1 end if b>a then r+=2 end if not (b<a) then r+=4 end\n"
        "if a<=a then r+=8 end if b>=a then r+=16 end\n"
        "if not (a>=b) then r+=32 end return r", 63);
    /* Sort objects with a < handler. */
    EXPECT_INT(
        "local mt={__lt=function(a,b) return a.v<b.v end}\n"
        "local t={} for v in all({5,3,9,1}) do add(t,setmetatable({v=v},mt)) end\n"
        "for i=2,#t do local j=i while j>1 and t[j]<t[j-1] do\n"
        "  t[j],t[j-1]=t[j-1],t[j] j-=1 end end\n"
        "return t[1].v*1000+t[2].v*100+t[3].v*10+t[4].v", 1359);
    PASS();
}
