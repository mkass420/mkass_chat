#ifndef FILE_REPOSITORY_H
#define FILE_REPOSITORY_H

#include "common/file_id.h"
#include "config.h"

#include <sqlite3.h>
#include <stdint.h>

typedef struct {
    FileId   file_id;
    uint32_t owner_user_id;
    uint64_t file_size;
    uint32_t file_crc32;
    int64_t  created_at;
    uint16_t file_name_len;
    char     file_name[FILE_MAX_NAME_LENGTH + 1U];
} FileMetadata;

typedef enum {
    FILE_REPOSITORY_OK = 0,
    FILE_REPOSITORY_NOT_FOUND,
    FILE_REPOSITORY_ALREADY_EXISTS,
    FILE_REPOSITORY_INVALID_ARGUMENT,
    FILE_REPOSITORY_DATABASE_ERROR,
    FILE_REPOSITORY_CORRUPT_DATA
} FileRepositoryResult;

typedef struct {
    sqlite3*      db;
    sqlite3_stmt* insert_statement;
    sqlite3_stmt* find_statement;
    sqlite3_stmt* remove_statement;
} FileRepository;

void                 file_repository_reset(FileRepository* repository);
FileRepositoryResult file_repository_init(FileRepository* repository, sqlite3* db);
void                 file_repository_destroy(FileRepository* repository);
FileRepositoryResult file_repository_insert(FileRepository* repository, const FileMetadata* metadata);
FileRepositoryResult file_repository_find(FileRepository* repository, const FileId* file_id, FileMetadata* metadata);
FileRepositoryResult file_repository_remove(FileRepository* repository, const FileId* file_id);
const char*          file_repository_result_to_string(FileRepositoryResult result);

#endif
