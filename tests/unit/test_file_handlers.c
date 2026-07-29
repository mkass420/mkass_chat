#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "test.h"

#include "common/binary.h"
#include "server/file_repository.h"
#include "server/handlers/file_handlers.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <sqlite3.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    ServerState*     server;
    ClientConnection connection;
    sqlite3*         db;
    char             root[PATH_MAX];
} FileHandlerFixture;

static int file_handler_remove_tree(const char* path) {
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
            if(file_handler_remove_tree(child) != 0) {
                result = -1;
                break;
            }
        }
        else if(unlink(child) != 0) {
            result = -1;
            break;
        }
    }

    (void)closedir(directory);

    if(result == 0 && rmdir(path) != 0) {
        result = -1;
    }

    return result;
}

static bool file_handler_fixture_init(FileHandlerFixture* fixture) {
    memset(fixture, 0, sizeof(*fixture));

    fixture->server = mmap(NULL, sizeof(*fixture->server), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if(fixture->server == MAP_FAILED) {
        fixture->server = NULL;
        return false;
    }

    file_storage_reset(&fixture->server->file_storage);

    file_repository_reset(&fixture->server->file_repository);

    char  root_template[] = "/tmp/mkass-file-handler-XXXXXX";
    char* root            = mkdtemp(root_template);

    if(root == NULL) {
        return false;
    }

    const size_t root_len = strlen(root);

    if(root_len >= sizeof(fixture->root)) {
        return false;
    }

    memcpy(fixture->root, root, root_len + 1U);

    if(file_storage_init(&fixture->server->file_storage, fixture->root) != 0) {
        return false;
    }

    if(sqlite3_open(":memory:", &fixture->db) != SQLITE_OK) {
        return false;
    }

    if(file_repository_init(&fixture->server->file_repository, fixture->db) != FILE_REPOSITORY_OK) {
        return false;
    }

    connection_slot_init(&fixture->connection);
    connection_open(&fixture->connection, 10);

    return true;
}

static void file_handler_fixture_destroy(FileHandlerFixture* fixture) {
    if(fixture == NULL) {
        return;
    }

    if(fixture->server != NULL) {
        file_transfer_abort_all(&fixture->server->file_storage, &fixture->connection.files);

        file_repository_destroy(&fixture->server->file_repository);

        file_storage_destroy(&fixture->server->file_storage);
    }

    if(fixture->db != NULL) {
        (void)sqlite3_close(fixture->db);
        fixture->db = NULL;
    }

    if(fixture->root[0] != '\0') {
        (void)file_handler_remove_tree(fixture->root);
    }

    if(fixture->server != NULL) {
        (void)munmap(fixture->server, sizeof(*fixture->server));

        fixture->server = NULL;
    }
}

static DecodedFrame file_handler_request(
    MessageType    type,
    uint32_t       request_id,
    const uint8_t* payload,
    uint32_t       payload_len
) {
    return (DecodedFrame){
        .header =
            {
                     .magic            = PROTOCOL_MAGIC,
                     .type             = type,
                     .request_id       = request_id,
                     .uncompressed_len = payload_len,
                     .payload_len      = payload_len,
                     },
        .payload     = payload,
        .payload_len = payload_len,
    };
}

static bool file_handler_decode_response(FileHandlerFixture* fixture, DecodedFrame* response) {
    ParsedFrame parsed;

    return frame_try_parse(
               fixture->connection.transport.write_buffer, fixture->connection.transport.write_bytes, &parsed
           ) == FRAME_PARSE_COMPLETE &&
           frame_decode(&fixture->server->frame_codec, &parsed, response) == FRAME_DECODE_OK;
}

static void file_handler_clear_response(FileHandlerFixture* fixture) {
    fixture->connection.transport.write_offset = 0U;
    fixture->connection.transport.write_bytes  = 0U;
}

static bool file_handler_make_upload_begin_payload(
    uint8_t*    payload,
    size_t      capacity,
    uint64_t    file_size,
    const char* file_name,
    uint32_t*   payload_len
) {
    const size_t name_len = strlen(file_name);
    BinaryWriter writer;

    binary_writer_init(&writer, payload, capacity);

    if(name_len > UINT16_MAX || !binary_write_u64(&writer, file_size) ||
        !binary_write_u16(&writer, (uint16_t)name_len) || !binary_write_bytes(&writer, file_name, name_len)) {
        return false;
    }

    *payload_len = (uint32_t)binary_writer_size(&writer);

    return true;
}

static bool test_file_handlers_round_trip(void) {
    FileHandlerFixture fixture;
    TEST_ASSERT(file_handler_fixture_init(&fixture));

    static const char    file_name[] = "hello.txt";
    static const uint8_t file_data[] = "hello world";

    uint8_t  begin_payload[64];
    uint32_t begin_payload_len = 0U;

    TEST_ASSERT(file_handler_make_upload_begin_payload(
        begin_payload, sizeof(begin_payload), sizeof(file_data) - 1U, file_name, &begin_payload_len
    ));

    DecodedFrame request =
        file_handler_request(MSG_TYPE_FILE_UPLOAD_BEGIN_REQUEST, 1U, begin_payload, begin_payload_len);

    TEST_ASSERT(server_handle_file_upload_begin(fixture.server, &fixture.connection, &request) == TRANSPORT_IO_OK);

    DecodedFrame response;
    TEST_ASSERT(file_handler_decode_response(&fixture, &response));
    TEST_ASSERT(response.header.type == MSG_TYPE_FILE_UPLOAD_BEGIN_RESPONSE);
    TEST_ASSERT(file_upload_is_active(&fixture.connection.files.upload));

    file_handler_clear_response(&fixture);

    request =
        file_handler_request(MSG_TYPE_FILE_UPLOAD_CHUNK_REQUEST, 2U, file_data, (uint32_t)(sizeof(file_data) - 1U));

    TEST_ASSERT(server_handle_file_upload_chunk(fixture.server, &fixture.connection, &request) == TRANSPORT_IO_OK);

    file_handler_clear_response(&fixture);

    request = file_handler_request(MSG_TYPE_FILE_UPLOAD_FINISH_REQUEST, 3U, NULL, 0U);

    TEST_ASSERT(server_handle_file_upload_finish(fixture.server, &fixture.connection, &request) == TRANSPORT_IO_OK);

    TEST_ASSERT(file_handler_decode_response(&fixture, &response));
    TEST_ASSERT(response.header.type == MSG_TYPE_FILE_UPLOAD_FINISH_RESPONSE);
    TEST_ASSERT(response.payload_len == FILE_ID_SIZE);

    FileId file_id;
    memcpy(file_id.bytes, response.payload, FILE_ID_SIZE);

    FileMetadata metadata;
    TEST_ASSERT(file_repository_find(&fixture.server->file_repository, &file_id, &metadata) == FILE_REPOSITORY_OK);
    TEST_ASSERT(metadata.file_size == sizeof(file_data) - 1U);
    TEST_ASSERT(strcmp(metadata.file_name, file_name) == 0);

    file_handler_clear_response(&fixture);

    request = file_handler_request(MSG_TYPE_FILE_DOWNLOAD_BEGIN_REQUEST, 4U, file_id.bytes, FILE_ID_SIZE);

    TEST_ASSERT(server_handle_file_download_begin(fixture.server, &fixture.connection, &request) == TRANSPORT_IO_OK);

    TEST_ASSERT(file_handler_decode_response(&fixture, &response));
    TEST_ASSERT(response.header.type == MSG_TYPE_FILE_DOWNLOAD_BEGIN_RESPONSE);

    BinaryReader reader;
    binary_reader_init(&reader, response.payload, response.payload_len);

    uint64_t       downloaded_size     = 0U;
    uint16_t       downloaded_name_len = 0U;
    const uint8_t* downloaded_name     = NULL;

    TEST_ASSERT(binary_read_u64(&reader, &downloaded_size));
    TEST_ASSERT(binary_read_u16(&reader, &downloaded_name_len));
    TEST_ASSERT(binary_read_view(&reader, &downloaded_name, downloaded_name_len));
    TEST_ASSERT(binary_reader_is_finished(&reader));
    TEST_ASSERT(downloaded_size == sizeof(file_data) - 1U);
    TEST_ASSERT(downloaded_name_len == sizeof(file_name) - 1U);
    TEST_ASSERT(memcmp(downloaded_name, file_name, sizeof(file_name) - 1U) == 0);

    file_handler_clear_response(&fixture);

    request = file_handler_request(MSG_TYPE_FILE_DOWNLOAD_CHUNK_REQUEST, 5U, NULL, 0U);

    TEST_ASSERT(server_handle_file_download_chunk(fixture.server, &fixture.connection, &request) == TRANSPORT_IO_OK);

    TEST_ASSERT(file_handler_decode_response(&fixture, &response));
    TEST_ASSERT(response.header.type == MSG_TYPE_FILE_DOWNLOAD_CHUNK_RESPONSE);
    TEST_ASSERT(response.payload_len == sizeof(file_data) - 1U);
    TEST_ASSERT(memcmp(response.payload, file_data, sizeof(file_data) - 1U) == 0);
    TEST_ASSERT(!file_download_is_active(&fixture.connection.files.download));

    file_handler_fixture_destroy(&fixture);
    return true;
}

static bool test_file_handlers_invalid_upload_payload(void) {
    FileHandlerFixture fixture;
    TEST_ASSERT(file_handler_fixture_init(&fixture));

    static const uint8_t invalid_payload[] = {0x01U};
    const DecodedFrame   request           = file_handler_request(
        MSG_TYPE_FILE_UPLOAD_BEGIN_REQUEST, 10U, invalid_payload, (uint32_t)sizeof(invalid_payload)
    );

    TEST_ASSERT(server_handle_file_upload_begin(fixture.server, &fixture.connection, &request) == TRANSPORT_IO_OK);

    DecodedFrame response;
    TEST_ASSERT(file_handler_decode_response(&fixture, &response));
    TEST_ASSERT(response.header.type == MSG_TYPE_ERROR_RESPONSE);
    TEST_ASSERT(!file_upload_is_active(&fixture.connection.files.upload));

    file_handler_fixture_destroy(&fixture);
    return true;
}

static bool test_file_handlers_incomplete_upload(void) {
    FileHandlerFixture fixture;
    TEST_ASSERT(file_handler_fixture_init(&fixture));

    uint8_t  begin_payload[64];
    uint32_t begin_payload_len = 0U;

    TEST_ASSERT(file_handler_make_upload_begin_payload(
        begin_payload, sizeof(begin_payload), 10U, "partial.bin", &begin_payload_len
    ));

    DecodedFrame request =
        file_handler_request(MSG_TYPE_FILE_UPLOAD_BEGIN_REQUEST, 20U, begin_payload, begin_payload_len);

    TEST_ASSERT(server_handle_file_upload_begin(fixture.server, &fixture.connection, &request) == TRANSPORT_IO_OK);

    file_handler_clear_response(&fixture);

    request = file_handler_request(MSG_TYPE_FILE_UPLOAD_FINISH_REQUEST, 21U, NULL, 0U);

    TEST_ASSERT(server_handle_file_upload_finish(fixture.server, &fixture.connection, &request) == TRANSPORT_IO_OK);

    DecodedFrame response;
    TEST_ASSERT(file_handler_decode_response(&fixture, &response));
    TEST_ASSERT(response.header.type == MSG_TYPE_ERROR_RESPONSE);
    TEST_ASSERT(file_upload_is_active(&fixture.connection.files.upload));

    file_handler_fixture_destroy(&fixture);
    return true;
}

static bool test_file_handlers_unknown_download(void) {
    FileHandlerFixture fixture;
    TEST_ASSERT(file_handler_fixture_init(&fixture));

    FileId file_id   = {0};
    file_id.bytes[0] = 0x11U;

    const DecodedFrame request =
        file_handler_request(MSG_TYPE_FILE_DOWNLOAD_BEGIN_REQUEST, 30U, file_id.bytes, FILE_ID_SIZE);

    TEST_ASSERT(server_handle_file_download_begin(fixture.server, &fixture.connection, &request) == TRANSPORT_IO_OK);

    DecodedFrame response;
    TEST_ASSERT(file_handler_decode_response(&fixture, &response));
    TEST_ASSERT(response.header.type == MSG_TYPE_ERROR_RESPONSE);
    TEST_ASSERT(!file_download_is_active(&fixture.connection.files.download));

    file_handler_fixture_destroy(&fixture);
    return true;
}

static bool test_file_handlers_access_denied(void) {
    FileHandlerFixture fixture;
    TEST_ASSERT(file_handler_fixture_init(&fixture));

    fixture.connection.user_id = 7U;

    FileMetadata metadata     = {0};
    metadata.file_id.bytes[0] = 0x44U;
    metadata.owner_user_id    = 8U;
    metadata.file_size        = 1U;
    metadata.file_crc32       = 0U;
    metadata.created_at       = 1;
    metadata.file_name_len    = 8U;
    memcpy(metadata.file_name, "file.bin", 9U);

    TEST_ASSERT(file_repository_insert(&fixture.server->file_repository, &metadata) == FILE_REPOSITORY_OK);

    const DecodedFrame request =
        file_handler_request(MSG_TYPE_FILE_DOWNLOAD_BEGIN_REQUEST, 40U, metadata.file_id.bytes, FILE_ID_SIZE);

    TEST_ASSERT(server_handle_file_download_begin(fixture.server, &fixture.connection, &request) == TRANSPORT_IO_OK);

    DecodedFrame response;
    TEST_ASSERT(file_handler_decode_response(&fixture, &response));
    TEST_ASSERT(response.header.type == MSG_TYPE_ERROR_RESPONSE);
    TEST_ASSERT(!file_download_is_active(&fixture.connection.files.download));

    file_handler_fixture_destroy(&fixture);
    return true;
}

void register_file_handler_tests(TestSuite* suite) {
    TEST_ADD(suite, test_file_handlers_round_trip);
    TEST_ADD(suite, test_file_handlers_invalid_upload_payload);
    TEST_ADD(suite, test_file_handlers_incomplete_upload);
    TEST_ADD(suite, test_file_handlers_unknown_download);
    TEST_ADD(suite, test_file_handlers_access_denied);
}
