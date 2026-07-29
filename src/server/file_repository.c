#include "server/file_repository.h"

#include "common/file_id.h"
#include "config.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const char FILE_REPOSITORY_SCHEMA[] = "CREATE TABLE IF NOT EXISTS files ("
                                             "id BLOB NOT NULL PRIMARY KEY "
                                             "CHECK(typeof(id) = 'blob' AND length(id) = 16),"
                                             "owner_user_id INTEGER NOT NULL "
                                             "CHECK(typeof(owner_user_id) = 'integer' "
                                             "AND owner_user_id >= 0 "
                                             "AND owner_user_id <= 4294967295),"
                                             "original_name BLOB NOT NULL "
                                             "CHECK(typeof(original_name) = 'blob' "
                                             "AND length(original_name) >= 1),"
                                             "size INTEGER NOT NULL "
                                             "CHECK(typeof(size) = 'integer' AND size >= 0),"
                                             "crc32 INTEGER NOT NULL "
                                             "CHECK(typeof(crc32) = 'integer' "
                                             "AND crc32 >= 0 "
                                             "AND crc32 <= 4294967295),"
                                             "created_at INTEGER NOT NULL "
                                             "CHECK(typeof(created_at) = 'integer' AND created_at >= 0)"
                                             ") WITHOUT ROWID;";

static const char FILE_REPOSITORY_INSERT_SQL[] = "INSERT INTO files ("
                                                 "id, owner_user_id, original_name, size, crc32, created_at"
                                                 ") VALUES (?1, ?2, ?3, ?4, ?5, ?6);";

static const char FILE_REPOSITORY_FIND_SQL[] = "SELECT owner_user_id, original_name, size, crc32, created_at "
                                               "FROM files WHERE id = ?1;";

static const char FILE_REPOSITORY_REMOVE_SQL[] = "DELETE FROM files WHERE id = ?1;";

static void file_repository_statement_reset(sqlite3_stmt* statement) {
    if(statement == NULL) {
        return;
    }

    (void)sqlite3_reset(statement);
    (void)sqlite3_clear_bindings(statement);
}

static bool file_repository_is_ready(const FileRepository* repository) {
    return repository != NULL && repository->db != NULL && repository->insert_statement != NULL &&
           repository->find_statement != NULL && repository->remove_statement != NULL;
}

static bool file_repository_name_is_valid(const char* file_name, uint16_t file_name_len) {
    if(file_name == NULL || file_name_len == 0U || file_name_len > FILE_MAX_NAME_LENGTH) {
        return false;
    }

    if(memchr(file_name, '\0', file_name_len) != NULL) {
        return false;
    }

    return file_name[file_name_len] == '\0';
}

static bool file_repository_metadata_is_valid(const FileMetadata* metadata) {
    return metadata != NULL && !file_id_is_zero(&metadata->file_id) && metadata->file_size <= FILE_MAX_SIZE &&
           metadata->created_at >= 0 && file_repository_name_is_valid(metadata->file_name, metadata->file_name_len);
}

static FileRepositoryResult file_repository_prepare(sqlite3* db, const char* sql, sqlite3_stmt** statement) {
    if(sqlite3_prepare_v2(db, sql, -1, statement, NULL) != SQLITE_OK) {
        return FILE_REPOSITORY_DATABASE_ERROR;
    }

    return FILE_REPOSITORY_OK;
}

static FileRepositoryResult file_repository_bind_id(
    sqlite3_stmt* statement,
    int           parameter_index,
    const FileId* file_id
) {
    if(file_id == NULL || file_id_is_zero(file_id)) {
        return FILE_REPOSITORY_INVALID_ARGUMENT;
    }

    if(sqlite3_bind_blob(statement, parameter_index, file_id->bytes, (int)FILE_ID_SIZE, SQLITE_TRANSIENT) !=
        SQLITE_OK) {
        return FILE_REPOSITORY_DATABASE_ERROR;
    }

    return FILE_REPOSITORY_OK;
}

