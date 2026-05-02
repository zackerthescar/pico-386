#include "test.h"
#include "test_vm.c"

void main() {
    TEST_INIT();
    RUN_TEST(vm_loader_rejects_bad_magic);
    RUN_TEST(vm_loadk_add_return_constants);
    RUN_TEST(vm_move_booleans_and_loadn);
    RUN_TEST(vm_globals_round_trip);
    RUN_TEST(vm_branch_false_skips_poison);
    RUN_TEST(vm_not_and_comparisons);
    RUN_TEST(vm_type_trap_add_bool);
    RUN_TEST(vm_division_by_zero_traps);
    TEST_REPORT();
}
