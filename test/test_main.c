#include "test.h"
#include "mem.h"

P8Ram p8_ram;

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
    RUN_TEST(parse_long_string_eq_level);
    RUN_TEST(parse_long_comment_eq_level);
    RUN_TEST(parse_tilde_is_binary_xor);
    RUN_TEST(parse_deeply_nested_tables_no_blowup);
    RUN_TEST(parse_short_if_stops_at_newline);
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
    RUN_TEST(compile_print_runs_vm);
    RUN_TEST(compile_arithmetic_global_runs_vm);
    RUN_TEST(compile_boolean_short_circuit_runs_vm);
    RUN_TEST(compile_multi_arg_call_preserves_args);
    RUN_TEST(compile_local_scope_shadowing_runs_vm);
    RUN_TEST(compile_multi_assign_rhs_first_runs_vm);
    RUN_TEST(compile_constants_past_old_limit_compile);
    RUN_TEST(compile_branch_local_scope_does_not_leak);
    RUN_TEST(compile_lua_function_call_runs_vm);
    RUN_TEST(compile_multi_return_three_values_runs_vm);
    RUN_TEST(compile_varargs_sum_runs_vm);
    RUN_TEST(compile_varargs_forwarding_runs_vm);
    RUN_TEST(compile_varargs_missing_are_nil_runs_vm);
    RUN_TEST(compile_varargs_return_single_runs_vm);
    RUN_TEST(compile_varargs_return_spread_runs_vm);
    RUN_TEST(compile_local_function_call_runs_vm);
    RUN_TEST(compile_function_literal_call_runs_vm);
    RUN_TEST(compile_nested_noncapturing_function_call_runs_vm);
    RUN_TEST(compile_captured_local_runs_vm);
    RUN_TEST(compile_foreach_sums_elements);
    RUN_TEST(compile_all_iterates_generic_for);
    RUN_TEST(compile_foreach_del_during_iteration);
    RUN_TEST(compile_ipairs_yields_index_and_value);
    RUN_TEST(compile_comment_headed_cart_with_prelude);
    RUN_TEST(compile_lifecycle_slots_and_host_call_draw_pixels);
    RUN_TEST(compile_builtin_result_feeds_last_arg);
    RUN_TEST(compile_string_compare);
    RUN_TEST(compile_string_compare_mixed_traps);
    RUN_TEST(compile_string_insertion_sort);
    RUN_TEST(vga_screen_pal_maps_to_dac_rgb);
    RUN_TEST(compile_gfx_spr_and_map_draw);
    RUN_TEST(compile_gfx_multi_value_returns);
    RUN_TEST(compile_many_globals_past_old_limit);
    RUN_TEST(meta_index_table_class);
    RUN_TEST(meta_index_function);
    RUN_TEST(meta_index_cycle_traps);
    RUN_TEST(meta_newindex);
    RUN_TEST(meta_arith_vector);
    RUN_TEST(meta_arith_without_handler_traps);
    RUN_TEST(meta_eq);
    RUN_TEST(meta_len_concat_call);
    RUN_TEST(meta_get_set_metatable);
    RUN_TEST(meta_lt_le);
    RUN_TEST(cond_jumps_truth_table);
    RUN_TEST(getfield_inline_cache);
    RUN_TEST(gc_garbage_loop_stays_bounded);
    RUN_TEST(gc_collects_cycles);
    RUN_TEST(gc_keeps_live_data);
    RUN_TEST(gc_stat0_reports_memory);
    RUN_TEST(gc_stress_programs);
    RUN_TEST(gc_host_callbacks_keep_state);
    RUN_TEST(co_programs);
    RUN_TEST(co_programs_gc_stress);
    RUN_TEST(co_yield_outside_coroutine_traps);
    RUN_TEST(co_memory_stays_bounded);
    RUN_TEST(co_host_frames_wait_pattern);
    RUN_TEST(builtin_error_keeps_its_message);
    RUN_TEST(cart_glyph_bytes_compile);
    RUN_TEST(cart_t_is_separate_from_time);
    RUN_TEST(cart_menuitem_and_cartdata);
    RUN_TEST(intrinsics_match_builtins);
    RUN_TEST(intrinsics_respect_redefinition);
    RUN_TEST(table_programs);
    RUN_TEST(table_programs_gc_stress);
    RUN_TEST(gc_scans_caller_registers_above_callee_frame);
    RUN_TEST(call_args_in_place);
    RUN_TEST(compile_constructor_constant_fields);
    RUN_TEST(compile_constant_unary_and_concat_operands);
    RUN_TEST(compile_many_number_constants);
    RUN_TEST(compile_many_field_names);
    RUN_TEST(compile_high_index_method_call);
    RUN_TEST(compile_big_table_constructor);

    TEST_REPORT();
}
