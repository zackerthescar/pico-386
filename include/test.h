#ifndef _TEST_H
#define _TEST_H

#include <string.h>
#include "serial.h"

/*
 * Minimal test framework for DOS4GW.
 * Output goes over COM2 serial in a simple pass/fail format.
 *
 * Usage:
 *   TEST(name_of_test) {
 *       ASSERT_TRUE(1 + 1 == 2);
 *       ASSERT_EQ(42, 42);
 *       ASSERT_STR_EQ("hello", "hello");
 *       PASS();
 *   }
 *
 *   void main() {
 *       TEST_INIT();
 *       RUN_TEST(name_of_test);
 *       TEST_REPORT();
 *   }
 */

/* Global counters */
static int _test_total;
static int _test_passed;
static int _test_failed;
static int _test_current_failed;

#define TEST_INIT() do { \
    debug_serial_init(); \
    _test_total = 0; \
    _test_passed = 0; \
    _test_failed = 0; \
    debug_serial_print("TAP version 13\n"); \
} while(0)

#define RUN_TEST(name) do { \
    _test_total++; \
    _test_current_failed = 0; \
    test_##name(); \
    if (_test_current_failed) { \
        _test_failed++; \
        debug_serial_printf("not ok %d - %s\n", _test_total, #name); \
    } \
} while(0)

#define TEST_REPORT() do { \
    debug_serial_printf("1..%d\n", _test_total); \
    debug_serial_printf("# passed: %d\n", _test_passed); \
    debug_serial_printf("# failed: %d\n", _test_failed); \
    if (_test_failed == 0) { \
        debug_serial_print("# ALL TESTS PASSED\n"); \
    } else { \
        debug_serial_print("# SOME TESTS FAILED\n"); \
    } \
} while(0)

#define TEST(name) static void test_##name()

#define PASS() do { \
    if (!_test_current_failed) { \
        _test_passed++; \
        debug_serial_printf("ok %d - %s\n", _test_total, __func__ + 5); \
    } \
} while(0)

#define FAIL(msg) do { \
    _test_current_failed = 1; \
    debug_serial_printf("# FAIL at %s:%d: %s\n", __FILE__, __LINE__, msg); \
} while(0)

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        FAIL(#expr); \
        return; \
    } \
} while(0)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define ASSERT_EQ(expected, actual) do { \
    int _e = (int)(expected); \
    int _a = (int)(actual); \
    if (_e != _a) { \
        debug_serial_printf("# FAIL at %s:%d: expected %d, got %d\n", \
            __FILE__, __LINE__, _e, _a); \
        _test_current_failed = 1; \
        return; \
    } \
} while(0)

#define ASSERT_NEQ(expected, actual) do { \
    int _e = (int)(expected); \
    int _a = (int)(actual); \
    if (_e == _a) { \
        debug_serial_printf("# FAIL at %s:%d: expected != %d, got %d\n", \
            __FILE__, __LINE__, _e, _a); \
        _test_current_failed = 1; \
        return; \
    } \
} while(0)

#define ASSERT_NOT_NULL(ptr) do { \
    if ((ptr) == 0) { \
        debug_serial_printf("# FAIL at %s:%d: expected non-NULL\n", \
            __FILE__, __LINE__); \
        _test_current_failed = 1; \
        return; \
    } \
} while(0)

#define ASSERT_NULL(ptr) do { \
    if ((ptr) != 0) { \
        debug_serial_printf("# FAIL at %s:%d: expected NULL\n", \
            __FILE__, __LINE__); \
        _test_current_failed = 1; \
        return; \
    } \
} while(0)

#define ASSERT_STR_EQ(expected, actual) do { \
    const char *_e = (const char *)(expected); \
    const char *_a = (const char *)(actual); \
    if (strcmp(_e, _a) != 0) { \
        debug_serial_printf("# FAIL at %s:%d: strings differ\n", \
            __FILE__, __LINE__); \
        debug_serial_printf("#   expected: %s\n", _e); \
        debug_serial_printf("#   actual:   %s\n", _a); \
        _test_current_failed = 1; \
        return; \
    } \
} while(0)

#define ASSERT_MEM_EQ(expected, actual, len) do { \
    if (memcmp((expected), (actual), (len)) != 0) { \
        debug_serial_printf("# FAIL at %s:%d: memory differs (%d bytes)\n", \
            __FILE__, __LINE__, (int)(len)); \
        _test_current_failed = 1; \
        return; \
    } \
} while(0)

#endif
