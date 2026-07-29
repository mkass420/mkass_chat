#ifndef FILE_STORAGE_H
#define FILE_STORAGE_H

#include "common/file_id.h"

#include <stdbool.h>

typedef struct {
    int root_dir_fd;
    int temporary_dir_fd;
    int objects_dir_fd;
} FileStorage;

void file_storage_reset(FileStorage* storage);

int file_storage_init(FileStorage* storage, const char* root_path);

void file_storage_destroy(FileStorage* storage);

int file_storage_create_temporary(const FileStorage* storage, const FileId* file_id);

int file_storage_open_object(const FileStorage* storage, const FileId* file_id);

int file_storage_finalize(const FileStorage* storage, const FileId* file_id);

int file_storage_abort(const FileStorage* storage, const FileId* file_id);

int file_storage_remove_object(const FileStorage* storage, const FileId* file_id);

bool file_storage_is_initialized(const FileStorage* storage);

#endif
