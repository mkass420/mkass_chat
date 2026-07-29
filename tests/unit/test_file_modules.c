#define _POSIX_C_SOURCE 200809L

#include "test.h"

#include "common/file_id.h"
#include "server/file_storage.h"
#include "server/file_transfer.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

typedef struct {
    char        root[PATH_MAX];
    FileStorage storage;
    bool        initialized;
} StorageFixture;

static int remove_tree(const char* path) {
    DIR* directory = opendir(path);

    if(directory == NULL) {
        return errno == ENOENT ? 0 : -1;
    }

    int            result = 0;
    struct dirent* entry;

    while((entry = readdir(directory)) != NULL) {
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char      child[PATH_MAX];
        const int length = snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);

        if(length < 0 || (size_t)length >= sizeof(child)) {
            result = -1;
            break;
        }

        struct stat status;

        if(lstat(child, &status) != 0) {
            result = -1;
            break;
        }

        if(S_ISDIR(status.st_mode)) {
            if(remove_tree(child) != 0) {
                result = -1;
                break;
            }
        }
        else if(unlink(child) != 0) {
            result = -1;
            break;
        }
    }

    const int saved_errno = errno;
    (void)closedir(directory);

    if(result == 0 && rmdir(path) != 0) {
        result = -1;
    }

    if(result != 0) {
        errno = saved_errno;
    }

    return result;
}

static bool storage_fixture_init(StorageFixture* fixture) {
    if(fixture == NULL) {
        return false;
    }

    memset(fixture, 0, sizeof(*fixture));
    file_storage_reset(&fixture->storage);

    char  root_template[] = "/tmp/mkass-file-test-XXXXXX";
    char* root            = mkdtemp(root_template);

    if(root == NULL) {
        return false;
    }

    const size_t root_len = strlen(root);

    if(root_len >= sizeof(fixture->root)) {
        (void)remove_tree(root);
        return false;
    }

    memcpy(fixture->root, root, root_len + 1U);

    if(file_storage_init(&fixture->storage, fixture->root) != 0) {
        (void)remove_tree(fixture->root);
        fixture->root[0] = '\0';
        return false;
    }

    fixture->initialized = true;
    return true;
}

static void storage_fixture_destroy(StorageFixture* fixture) {
    if(fixture == NULL) {
        return;
    }

    if(fixture->initialized) {
        file_storage_destroy(&fixture->storage);
        fixture->initialized = false;
    }

    if(fixture->root[0] != '\0') {
        (void)remove_tree(fixture->root);
        fixture->root[0] = '\0';
    }
}

static bool test_file_id_helpers(void) {
    FileId first  = {0};
    FileId second = {0};

    TEST_ASSERT(file_id_is_zero(&first));
    TEST_ASSERT(!file_id_is_zero(NULL));
    TEST_ASSERT(file_id_equal(&first, &second));
    TEST_ASSERT(!file_id_equal(NULL, &second));

    for(size_t i = 0U; i < FILE_ID_SIZE; ++i) {
        first.bytes[i]  = (uint8_t)i;
        second.bytes[i] = (uint8_t)i;
    }

    TEST_ASSERT(!file_id_is_zero(&first));
    TEST_ASSERT(file_id_equal(&first, &second));

    second.bytes[FILE_ID_SIZE - 1U] ^= 0x01U;
    TEST_ASSERT(!file_id_equal(&first, &second));

    char hex[FILE_ID_HEX_SIZE + 1U];
    file_id_to_hex(&first, hex);
    TEST_ASSERT(strcmp(hex, "000102030405060708090a0b0c0d0e0f") == 0);

    file_id_to_hex(NULL, hex);
    TEST_ASSERT(hex[0] == '\0');

    return true;
}

