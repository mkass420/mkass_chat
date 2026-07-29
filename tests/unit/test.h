#ifndef UNIT_TEST_H
#define UNIT_TEST_H

#include <stdbool.h>
#include <stddef.h>

#define TEST_MAX_CASES 256U

typedef bool (*TestFunction)(void);

typedef struct {
    const char*  name;
    TestFunction function;
} TestCase;

typedef struct {
    TestCase cases[TEST_MAX_CASES];
    size_t   count;
} TestSuite;

bool test_fail(const char* expression, const char* file, int line);
void test_suite_add(TestSuite* suite, const char* name, TestFunction function);
int  test_suite_run(const TestSuite* suite);

#define TEST_ADD(suite, function) test_suite_add((suite), #function, (function))

#define TEST_ASSERT(expression)                                \
    do {                                                       \
        if(!(expression)) {                                    \
            return test_fail(#expression, __FILE__, __LINE__); \
        }                                                      \
    } while(0)

void register_protocol_tests(TestSuite* suite);
void register_frame_tests(TestSuite* suite);
void register_binary_tests(TestSuite* suite);
void register_file_protocol_tests(TestSuite* suite);
void register_transport_tests(TestSuite* suite);
void register_connection_tests(TestSuite* suite);
void register_dispatcher_tests(TestSuite* suite);
void register_file_tests(TestSuite* suite);
void register_file_repository_tests(TestSuite* suite);
void register_file_handler_tests(TestSuite* suite);

#endif
