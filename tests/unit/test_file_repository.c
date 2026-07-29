#include "test.h"

#include "common/file_id.h"
#include "config.h"
#include "server/file_repository.h"

#include <sqlite3.h>
#include <stdint.h>
#include <string.h>

static FileId test_file_repository_id(uint8_t first_byte) {
    FileId file_id = {0};

    file_id.bytes[0]                 = first_byte;
    file_id.bytes[FILE_ID_SIZE - 1U] = (uint8_t)(first_byte ^ 0xA5U);

    return file_id;
}

static FileMetadata test_file_repository_metadata(uint8_t id_byte, const char* file_name) {
    FileMetadata metadata = {0};

    metadata.file_id       = test_file_repository_id(id_byte);
    metadata.owner_user_id = UINT32_C(4000000000);
    metadata.file_size     = UINT64_C(123456789);
    metadata.file_crc32    = UINT32_C(0xF2345678);
    metadata.created_at    = INT64_C(1700000000);
    metadata.file_name_len = (uint16_t)strlen(file_name);

    memcpy(metadata.file_name, file_name, (size_t)metadata.file_name_len + 1U);

    return metadata;
}

static bool test_file_repository_open(sqlite3** db, FileRepository* repository) {
    *db = NULL;
    file_repository_reset(repository);

    if(sqlite3_open(":memory:", db) != SQLITE_OK) {
        return false;
    }

    if(file_repository_init(repository, *db) != FILE_REPOSITORY_OK) {
        (void)sqlite3_close(*db);
        *db = NULL;
        return false;
    }

    return true;
}

static void test_file_repository_close(sqlite3* db, FileRepository* repository) {
    file_repository_destroy(repository);
    (void)sqlite3_close(db);
}

static bool test_file_repository_init(void) {
    sqlite3*       db = NULL;
    FileRepository repository;

    TEST_ASSERT(test_file_repository_open(&db, &repository));

    TEST_ASSERT(repository.db == db);
    TEST_ASSERT(repository.insert_statement != NULL);
    TEST_ASSERT(repository.find_statement != NULL);
    TEST_ASSERT(repository.remove_statement != NULL);

    file_repository_destroy(&repository);

    TEST_ASSERT(repository.db == NULL);
    TEST_ASSERT(repository.insert_statement == NULL);
    TEST_ASSERT(repository.find_statement == NULL);
    TEST_ASSERT(repository.remove_statement == NULL);

    TEST_ASSERT(sqlite3_close(db) == SQLITE_OK);

    return true;
}

static bool test_file_repository_insert_and_find(void) {
    sqlite3*       db = NULL;
    FileRepository repository;

    TEST_ASSERT(test_file_repository_open(&db, &repository));

    const FileMetadata expected = test_file_repository_metadata(0x11U, "archive.tar");

    TEST_ASSERT(file_repository_insert(&repository, &expected) == FILE_REPOSITORY_OK);

    FileMetadata actual;

    TEST_ASSERT(file_repository_find(&repository, &expected.file_id, &actual) == FILE_REPOSITORY_OK);

    TEST_ASSERT(file_id_equal(&actual.file_id, &expected.file_id));

    TEST_ASSERT(actual.owner_user_id == expected.owner_user_id);

    TEST_ASSERT(actual.file_size == expected.file_size);

    TEST_ASSERT(actual.file_crc32 == expected.file_crc32);

    TEST_ASSERT(actual.created_at == expected.created_at);

    TEST_ASSERT(actual.file_name_len == expected.file_name_len);

    TEST_ASSERT(memcmp(actual.file_name, expected.file_name, (size_t)expected.file_name_len + 1U) == 0);

    test_file_repository_close(db, &repository);

    return true;
}

static bool test_file_repository_duplicate(void) {
    sqlite3*       db = NULL;
    FileRepository repository;

    TEST_ASSERT(test_file_repository_open(&db, &repository));

    const FileMetadata metadata = test_file_repository_metadata(0x22U, "duplicate.bin");

    TEST_ASSERT(file_repository_insert(&repository, &metadata) == FILE_REPOSITORY_OK);

    TEST_ASSERT(file_repository_insert(&repository, &metadata) == FILE_REPOSITORY_ALREADY_EXISTS);

    FileMetadata found;

    TEST_ASSERT(file_repository_find(&repository, &metadata.file_id, &found) == FILE_REPOSITORY_OK);

    test_file_repository_close(db, &repository);

    return true;
}

static bool test_file_repository_not_found(void) {
    sqlite3*       db = NULL;
    FileRepository repository;

    TEST_ASSERT(test_file_repository_open(&db, &repository));

    const FileId file_id = test_file_repository_id(0x33U);
    FileMetadata metadata;

    memset(&metadata, 0xA5, sizeof(metadata));

    TEST_ASSERT(file_repository_find(&repository, &file_id, &metadata) == FILE_REPOSITORY_NOT_FOUND);

    const FileMetadata empty = {0};

    TEST_ASSERT(memcmp(&metadata, &empty, sizeof(metadata)) == 0);

    TEST_ASSERT(file_repository_remove(&repository, &file_id) == FILE_REPOSITORY_NOT_FOUND);

    test_file_repository_close(db, &repository);

    return true;
}