static bool test_file_id_generate(void) {
    FileId first  = {0};
    FileId second = {0};

    TEST_ASSERT(file_id_generate(NULL) == -1);
    TEST_ASSERT(errno == EINVAL);
    TEST_ASSERT(file_id_generate(&first) == 0);
    TEST_ASSERT(file_id_generate(&second) == 0);
    TEST_ASSERT(!file_id_is_zero(&first));
    TEST_ASSERT(!file_id_is_zero(&second));
    TEST_ASSERT(!file_id_equal(&first, &second));

    return true;
}

static bool test_file_storage_lifecycle(void) {
    StorageFixture fixture;
    TEST_ASSERT(storage_fixture_init(&fixture));

    FileId file_id;
    TEST_ASSERT(file_id_generate(&file_id) == 0);

    const int temporary_fd = file_storage_create_temporary(&fixture.storage, &file_id);
    TEST_ASSERT(temporary_fd >= 0);

    static const uint8_t payload[] = "stored object";
    TEST_ASSERT(write(temporary_fd, payload, sizeof(payload) - 1U) == (ssize_t)(sizeof(payload) - 1U));
    TEST_ASSERT(fsync(temporary_fd) == 0);
    TEST_ASSERT(close(temporary_fd) == 0);

    errno = 0;
    TEST_ASSERT(file_storage_create_temporary(&fixture.storage, &file_id) == -1);
    TEST_ASSERT(errno == EEXIST);

    TEST_ASSERT(file_storage_finalize(&fixture.storage, &file_id) == 0);
    TEST_ASSERT(file_storage_abort(&fixture.storage, &file_id) == 0);

    const int object_fd = file_storage_open_object(&fixture.storage, &file_id);
    TEST_ASSERT(object_fd >= 0);

    uint8_t restored[sizeof(payload)] = {0};
    TEST_ASSERT(read(object_fd, restored, sizeof(payload) - 1U) == (ssize_t)(sizeof(payload) - 1U));
    TEST_ASSERT(close(object_fd) == 0);
    TEST_ASSERT(memcmp(restored, payload, sizeof(payload) - 1U) == 0);

    TEST_ASSERT(file_storage_remove_object(&fixture.storage, &file_id) == 0);
    TEST_ASSERT(file_storage_remove_object(&fixture.storage, &file_id) == 0);

    storage_fixture_destroy(&fixture);
    return true;
}

static bool test_file_storage_invalid_arguments(void) {
    FileStorage storage;
    file_storage_reset(&storage);
    FileId zero = {0};

    TEST_ASSERT(!file_storage_is_initialized(&storage));
    TEST_ASSERT(file_storage_init(NULL, "/tmp/test") == -1);
    TEST_ASSERT(file_storage_init(&storage, NULL) == -1);
    TEST_ASSERT(file_storage_init(&storage, "") == -1);

    TEST_ASSERT(file_storage_create_temporary(&storage, &zero) == -1);
    TEST_ASSERT(file_storage_open_object(&storage, &zero) == -1);
    TEST_ASSERT(file_storage_finalize(&storage, &zero) == -1);
    TEST_ASSERT(file_storage_abort(&storage, &zero) == -1);
    TEST_ASSERT(file_storage_remove_object(&storage, &zero) == -1);

    return true;
}

