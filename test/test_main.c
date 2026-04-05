#include "test.h"

/*
 * Test declarations — each test file provides TEST(name) functions.
 * We include the test source files directly so everything links
 * as a single translation unit (avoids wcc386 multi-obj headaches
 * with static test counters).
 */
#include "test_compress.c"
#include "test_compile.c"

void main() {
    TEST_INIT();

    /* Compression tests */
    RUN_TEST(decompress_mini_empty);
    RUN_TEST(decompress_mini_literals_only);
    RUN_TEST(decompress_mini_raw_literal);
    RUN_TEST(decompress_mini_corrupt_length);
    RUN_TEST(decompress_unknown_format);
    RUN_TEST(compress_roundtrip_simple);
    RUN_TEST(compress_roundtrip_repeated);

    /* Compiler tests */
    RUN_TEST(parse_empty_program);
    RUN_TEST(parse_simple_assignment);
    RUN_TEST(parse_function_def);
    RUN_TEST(parse_pico8_shorthand_if);
    RUN_TEST(parse_pico8_shorthand_print);
    RUN_TEST(parse_pico8_compound_assign);
    RUN_TEST(parse_pico8_bitwise_ops);
    RUN_TEST(parse_pico8_shift_ops);
    RUN_TEST(parse_for_loop);
    RUN_TEST(parse_for_in_loop);
    RUN_TEST(parse_repeat_until);
    RUN_TEST(parse_table_constructor);
    RUN_TEST(parse_nested_functions);
    RUN_TEST(parse_invalid_syntax);
    RUN_TEST(parse_null_input);
    RUN_TEST(compile_empty);
    RUN_TEST(compile_hello_world);
    RUN_TEST(compile_arithmetic);
    RUN_TEST(compile_game_loop);
    RUN_TEST(compile_invalid_returns_null);
    RUN_TEST(compile_free_null_safe);

    TEST_REPORT();
}
