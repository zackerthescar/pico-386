#include "test.h"
#include "test_vm.c"

void main() {
    TEST_INIT();
    RUN_TEST(vm_loader_rejects_bad_magic);
    RUN_TEST(vm_loader_rejects_bad_version);
    RUN_TEST(vm_loader_rejects_proto_table_before_header_end);
    RUN_TEST(vm_loader_rejects_proto_table_misaligned);
    RUN_TEST(vm_loader_rejects_string_table_before_proto_end);
    RUN_TEST(vm_loader_rejects_string_table_misaligned);
    RUN_TEST(vm_loader_rejects_bytecode_section_before_string_table_end);
    RUN_TEST(vm_loader_rejects_bytecode_section_misaligned);
    RUN_TEST(vm_loader_rejects_unaligned_code_len);
    RUN_TEST(vm_loader_rejects_code_out_of_range);
    RUN_TEST(vm_loader_rejects_consts_before_aligned_code_end);
    RUN_TEST(vm_loader_rejects_consts_out_of_range);
    RUN_TEST(vm_loader_rejects_upvalues_before_consts_end);
    RUN_TEST(vm_loader_rejects_upvalues_out_of_range);
    RUN_TEST(vm_loader_rejects_zero_registers);
    RUN_TEST(vm_loader_rejects_string_out_of_range);
    RUN_TEST(vm_vm_load_sets_bad_bc_status);
    RUN_TEST(vm_loadk_add_return_constants);
    RUN_TEST(vm_loadk_out_of_bounds_traps);
    RUN_TEST(vm_rk_const_out_of_bounds_traps);
    RUN_TEST(vm_move_booleans_and_loadn);
    RUN_TEST(vm_globals_round_trip);
    RUN_TEST(vm_branch_false_skips_poison);
    RUN_TEST(vm_not_and_comparisons);
    RUN_TEST(vm_type_trap_add_bool);
    RUN_TEST(vm_division_by_zero_traps);
    RUN_TEST(vm_idiv_mod_truncate);
    RUN_TEST(vm_pow_integer_exponent);
    RUN_TEST(vm_bitwise_and_or_xor_not);
    RUN_TEST(vm_shifts_and_rotates);
    RUN_TEST(vm_idiv_div_zero_traps);
    TEST_REPORT();
}