static bool test_file_upload_download_round_trip(void) {
    StorageFixture fixture;
    TEST_ASSERT(storage_fixture_init(&fixture));

    FileTransferState state;
    file_transfer_state_init(&state);

    static const uint8_t name[]     = "hello.txt";
    static const uint8_t first[]    = "hello ";
    static const uint8_t second[]   = "world";
    static const uint8_t expected[] = "hello world";

    TEST_ASSERT(
        file_upload_begin(
            &fixture.storage, &state.upload, sizeof(expected) - 1U, name, (uint16_t)(sizeof(name) - 1U)
        ) == FILE_TRANSFER_OK
    );
    TEST_ASSERT(file_upload_is_active(&state.upload));
    TEST_ASSERT(state.upload.status == FILE_UPLOAD_READY);
    TEST_ASSERT(state.upload.expected_size == sizeof(expected) - 1U);
    TEST_ASSERT(strcmp(state.upload.file_name, (const char*)name) == 0);

    TEST_ASSERT(file_upload_write(&state.upload, first, (uint32_t)(sizeof(first) - 1U)) == FILE_TRANSFER_OK);
    TEST_ASSERT(file_upload_write(&state.upload, second, (uint32_t)(sizeof(second) - 1U)) == FILE_TRANSFER_OK);
    TEST_ASSERT(state.upload.committed_size == sizeof(expected) - 1U);

    CompletedFileUpload completed_upload;
    TEST_ASSERT(file_upload_finish(&fixture.storage, &state.upload, &completed_upload) == FILE_TRANSFER_OK);

    FileId   file_id    = completed_upload.file_id;
    uint32_t file_crc32 = completed_upload.file_crc32;

    TEST_ASSERT(!file_upload_is_active(&state.upload));
    TEST_ASSERT(!file_id_is_zero(&file_id));

    uLong expected_crc = crc32(0L, Z_NULL, 0);
    expected_crc       = crc32(expected_crc, expected, (uInt)(sizeof(expected) - 1U));
    TEST_ASSERT(file_crc32 == (uint32_t)expected_crc);

    TEST_ASSERT(
        file_download_begin(&fixture.storage, &state.download, &file_id, name, (uint16_t)(sizeof(name) - 1U)) ==
        FILE_TRANSFER_OK
    );
    TEST_ASSERT(file_download_is_active(&state.download));
    TEST_ASSERT(state.download.file_size == sizeof(expected) - 1U);

    uint8_t output[sizeof(expected)] = {0};
    size_t  total                    = 0U;
    bool    complete                 = false;

    while(!complete) {
        uint32_t output_len = 0U;
        TEST_ASSERT(
            file_download_read(&state.download, output + total, 4U, &output_len, &complete) == FILE_TRANSFER_OK
        );
        total += output_len;
        TEST_ASSERT(total <= sizeof(expected) - 1U);
    }

    TEST_ASSERT(total == sizeof(expected) - 1U);
    TEST_ASSERT(memcmp(output, expected, sizeof(expected) - 1U) == 0);
    TEST_ASSERT(file_download_finish(&state.download) == FILE_TRANSFER_OK);
    TEST_ASSERT(!file_download_is_active(&state.download));

    TEST_ASSERT(file_storage_remove_object(&fixture.storage, &file_id) == 0);
    storage_fixture_destroy(&fixture);

    return true;
}

