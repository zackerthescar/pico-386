#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "rust.h"

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
