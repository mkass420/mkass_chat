#ifndef UNIT_TEST_H
#define UNIT_TEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*TestFunction)(void);

typedef struct {
    const char*  name;
    TestFunction function;
} TestCase;

typedef struct {
    TestCase cases[128];
    size_t   count;
} TestSuite;

void test_suite_add(TestSuite* suite, const char* name, TestFunction function);
int  test_suite_run(const TestSuite* suite);

bool test_assert_true_impl(bool value, const char* expression, const char* file, int line);
bool test_assert_int_impl(long long expected, long long actual, const char* file, int line);
bool test_assert_u64_impl(uint64_t expected, uint64_t actual, const char* file, int line);
bool test_assert_string_impl(const char* expected, const char* actual, const char* file, int line);
bool test_assert_memory_impl(const void* expected, const void* actual, size_t size, const char* file, int line);

#define TEST_ADD(suite, function) test_suite_add((suite), #function, (function))

#define TEST_ASSERT(expression)                                                                                      \
    do {                                                                                                             \
        if(!test_assert_true_impl((expression), #expression, __FILE__, __LINE__)) {                                  \
            return;                                                                                                  \
        }                                                                                                            \
    } while(0)

#define TEST_ASSERT_EQ_INT(expected, actual)                                                                         \
    do {                                                                                                             \
        if(!test_assert_int_impl((long long)(expected), (long long)(actual), __FILE__, __LINE__)) {                  \
            return;                                                                                                  \
        }                                                                                                            \
    } while(0)

#define TEST_ASSERT_EQ_U64(expected, actual)                                                                         \
    do {                                                                                                             \
        if(!test_assert_u64_impl((uint64_t)(expected), (uint64_t)(actual), __FILE__, __LINE__)) {                    \
            return;                                                                                                  \
        }                                                                                                            \
    } while(0)

#define TEST_ASSERT_EQ_SIZE(expected, actual) TEST_ASSERT_EQ_U64((expected), (actual))
#define TEST_ASSERT_EQ_U32(expected, actual) TEST_ASSERT_EQ_U64((expected), (actual))

#define TEST_ASSERT_EQ_STRING(expected, actual)                                                                      \
    do {                                                                                                             \
        if(!test_assert_string_impl((expected), (actual), __FILE__, __LINE__)) {                                     \
            return;                                                                                                  \
        }                                                                                                            \
    } while(0)

#define TEST_ASSERT_MEMORY(expected, actual, size)                                                                   \
    do {                                                                                                             \
        if(!test_assert_memory_impl((expected), (actual), (size), __FILE__, __LINE__)) {                             \
            return;                                                                                                  \
        }                                                                                                            \
    } while(0)

void register_protocol_tests(TestSuite* suite);
void register_frame_tests(TestSuite* suite);
void register_session_tests(TestSuite* suite);
void register_dispatcher_tests(TestSuite* suite);

#endif