static bool test_file_upload_validation_and_abort(void) {
    StorageFixture fixture;
    TEST_ASSERT(storage_fixture_init(&fixture));

    FileTransferState state;
    file_transfer_state_init(&state);

    static const uint8_t name[]         = "upload.bin";
    static const uint8_t invalid_name[] = {'a', '\0', 'b'};
    static const uint8_t data[]         = {1U, 2U, 3U};

    TEST_ASSERT(
        file_upload_begin(&fixture.storage, &state.upload, FILE_MAX_SIZE + 1U, name, (uint16_t)(sizeof(name) - 1U)) ==
        FILE_TRANSFER_FILE_TOO_LARGE
    );

    TEST_ASSERT(
        file_upload_begin(&fixture.storage, &state.upload, 3U, invalid_name, (uint16_t)sizeof(invalid_name)) ==
        FILE_TRANSFER_INVALID_ARGUMENT
    );

    TEST_ASSERT(
        file_upload_begin(&fixture.storage, &state.upload, 5U, name, (uint16_t)(sizeof(name) - 1U)) == FILE_TRANSFER_OK
    );

    const FileId temporary_id = state.upload.file_id;

    TEST_ASSERT(
        file_upload_begin(&fixture.storage, &state.upload, 5U, name, (uint16_t)(sizeof(name) - 1U)) ==
        FILE_TRANSFER_BUSY
    );

    TEST_ASSERT(file_upload_write(&state.upload, NULL, 1U) == FILE_TRANSFER_INVALID_ARGUMENT);
    TEST_ASSERT(file_upload_write(&state.upload, data, 0U) == FILE_TRANSFER_INVALID_ARGUMENT);
    TEST_ASSERT(file_upload_write(&state.upload, data, FILE_CHUNK_DATA_SIZE + 1U) == FILE_TRANSFER_INVALID_CHUNK);
    TEST_ASSERT(file_upload_write(&state.upload, data, sizeof(data)) == FILE_TRANSFER_OK);
    TEST_ASSERT(file_upload_write(&state.upload, data, sizeof(data)) == FILE_TRANSFER_INVALID_CHUNK);

    CompletedFileUpload completed_upload;
    TEST_ASSERT(file_upload_finish(&fixture.storage, &state.upload, &completed_upload) == FILE_TRANSFER_INCOMPLETE);

    file_upload_abort(&fixture.storage, &state.upload);
    TEST_ASSERT(!file_upload_is_active(&state.upload));
    TEST_ASSERT(state.upload.file_fd == -1);
    TEST_ASSERT(file_storage_abort(&fixture.storage, &temporary_id) == 0);

    storage_fixture_destroy(&fixture);
    return true;
}

static bool test_empty_file_round_trip(void) {
    StorageFixture fixture;
    TEST_ASSERT(storage_fixture_init(&fixture));

    FileTransferState state;
    file_transfer_state_init(&state);
    static const uint8_t name[] = "empty.bin";

    TEST_ASSERT(
        file_upload_begin(&fixture.storage, &state.upload, 0U, name, (uint16_t)(sizeof(name) - 1U)) == FILE_TRANSFER_OK
    );
    CompletedFileUpload completed_upload;
    TEST_ASSERT(file_upload_finish(&fixture.storage, &state.upload, &completed_upload) == FILE_TRANSFER_OK);
    TEST_ASSERT(completed_upload.file_crc32 == 0U);
    FileId file_id = completed_upload.file_id;

    TEST_ASSERT(
        file_download_begin(&fixture.storage, &state.download, &file_id, name, (uint16_t)(sizeof(name) - 1U)) ==
        FILE_TRANSFER_OK
    );

    uint8_t  output     = 0U;
    uint32_t output_len = 99U;
    bool     complete   = false;

    TEST_ASSERT(file_download_read(&state.download, &output, 1U, &output_len, &complete) == FILE_TRANSFER_OK);
    TEST_ASSERT(output_len == 0U);
    TEST_ASSERT(complete);
    TEST_ASSERT(file_download_finish(&state.download) == FILE_TRANSFER_OK);

    TEST_ASSERT(file_storage_remove_object(&fixture.storage, &file_id) == 0);
    storage_fixture_destroy(&fixture);

    return true;
}

static bool test_file_download_validation(void) {
    FileTransferState state;
    file_transfer_state_init(&state);

    uint8_t  output[8]  = {0};
    uint32_t output_len = 99U;
    bool     complete   = true;

    TEST_ASSERT(
        file_download_read(&state.download, output, sizeof(output), &output_len, &complete) ==
        FILE_TRANSFER_INVALID_STATE
    );
    TEST_ASSERT(output_len == 0U);
    TEST_ASSERT(!complete);
    TEST_ASSERT(file_download_finish(&state.download) == FILE_TRANSFER_INVALID_STATE);

    TEST_ASSERT(
        file_download_read(NULL, output, sizeof(output), &output_len, &complete) == FILE_TRANSFER_INVALID_ARGUMENT
    );
    TEST_ASSERT(
        file_download_read(&state.download, NULL, sizeof(output), &output_len, &complete) ==
        FILE_TRANSFER_INVALID_ARGUMENT
    );
    TEST_ASSERT(
        file_download_read(&state.download, output, 0U, &output_len, &complete) == FILE_TRANSFER_INVALID_ARGUMENT
    );
    TEST_ASSERT(
        file_download_read(&state.download, output, sizeof(output), NULL, &complete) == FILE_TRANSFER_INVALID_ARGUMENT
    );
    TEST_ASSERT(
        file_download_read(&state.download, output, sizeof(output), &output_len, NULL) == FILE_TRANSFER_INVALID_ARGUMENT
    );

    return true;
}