static FileRepositoryResult file_repository_read_metadata(
    sqlite3_stmt* statement,
    const FileId* file_id,
    FileMetadata* metadata
) {
    if(sqlite3_column_type(statement, 0) != SQLITE_INTEGER || sqlite3_column_type(statement, 1) != SQLITE_BLOB ||
        sqlite3_column_type(statement, 2) != SQLITE_INTEGER || sqlite3_column_type(statement, 3) != SQLITE_INTEGER ||
        sqlite3_column_type(statement, 4) != SQLITE_INTEGER) {
        return FILE_REPOSITORY_CORRUPT_DATA;
    }

    const sqlite3_int64 owner_user_id = sqlite3_column_int64(statement, 0);
    const sqlite3_int64 file_size     = sqlite3_column_int64(statement, 2);
    const sqlite3_int64 file_crc32    = sqlite3_column_int64(statement, 3);
    const sqlite3_int64 created_at    = sqlite3_column_int64(statement, 4);

    const int   file_name_len = sqlite3_column_bytes(statement, 1);
    const void* file_name     = sqlite3_column_blob(statement, 1);

    if(owner_user_id < 0 || owner_user_id > (sqlite3_int64)UINT32_MAX || file_size < 0 ||
        (uint64_t)file_size > FILE_MAX_SIZE || file_crc32 < 0 || file_crc32 > (sqlite3_int64)UINT32_MAX ||
        created_at < 0 || file_name_len <= 0 || file_name_len > (int)FILE_MAX_NAME_LENGTH || file_name == NULL ||
        memchr(file_name, '\0', (size_t)file_name_len) != NULL) {
        return FILE_REPOSITORY_CORRUPT_DATA;
    }

    *metadata = (FileMetadata){0};

    metadata->file_id       = *file_id;
    metadata->owner_user_id = (uint32_t)owner_user_id;
    metadata->file_size     = (uint64_t)file_size;
    metadata->file_crc32    = (uint32_t)file_crc32;
    metadata->created_at    = (int64_t)created_at;
    metadata->file_name_len = (uint16_t)file_name_len;

    memcpy(metadata->file_name, file_name, (size_t)file_name_len);

    metadata->file_name[file_name_len] = '\0';

    return FILE_REPOSITORY_OK;
}

void file_repository_reset(FileRepository* repository) {
    if(repository == NULL) {
        return;
    }

    repository->db               = NULL;
    repository->insert_statement = NULL;
    repository->find_statement   = NULL;
    repository->remove_statement = NULL;
}

FileRepositoryResult file_repository_init(FileRepository* repository, sqlite3* db) {
    if(repository == NULL || db == NULL) {
        return FILE_REPOSITORY_INVALID_ARGUMENT;
    }

    file_repository_reset(repository);
    repository->db = db;

    if(sqlite3_exec(db, FILE_REPOSITORY_SCHEMA, NULL, NULL, NULL) != SQLITE_OK) {
        file_repository_reset(repository);
        return FILE_REPOSITORY_DATABASE_ERROR;
    }

    FileRepositoryResult result =
        file_repository_prepare(db, FILE_REPOSITORY_INSERT_SQL, &repository->insert_statement);

    if(result == FILE_REPOSITORY_OK) {
        result = file_repository_prepare(db, FILE_REPOSITORY_FIND_SQL, &repository->find_statement);
    }

    if(result == FILE_REPOSITORY_OK) {
        result = file_repository_prepare(db, FILE_REPOSITORY_REMOVE_SQL, &repository->remove_statement);
    }

    if(result != FILE_REPOSITORY_OK) {
        file_repository_destroy(repository);
        return result;
    }

    return FILE_REPOSITORY_OK;
}

void file_repository_destroy(FileRepository* repository) {
    if(repository == NULL) {
        return;
    }

    if(repository->insert_statement != NULL) {
        (void)sqlite3_finalize(repository->insert_statement);
    }

    if(repository->find_statement != NULL) {
        (void)sqlite3_finalize(repository->find_statement);
    }

    if(repository->remove_statement != NULL) {
        (void)sqlite3_finalize(repository->remove_statement);
    }

    file_repository_reset(repository);
}

