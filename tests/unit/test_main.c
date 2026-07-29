#include "test.h"

#include <stdio.h>

bool test_fail(const char* expression, const char* file, int line) {
    fprintf(stderr, "\n       %s:%d: assertion failed: %s", file, line, expression);

    return false;
}

void test_suite_add(TestSuite* suite, const char* name, TestFunction function) {
    if(suite == NULL || name == NULL || function == NULL || suite->count >= TEST_MAX_CASES) {
        return;
    }

    suite->cases[suite->count] = (TestCase){
        .name     = name,
        .function = function,
    };

    ++suite->count;
}

int test_suite_run(const TestSuite* suite) {
    if(suite == NULL) {
        return 1;
    }

    size_t passed = 0U;
    size_t failed = 0U;

    for(size_t i = 0U; i < suite->count; ++i) {
        const TestCase* test = &suite->cases[i];

        printf("[%02zu/%02zu] %s", i + 1U, suite->count, test->name);

        fflush(stdout);

        if(test->function()) {
            ++passed;
            printf(" ... OK\n");
        }
        else {
            ++failed;
            printf(" ... FAIL\n");
        }
    }

    printf(
        "\nУспешно: %zu\n"
        "Ошибок:  %zu\n"
        "Всего:   %zu\n",
        passed, failed, suite->count
    );

    return failed == 0U ? 0 : 1;
}

int main(void) {
    TestSuite suite = {0};

    register_protocol_tests(&suite);
    register_frame_tests(&suite);
    register_binary_tests(&suite);
    register_file_protocol_tests(&suite);
    register_transport_tests(&suite);
    register_connection_tests(&suite);
    register_dispatcher_tests(&suite);
    register_file_tests(&suite);
    register_file_repository_tests(&suite);
    register_file_handler_tests(&suite);

    return test_suite_run(&suite);
}