static bool test_file_transfer_abort_all(void) {
    StorageFixture fixture;
    TEST_ASSERT(storage_fixture_init(&fixture));

    FileTransferState state;
    file_transfer_state_init(&state);
    static const uint8_t name[] = "active.bin";
    static const uint8_t data[] = "data";

    TEST_ASSERT(
        file_upload_begin(&fixture.storage, &state.upload, sizeof(data) - 1U, name, (uint16_t)(sizeof(name) - 1U)) ==
        FILE_TRANSFER_OK
    );
    TEST_ASSERT(file_upload_write(&state.upload, data, (uint32_t)(sizeof(data) - 1U)) == FILE_TRANSFER_OK);

    CompletedFileUpload completed_upload;
    TEST_ASSERT(file_upload_finish(&fixture.storage, &state.upload, &completed_upload) == FILE_TRANSFER_OK);

    FileId file_id = completed_upload.file_id;

    TEST_ASSERT(
        file_download_begin(&fixture.storage, &state.download, &file_id, name, (uint16_t)(sizeof(name) - 1U)) ==
        FILE_TRANSFER_OK
    );

    TEST_ASSERT(
        file_upload_begin(&fixture.storage, &state.upload, 10U, name, (uint16_t)(sizeof(name) - 1U)) == FILE_TRANSFER_OK
    );

    file_transfer_abort_all(&fixture.storage, &state);

    TEST_ASSERT(!file_upload_is_active(&state.upload));
    TEST_ASSERT(!file_download_is_active(&state.download));
    TEST_ASSERT(state.upload.file_fd == -1);
    TEST_ASSERT(state.download.file_fd == -1);

    const int object_fd = file_storage_open_object(&fixture.storage, &file_id);
    TEST_ASSERT(object_fd >= 0);
    TEST_ASSERT(close(object_fd) == 0);

    TEST_ASSERT(file_storage_remove_object(&fixture.storage, &file_id) == 0);
    storage_fixture_destroy(&fixture);

    return true;
}

static bool test_file_transfer_result_strings(void) {
    TEST_ASSERT(strcmp(file_transfer_result_to_string(FILE_TRANSFER_OK), "ok") == 0);
    TEST_ASSERT(strcmp(file_transfer_result_to_string(FILE_TRANSFER_BUSY), "file transfer is already active") == 0);
    TEST_ASSERT(strcmp(file_transfer_result_to_string(FILE_TRANSFER_INCOMPLETE), "file transfer is incomplete") == 0);
    TEST_ASSERT(strcmp(file_transfer_result_to_string((FileTransferResult)99), "unknown file transfer error") == 0);

    return true;
}

void register_file_tests(TestSuite* suite) {
    TEST_ADD(suite, test_file_id_helpers);
    TEST_ADD(suite, test_file_id_generate);
    TEST_ADD(suite, test_file_storage_lifecycle);
    TEST_ADD(suite, test_file_storage_invalid_arguments);
    TEST_ADD(suite, test_file_upload_download_round_trip);
    TEST_ADD(suite, test_file_upload_validation_and_abort);
    TEST_ADD(suite, test_empty_file_round_trip);
    TEST_ADD(suite, test_file_download_validation);
    TEST_ADD(suite, test_file_transfer_abort_all);
    TEST_ADD(suite, test_file_transfer_result_strings);
}