FileRepositoryResult file_repository_insert(FileRepository* repository, const FileMetadata* metadata) {
    if(!file_repository_is_ready(repository) || !file_repository_metadata_is_valid(metadata)) {
        return FILE_REPOSITORY_INVALID_ARGUMENT;
    }

    sqlite3_stmt* statement = repository->insert_statement;

    FileRepositoryResult result = file_repository_bind_id(statement, 1, &metadata->file_id);

    if(result == FILE_REPOSITORY_OK &&
        sqlite3_bind_int64(statement, 2, (sqlite3_int64)metadata->owner_user_id) != SQLITE_OK) {
        result = FILE_REPOSITORY_DATABASE_ERROR;
    }

    if(result == FILE_REPOSITORY_OK &&
        sqlite3_bind_blob(statement, 3, metadata->file_name, (int)metadata->file_name_len, SQLITE_TRANSIENT) !=
            SQLITE_OK) {
        result = FILE_REPOSITORY_DATABASE_ERROR;
    }

    if(result == FILE_REPOSITORY_OK &&
        sqlite3_bind_int64(statement, 4, (sqlite3_int64)metadata->file_size) != SQLITE_OK) {
        result = FILE_REPOSITORY_DATABASE_ERROR;
    }

    if(result == FILE_REPOSITORY_OK &&
        sqlite3_bind_int64(statement, 5, (sqlite3_int64)metadata->file_crc32) != SQLITE_OK) {
        result = FILE_REPOSITORY_DATABASE_ERROR;
    }

    if(result == FILE_REPOSITORY_OK &&
        sqlite3_bind_int64(statement, 6, (sqlite3_int64)metadata->created_at) != SQLITE_OK) {
        result = FILE_REPOSITORY_DATABASE_ERROR;
    }

    if(result == FILE_REPOSITORY_OK) {
        const int step_result = sqlite3_step(statement);

        if(step_result != SQLITE_DONE) {
            const int extended_result = sqlite3_extended_errcode(repository->db);

            if(extended_result == SQLITE_CONSTRAINT_PRIMARYKEY || extended_result == SQLITE_CONSTRAINT_UNIQUE) {
                result = FILE_REPOSITORY_ALREADY_EXISTS;
            }
            else {
                result = FILE_REPOSITORY_DATABASE_ERROR;
            }
        }
    }

    file_repository_statement_reset(statement);

    return result;
}

FileRepositoryResult file_repository_find(FileRepository* repository, const FileId* file_id, FileMetadata* metadata) {
    if(metadata != NULL) {
        *metadata = (FileMetadata){0};
    }

    if(!file_repository_is_ready(repository) || file_id == NULL || file_id_is_zero(file_id) || metadata == NULL) {
        return FILE_REPOSITORY_INVALID_ARGUMENT;
    }

    sqlite3_stmt* statement = repository->find_statement;

    FileRepositoryResult result = file_repository_bind_id(statement, 1, file_id);

    if(result == FILE_REPOSITORY_OK) {
        const int step_result = sqlite3_step(statement);

        if(step_result == SQLITE_ROW) {
            result = file_repository_read_metadata(statement, file_id, metadata);

            if(result == FILE_REPOSITORY_OK && sqlite3_step(statement) != SQLITE_DONE) {
                result = FILE_REPOSITORY_DATABASE_ERROR;
            }
        }
        else if(step_result == SQLITE_DONE) {
            result = FILE_REPOSITORY_NOT_FOUND;
        }
        else {
            result = FILE_REPOSITORY_DATABASE_ERROR;
        }
    }

    file_repository_statement_reset(statement);

    if(result != FILE_REPOSITORY_OK) {
        *metadata = (FileMetadata){0};
    }

    return result;
}

FileRepositoryResult file_repository_remove(FileRepository* repository, const FileId* file_id) {
    if(!file_repository_is_ready(repository) || file_id == NULL || file_id_is_zero(file_id)) {
        return FILE_REPOSITORY_INVALID_ARGUMENT;
    }

    sqlite3_stmt* statement = repository->remove_statement;

    FileRepositoryResult result = file_repository_bind_id(statement, 1, file_id);

    if(result == FILE_REPOSITORY_OK) {
        const int step_result = sqlite3_step(statement);

        if(step_result != SQLITE_DONE) {
            result = FILE_REPOSITORY_DATABASE_ERROR;
        }
        else if(sqlite3_changes(repository->db) == 0) {
            result = FILE_REPOSITORY_NOT_FOUND;
        }
    }

    file_repository_statement_reset(statement);

    return result;
}

const char* file_repository_result_to_string(FileRepositoryResult result) {
    switch(result) {
        case FILE_REPOSITORY_OK: return "ok";
        case FILE_REPOSITORY_NOT_FOUND: return "file metadata not found";
        case FILE_REPOSITORY_ALREADY_EXISTS: return "file metadata already exists";
        case FILE_REPOSITORY_INVALID_ARGUMENT: return "invalid file repository argument";
        case FILE_REPOSITORY_DATABASE_ERROR: return "file repository database error";
        case FILE_REPOSITORY_CORRUPT_DATA: return "corrupt file metadata";
        default: return "unknown file repository error";
    }
}
