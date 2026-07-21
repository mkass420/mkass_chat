#include "test.h"

#include <stdio.h>
#include <string.h>

static bool current_test_failed = false;

void test_suite_add(TestSuite* suite, const char* name, TestFunction function) {
    if(suite == NULL || name == NULL || function == NULL || suite->count >= 128U) {
        return;
    }

    suite->cases[suite->count].name     = name;
    suite->cases[suite->count].function = function;
    ++suite->count;
}

static void test_fail_prefix(const char* file, int line) {
    current_test_failed = true;
    fprintf(stderr, "\n       %s:%d: ", file, line);
}

bool test_assert_true_impl(bool value, const char* expression, const char* file, int line) {
    if(value) {
        return true;
    }

    test_fail_prefix(file, line);
    fprintf(stderr, "условие не выполнено: %s", expression);
    return false;
}

bool test_assert_int_impl(long long expected, long long actual, const char* file, int line) {
    if(expected == actual) {
        return true;
    }

    test_fail_prefix(file, line);
    fprintf(stderr, "ожидалось %lld, получено %lld", expected, actual);
    return false;
}

bool test_assert_u64_impl(uint64_t expected, uint64_t actual, const char* file, int line) {
    if(expected == actual) {
        return true;
    }

    test_fail_prefix(file, line);
    fprintf(stderr, "ожидалось %llu, получено %llu", (unsigned long long)expected, (unsigned long long)actual);
    return false;
}

bool test_assert_string_impl(const char* expected, const char* actual, const char* file, int line) {
    if(expected != NULL && actual != NULL && strcmp(expected, actual) == 0) {
        return true;
    }

    test_fail_prefix(file, line);
    fprintf(
        stderr, "ожидалась строка \"%s\", получена \"%s\"", expected == NULL ? "(null)" : expected,
        actual == NULL ? "(null)" : actual
    );
    return false;
}

bool test_assert_memory_impl(const void* expected, const void* actual, size_t size, const char* file, int line) {
    if(expected != NULL && actual != NULL && memcmp(expected, actual, size) == 0) {
        return true;
    }

    test_fail_prefix(file, line);
    fprintf(stderr, "блоки памяти отличаются, размер %zu", size);
    return false;
}

int test_suite_run(const TestSuite* suite) {
    size_t passed = 0U;
    size_t failed = 0U;

    for(size_t i = 0U; i < suite->count; ++i) {
        current_test_failed = false;
        printf("[%02zu/%02zu] %s", i + 1U, suite->count, suite->cases[i].name);
        fflush(stdout);

        suite->cases[i].function();

        if(current_test_failed) {
            ++failed;
            printf(" ... FAIL\n");
        }
        else {
            ++passed;
            printf(" ... OK\n");
        }
    }

    printf("\nУспешно: %zu\nОшибок:  %zu\nВсего:   %zu\n", passed, failed, suite->count);
    return failed == 0U ? 0 : 1;
}

int main(void) {
    TestSuite suite = {0};

    register_protocol_tests(&suite);
    register_frame_tests(&suite);
    register_session_tests(&suite);
    register_dispatcher_tests(&suite);

    return test_suite_run(&suite);
}