static bool test_file_repository_remove(void) {
    sqlite3*       db = NULL;
    FileRepository repository;

    TEST_ASSERT(test_file_repository_open(&db, &repository));

    const FileMetadata metadata = test_file_repository_metadata(0x44U, "remove.txt");

    TEST_ASSERT(file_repository_insert(&repository, &metadata) == FILE_REPOSITORY_OK);

    TEST_ASSERT(file_repository_remove(&repository, &metadata.file_id) == FILE_REPOSITORY_OK);

    FileMetadata found;

    TEST_ASSERT(file_repository_find(&repository, &metadata.file_id, &found) == FILE_REPOSITORY_NOT_FOUND);

    test_file_repository_close(db, &repository);

    return true;
}

static bool test_file_repository_invalid_metadata(void) {
    sqlite3*       db = NULL;
    FileRepository repository;

    TEST_ASSERT(test_file_repository_open(&db, &repository));

    FileMetadata metadata = test_file_repository_metadata(0x55U, "valid.txt");

    metadata.file_id = (FileId){0};

    TEST_ASSERT(file_repository_insert(&repository, &metadata) == FILE_REPOSITORY_INVALID_ARGUMENT);

    metadata           = test_file_repository_metadata(0x56U, "valid.txt");
    metadata.file_size = FILE_MAX_SIZE + 1U;

    TEST_ASSERT(file_repository_insert(&repository, &metadata) == FILE_REPOSITORY_INVALID_ARGUMENT);

    metadata               = test_file_repository_metadata(0x57U, "valid.txt");
    metadata.file_name_len = 0U;

    TEST_ASSERT(file_repository_insert(&repository, &metadata) == FILE_REPOSITORY_INVALID_ARGUMENT);

    metadata              = test_file_repository_metadata(0x58U, "valid.txt");
    metadata.file_name[2] = '\0';

    TEST_ASSERT(file_repository_insert(&repository, &metadata) == FILE_REPOSITORY_INVALID_ARGUMENT);

    metadata            = test_file_repository_metadata(0x59U, "valid.txt");
    metadata.created_at = -1;

    TEST_ASSERT(file_repository_insert(&repository, &metadata) == FILE_REPOSITORY_INVALID_ARGUMENT);

    TEST_ASSERT(file_repository_insert(NULL, &metadata) == FILE_REPOSITORY_INVALID_ARGUMENT);

    test_file_repository_close(db, &repository);

    return true;
}

static bool test_file_repository_statement_reuse(void) {
    sqlite3*       db = NULL;
    FileRepository repository;

    TEST_ASSERT(test_file_repository_open(&db, &repository));

    for(uint8_t value = 1U; value <= 8U; ++value) {
        char file_name[32];

        const int length =
            sqlite3_snprintf((int)sizeof(file_name), file_name, "file-%u.bin", (unsigned int)value) == NULL
                ? -1
                : (int)strlen(file_name);

        TEST_ASSERT(length > 0);

        const FileMetadata metadata = test_file_repository_metadata((uint8_t)(0x60U + value), file_name);

        TEST_ASSERT(file_repository_insert(&repository, &metadata) == FILE_REPOSITORY_OK);

        FileMetadata found;

        TEST_ASSERT(file_repository_find(&repository, &metadata.file_id, &found) == FILE_REPOSITORY_OK);

        TEST_ASSERT(strcmp(found.file_name, file_name) == 0);
    }

    test_file_repository_close(db, &repository);

    return true;
}

static bool test_file_repository_detects_corrupt_row(void) {
    sqlite3* db = NULL;

    TEST_ASSERT(sqlite3_open(":memory:", &db) == SQLITE_OK);

    TEST_ASSERT(
        sqlite3_exec(
            db,
            "CREATE TABLE files ("
            "id BLOB PRIMARY KEY,"
            "owner_user_id,"
            "original_name,"
            "size,"
            "crc32,"
            "created_at"
            ") WITHOUT ROWID;",
            NULL, NULL, NULL
        ) == SQLITE_OK
    );

    FileRepository repository;

    TEST_ASSERT(file_repository_init(&repository, db) == FILE_REPOSITORY_OK);

    const FileId file_id = test_file_repository_id(0x77U);

    sqlite3_stmt* insert_statement = NULL;

    TEST_ASSERT(
        sqlite3_prepare_v2(db, "INSERT INTO files VALUES (?1, 1, X'61', -1, 0, 1);", -1, &insert_statement, NULL) ==
        SQLITE_OK
    );

    TEST_ASSERT(
        sqlite3_bind_blob(insert_statement, 1, file_id.bytes, (int)FILE_ID_SIZE, SQLITE_TRANSIENT) == SQLITE_OK
    );

    TEST_ASSERT(sqlite3_step(insert_statement) == SQLITE_DONE);

    TEST_ASSERT(sqlite3_finalize(insert_statement) == SQLITE_OK);

    FileMetadata metadata;

    TEST_ASSERT(file_repository_find(&repository, &file_id, &metadata) == FILE_REPOSITORY_CORRUPT_DATA);

    file_repository_destroy(&repository);

    TEST_ASSERT(sqlite3_close(db) == SQLITE_OK);

    return true;
}

void register_file_repository_tests(TestSuite* suite) {
    TEST_ADD(suite, test_file_repository_init);
    TEST_ADD(suite, test_file_repository_insert_and_find);
    TEST_ADD(suite, test_file_repository_duplicate);
    TEST_ADD(suite, test_file_repository_not_found);
    TEST_ADD(suite, test_file_repository_remove);
    TEST_ADD(suite, test_file_repository_invalid_metadata);
    TEST_ADD(suite, test_file_repository_statement_reuse);
    TEST_ADD(suite, test_file_repository_detects_corrupt_row);
}
