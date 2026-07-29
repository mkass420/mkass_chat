#define _GNU_SOURCE

#include "server/file_storage.h"

#include "common/file_id.h"
#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>


static int file_storage_make_directories(const char* path) {
    if(path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    const size_t path_length = strnlen(path, PATH_MAX);

    if(path_length == 0U || path_length >= PATH_MAX) {
        errno = ENAMETOOLONG;
        return -1;
    }

    char buffer[PATH_MAX];

    memcpy(buffer, path, path_length + 1U);

    for(size_t i = 1U; i < path_length; ++i) {
        if(buffer[i] != '/') {
            continue;
        }

        if(buffer[i - 1U] == '/') {
            continue;
        }

        buffer[i] = '\0';

        if(mkdir(buffer, FILE_STORAGE_DIRECTORY_MODE) != 0 && errno != EEXIST) {
            return -1;
        }

        buffer[i] = '/';
    }

    if(mkdir(buffer, FILE_STORAGE_DIRECTORY_MODE) != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

static int file_storage_ensure_directory(int parent_fd, const char* name) {
    if(mkdirat(parent_fd, name, FILE_STORAGE_DIRECTORY_MODE) != 0 && errno != EEXIST) {
        return -1;
    }

    return openat(parent_fd, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
}

static bool file_storage_arguments_are_valid(const FileStorage* storage, const FileId* file_id) {
    return file_storage_is_initialized(storage) && file_id != NULL && !file_id_is_zero(file_id);
}

static void file_storage_build_name(
    const FileId* file_id,
    const char*   suffix,
    char          output[FILE_STORAGE_NAME_CAPACITY]
) {
    file_id_to_hex(file_id, output);

    const size_t hex_length    = FILE_ID_HEX_SIZE;
    const size_t suffix_length = strlen(suffix);

    memcpy(output + hex_length, suffix, suffix_length + 1U);
}

void file_storage_reset(FileStorage* storage) {
    if(storage == NULL) {
        return;
    }

    storage->root_dir_fd      = -1;
    storage->temporary_dir_fd = -1;
    storage->objects_dir_fd   = -1;
}

bool file_storage_is_initialized(const FileStorage* storage) {
    return storage != NULL && storage->root_dir_fd >= 0 && storage->temporary_dir_fd >= 0 &&
           storage->objects_dir_fd >= 0;
}

int file_storage_init(FileStorage* storage, const char* root_path) {
    if(storage == NULL || root_path == NULL || root_path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    file_storage_reset(storage);

    if(file_storage_make_directories(root_path) != 0) {
        return -1;
    }

    storage->root_dir_fd = open(root_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);

    if(storage->root_dir_fd < 0) {
        return -1;
    }

    storage->temporary_dir_fd = file_storage_ensure_directory(storage->root_dir_fd, FILE_STORAGE_TEMPORARY_DIRECTORY);

    if(storage->temporary_dir_fd < 0) {
        const int saved_errno = errno;

        file_storage_destroy(storage);
        errno = saved_errno;
        return -1;
    }

    storage->objects_dir_fd = file_storage_ensure_directory(storage->root_dir_fd, FILE_STORAGE_OBJECTS_DIRECTORY);

    if(storage->objects_dir_fd < 0) {
        const int saved_errno = errno;

        file_storage_destroy(storage);
        errno = saved_errno;
        return -1;
    }

    return 0;
}

void file_storage_destroy(FileStorage* storage) {
    if(storage == NULL) {
        return;
    }

    if(storage->objects_dir_fd >= 0) {
        (void)close(storage->objects_dir_fd);
    }

    if(storage->temporary_dir_fd >= 0) {
        (void)close(storage->temporary_dir_fd);
    }

    if(storage->root_dir_fd >= 0) {
        (void)close(storage->root_dir_fd);
    }

    file_storage_reset(storage);
}

int file_storage_create_temporary(const FileStorage* storage, const FileId* file_id) {
    if(!file_storage_arguments_are_valid(storage, file_id)) {
        errno = EINVAL;
        return -1;
    }

    char name[FILE_STORAGE_NAME_CAPACITY];

    file_storage_build_name(file_id, FILE_STORAGE_TEMPORARY_SUFFIX, name);

    return openat(
        storage->temporary_dir_fd, name, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, FILE_STORAGE_FILE_MODE
    );
}

int file_storage_open_object(const FileStorage* storage, const FileId* file_id) {
    if(!file_storage_arguments_are_valid(storage, file_id)) {
        errno = EINVAL;
        return -1;
    }

    char name[FILE_STORAGE_NAME_CAPACITY];

    file_storage_build_name(file_id, FILE_STORAGE_OBJECT_SUFFIX, name);

    return openat(storage->objects_dir_fd, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
}

int file_storage_finalize(const FileStorage* storage, const FileId* file_id) {
    if(!file_storage_arguments_are_valid(storage, file_id)) {
        errno = EINVAL;
        return -1;
    }

    char temporary_name[FILE_STORAGE_NAME_CAPACITY];
    char object_name[FILE_STORAGE_NAME_CAPACITY];

    file_storage_build_name(file_id, FILE_STORAGE_TEMPORARY_SUFFIX, temporary_name);

    file_storage_build_name(file_id, FILE_STORAGE_OBJECT_SUFFIX, object_name);

    if(renameat2(storage->temporary_dir_fd, temporary_name, storage->objects_dir_fd, object_name, RENAME_NOREPLACE) !=
        0) {
        return -1;
    }

    if(fsync(storage->objects_dir_fd) != 0) {
        return -1;
    }

    if(fsync(storage->temporary_dir_fd) != 0) {
        return -1;
    }

    return 0;
}

int file_storage_abort(const FileStorage* storage, const FileId* file_id) {
    if(!file_storage_arguments_are_valid(storage, file_id)) {
        errno = EINVAL;
        return -1;
    }

    char name[FILE_STORAGE_NAME_CAPACITY];

    file_storage_build_name(file_id, FILE_STORAGE_TEMPORARY_SUFFIX, name);

    if(unlinkat(storage->temporary_dir_fd, name, 0) != 0 && errno != ENOENT) {
        return -1;
    }

    return 0;
}

int file_storage_remove_object(const FileStorage* storage, const FileId* file_id) {
    if(!file_storage_arguments_are_valid(storage, file_id)) {
        errno = EINVAL;
        return -1;
    }

    char name[FILE_STORAGE_NAME_CAPACITY];

    file_storage_build_name(file_id, FILE_STORAGE_OBJECT_SUFFIX, name);

    if(unlinkat(storage->objects_dir_fd, name, 0) != 0 && errno != ENOENT) {
        return -1;
    }

    return 0;
}
