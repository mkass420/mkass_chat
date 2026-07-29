#include "test.h"

#include "server/connection.h"

#include <stdint.h>
#include <string.h>

static bool test_connection_slot_init(void) {
    ClientConnection connection;
    memset(&connection, 0xA5, sizeof(connection));

    connection_slot_init(&connection);

    TEST_ASSERT(connection.generation == 0U);
    TEST_ASSERT(connection.user_id == 0U);
    TEST_ASSERT(!connection_is_active(&connection));
    TEST_ASSERT(connection.transport.socket_fd == -1);
    TEST_ASSERT(connection.files.upload.status == FILE_UPLOAD_IDLE);
    TEST_ASSERT(connection.files.upload.file_fd == -1);
    TEST_ASSERT(connection.files.download.status == FILE_DOWNLOAD_IDLE);
    TEST_ASSERT(connection.files.download.file_fd == -1);

    return true;
}

static bool test_connection_open_and_reset(void) {
    ClientConnection connection;
    connection_slot_init(&connection);

    connection_open(&connection, 17);

    TEST_ASSERT(connection_is_active(&connection));
    TEST_ASSERT(connection.transport.socket_fd == 17);
    TEST_ASSERT(connection.generation == 1U);
    TEST_ASSERT(connection.user_id == 0U);

    connection.user_id              = 42U;
    connection.files.upload.status  = FILE_UPLOAD_READY;
    connection.files.upload.file_fd = 99;

    connection_reset(&connection);

    TEST_ASSERT(!connection_is_active(&connection));
    TEST_ASSERT(connection.transport.socket_fd == -1);
    TEST_ASSERT(connection.generation == 1U);
    TEST_ASSERT(connection.user_id == 0U);
    TEST_ASSERT(connection.files.upload.status == FILE_UPLOAD_IDLE);
    TEST_ASSERT(connection.files.upload.file_fd == -1);

    return true;
}

static bool test_connection_generation_increments_on_reuse(void) {
    ClientConnection connection;
    connection_slot_init(&connection);

    connection_open(&connection, 10);
    TEST_ASSERT(connection.generation == 1U);

    connection_reset(&connection);
    connection_open(&connection, 11);
    TEST_ASSERT(connection.generation == 2U);
    TEST_ASSERT(connection.transport.socket_fd == 11);

    connection_reset(&connection);
    connection_open(&connection, 12);
    TEST_ASSERT(connection.generation == 3U);

    return true;
}

static bool test_connection_generation_never_becomes_zero(void) {
    ClientConnection connection;
    connection_slot_init(&connection);
    connection.generation = UINT64_MAX;

    connection_open(&connection, 10);

    TEST_ASSERT(connection.generation == 1U);
    TEST_ASSERT(connection_is_active(&connection));

    return true;
}

static bool test_connection_null_is_inactive(void) {
    TEST_ASSERT(!connection_is_active(NULL));
    return true;
}

void register_connection_tests(TestSuite* suite) {
    TEST_ADD(suite, test_connection_slot_init);
    TEST_ADD(suite, test_connection_open_and_reset);
    TEST_ADD(suite, test_connection_generation_increments_on_reuse);
    TEST_ADD(suite, test_connection_generation_never_becomes_zero);
    TEST_ADD(suite, test_connection_null_is_inactive);
}
