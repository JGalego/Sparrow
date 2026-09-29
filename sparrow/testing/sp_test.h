#ifndef SPARROW_TESTING_TEST_H
#define SPARROW_TESTING_TEST_H

#include <math.h>
#include <stdio.h>

/*
 * Minimal unit-test harness for C code.
 *
 *   SP_TEST(name, "REQ-001,REQ-002") { SP_ASSERT(x == 1); }
 *
 * The second argument lists the requirements the test verifies ("" if none).
 * Tests register themselves at load time (GCC/Clang constructor attribute).
 * The runner in sp_test_main.c executes them in registration order and can
 * write a JUnit XML report with --junit <file>. A failed assertion aborts
 * the current test only.
 */

typedef struct SpTestCase {
    const char *suite;
    const char *name;
    const char *requirements;
    void (*run)(void);
    struct SpTestCase *next;
} SpTestCase;

void sp_test_register(SpTestCase *test_case);
void sp_test_fail(const char *file, int line, const char *expression);

#define SP_TEST(test_name, requirements_list)                                                      \
    static void test_##test_name(void);                                                            \
    static SpTestCase case_##test_name = {SP_TEST_SUITE, #test_name, requirements_list,            \
                                          test_##test_name, NULL};                                 \
    __attribute__((constructor)) static void register_##test_name(void)                            \
    {                                                                                              \
        sp_test_register(&case_##test_name);                                                       \
    }                                                                                              \
    static void test_##test_name(void)

#define SP_ASSERT(condition)                                                                       \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            sp_test_fail(__FILE__, __LINE__, #condition);                                          \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define SP_ASSERT_EQ_INT(expected, actual)                                                         \
    do {                                                                                           \
        long long expected_value_ = (long long)(expected);                                         \
        long long actual_value_ = (long long)(actual);                                             \
        if (expected_value_ != actual_value_) {                                                    \
            char message_[160];                                                                    \
            snprintf(message_, sizeof message_, "%s == %s (expected %lld, got %lld)", #expected,   \
                     #actual, expected_value_, actual_value_);                                     \
            sp_test_fail(__FILE__, __LINE__, message_);                                            \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define SP_ASSERT_NEAR(expected, actual, tolerance)                                                \
    do {                                                                                           \
        double expected_value_ = (double)(expected);                                               \
        double actual_value_ = (double)(actual);                                                   \
        if (!(fabs(expected_value_ - actual_value_) <= (double)(tolerance))) {                     \
            char message_[160];                                                                    \
            snprintf(message_, sizeof message_, "%s ~= %s (expected %g, got %g)", #expected,       \
                     #actual, expected_value_, actual_value_);                                     \
            sp_test_fail(__FILE__, __LINE__, message_);                                            \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#endif
