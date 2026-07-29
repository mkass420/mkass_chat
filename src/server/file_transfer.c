#define _POSIX_C_SOURCE 200809L

#include "server/file_transfer.h"

#include "common/file_id.h"
#include "config.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>

static uint64_t file_transfer_monotonic_time_ms(void) {
    struct timespec time_value;

    if(clock_gettime(CLOCK_MONOTONIC, &time_value) != 0) {
        return 0U;
    }

    return (uint64_t)time_value.tv_sec * 1000U + (uint64_t)time_value.tv_nsec / 1000000U;
}

static bool file_transfer_name_is_valid(const uint8_t* file_name, uint16_t file_name_len) {
    return file_name != NULL && file_name_len != 0U && file_name_len <= FILE_MAX_NAME_LENGTH &&
           memchr(file_name, '\0', file_name_len) == NULL;
}

static void file_upload_state_reset(FileUploadState* upload) {
    if(upload == NULL) {
        return;
    }

    memset(upload, 0, sizeof(*upload));

    upload->status  = FILE_UPLOAD_IDLE;
    upload->file_fd = -1;
}

static void file_download_state_reset(FileDownloadState* download) {
    if(download == NULL) {
        return;
    }

    memset(download, 0, sizeof(*download));

    download->status  = FILE_DOWNLOAD_IDLE;
    download->file_fd = -1;
}

void file_transfer_state_init(FileTransferState* state) {
    if(state == NULL) {
        return;
    }

    file_upload_state_reset(&state->upload);
    file_download_state_reset(&state->download);
}

bool file_upload_is_active(const FileUploadState* upload) {
    return upload != NULL && upload->status != FILE_UPLOAD_IDLE;
}

bool file_download_is_active(const FileDownloadState* download) {
    return download != NULL && download->status != FILE_DOWNLOAD_IDLE;
}

FileTransferResult file_upload_begin(
    FileStorage*     storage,
    FileUploadState* upload,
    uint64_t         file_size,
    const uint8_t*   file_name,
    uint16_t         file_name_len
) {
    if(storage == NULL || upload == NULL || !file_transfer_name_is_valid(file_name, file_name_len)) {
        return FILE_TRANSFER_INVALID_ARGUMENT;
    }

    if(file_upload_is_active(upload)) {
        return FILE_TRANSFER_BUSY;
    }

    if(file_size > FILE_MAX_SIZE) {
        return FILE_TRANSFER_FILE_TOO_LARGE;
    }

    FileId file_id;
    int    file_fd = -1;

    for(unsigned int attempt = 0U; attempt < FILE_ID_GENERATION_ATTEMPTS; ++attempt) {
        if(file_id_generate(&file_id) != 0) {
            return FILE_TRANSFER_STORAGE_ERROR;
        }

        file_fd = file_storage_create_temporary(storage, &file_id);

        if(file_fd >= 0) {
            break;
        }

        if(errno != EEXIST) {
            return FILE_TRANSFER_STORAGE_ERROR;
        }
    }

    if(file_fd < 0) {
        errno = EEXIST;
        return FILE_TRANSFER_STORAGE_ERROR;
    }

    file_upload_state_reset(upload);

    upload->status           = FILE_UPLOAD_READY;
    upload->file_id          = file_id;
    upload->file_fd          = file_fd;
    upload->expected_size    = file_size;
    upload->running_crc32    = (uint32_t)crc32(0L, Z_NULL, 0);
    upload->last_activity_ms = file_transfer_monotonic_time_ms();

    memcpy(upload->file_name, file_name, file_name_len);

    upload->file_name[file_name_len] = '\0';

    return FILE_TRANSFER_OK;
}

FileTransferResult file_upload_write(FileUploadState* upload, const uint8_t* data, uint32_t data_len) {
    if(upload == NULL || data == NULL || data_len == 0U) {
        return FILE_TRANSFER_INVALID_ARGUMENT;
    }

    if(upload->status != FILE_UPLOAD_READY || upload->file_fd < 0) {
        return FILE_TRANSFER_INVALID_STATE;
    }

    if(data_len > FILE_CHUNK_DATA_SIZE) {
        return FILE_TRANSFER_INVALID_CHUNK;
    }

    if(upload->committed_size > upload->expected_size) {
        upload->status = FILE_UPLOAD_FAILED;
        return FILE_TRANSFER_INVALID_STATE;
    }

    if((uint64_t)data_len > upload->expected_size - upload->committed_size) {
        return FILE_TRANSFER_INVALID_CHUNK;
    }

    size_t       written    = 0U;
    const size_t total_size = (size_t)data_len;

    while(written < total_size) {
        const off_t offset = (off_t)(upload->committed_size + (uint64_t)written);

        const ssize_t result = pwrite(upload->file_fd, data + written, total_size - written, offset);

        if(result > 0) {
            written += (size_t)result;
            continue;
        }

        if(result < 0 && errno == EINTR) {
            continue;
        }

        if(result == 0) {
            errno = EIO;
        }

        upload->status = FILE_UPLOAD_FAILED;
        return FILE_TRANSFER_STORAGE_ERROR;
    }

    upload->running_crc32 = (uint32_t)crc32((uLong)upload->running_crc32, data, (uInt)data_len);

    upload->committed_size += (uint64_t)data_len;

    upload->last_activity_ms = file_transfer_monotonic_time_ms();

    return FILE_TRANSFER_OK;
}

FileTransferResult file_upload_finish(
    FileStorage*     storage,
    FileUploadState* upload,
    FileId*          completed_file_id,
    uint32_t*        completed_crc32
) {
    if(storage == NULL || upload == NULL || completed_file_id == NULL) {
        return FILE_TRANSFER_INVALID_ARGUMENT;
    }

    if(upload->status != FILE_UPLOAD_READY || upload->file_fd < 0) {
        return FILE_TRANSFER_INVALID_STATE;
    }

    if(upload->committed_size != upload->expected_size) {
        return FILE_TRANSFER_INCOMPLETE;
    }

    const FileId   file_id    = upload->file_id;
    const uint32_t file_crc32 = upload->running_crc32;

    if(fsync(upload->file_fd) != 0) {
        const int saved_errno = errno;

        file_upload_abort(storage, upload);
        errno = saved_errno;
        return FILE_TRANSFER_STORAGE_ERROR;
    }

    if(close(upload->file_fd) != 0) {
        const int saved_errno = errno;

        upload->file_fd = -1;
        (void)file_storage_abort(storage, &file_id);
        file_upload_state_reset(upload);
        errno = saved_errno;
        return FILE_TRANSFER_STORAGE_ERROR;
    }

    upload->file_fd = -1;

    if(file_storage_finalize(storage, &file_id) != 0) {
        const int saved_errno = errno;

        (void)file_storage_abort(storage, &file_id);
        (void)file_storage_remove_object(storage, &file_id);
        file_upload_state_reset(upload);
        errno = saved_errno;
        return FILE_TRANSFER_STORAGE_ERROR;
    }

    *completed_file_id = file_id;

    if(completed_crc32 != NULL) {
        *completed_crc32 = file_crc32;
    }

    file_upload_state_reset(upload);

    return FILE_TRANSFER_OK;
}

void file_upload_abort(FileStorage* storage, FileUploadState* upload) {
    if(upload == NULL) {
        return;
    }

    if(upload->file_fd >= 0) {
        (void)close(upload->file_fd);
        upload->file_fd = -1;
    }

    if(storage != NULL && upload->status != FILE_UPLOAD_IDLE && !file_id_is_zero(&upload->file_id)) {
        (void)file_storage_abort(storage, &upload->file_id);
    }

    file_upload_state_reset(upload);
}

FileTransferResult file_download_begin(
    FileStorage*       storage,
    FileDownloadState* download,
    const FileId*      file_id,
    const uint8_t*     file_name,
    uint16_t           file_name_len
) {
    if(storage == NULL || download == NULL || file_id == NULL || file_id_is_zero(file_id) ||
        !file_transfer_name_is_valid(file_name, file_name_len)) {
        return FILE_TRANSFER_INVALID_ARGUMENT;
    }

    if(file_download_is_active(download)) {
        return FILE_TRANSFER_BUSY;
    }

    const int file_fd = file_storage_open_object(storage, file_id);

    if(file_fd < 0) {
        return FILE_TRANSFER_STORAGE_ERROR;
    }

    struct stat file_stat;

    if(fstat(file_fd, &file_stat) != 0 || !S_ISREG(file_stat.st_mode) || file_stat.st_size < 0 ||
        (uint64_t)file_stat.st_size > FILE_MAX_SIZE) {
        const int saved_errno = errno == 0 ? EINVAL : errno;

        (void)close(file_fd);
        errno = saved_errno;
        return FILE_TRANSFER_STORAGE_ERROR;
    }

    file_download_state_reset(download);

    download->status           = FILE_DOWNLOAD_READY;
    download->file_id          = *file_id;
    download->file_fd          = file_fd;
    download->file_size        = (uint64_t)file_stat.st_size;
    download->last_activity_ms = file_transfer_monotonic_time_ms();

    memcpy(download->file_name, file_name, file_name_len);

    download->file_name[file_name_len] = '\0';

    return FILE_TRANSFER_OK;
}

FileTransferResult file_download_read(
    FileDownloadState* download,
    uint8_t*           output,
    uint32_t           output_capacity,
    uint32_t*          output_len,
    bool*              is_complete
) {
    if(output_len != NULL) {
        *output_len = 0U;
    }

    if(is_complete != NULL) {
        *is_complete = false;
    }

    if(download == NULL || output == NULL || output_capacity == 0U || output_len == NULL || is_complete == NULL) {
        return FILE_TRANSFER_INVALID_ARGUMENT;
    }

    if(download->status != FILE_DOWNLOAD_READY || download->file_fd < 0) {
        return FILE_TRANSFER_INVALID_STATE;
    }

    if(download->current_offset > download->file_size) {
        download->status = FILE_DOWNLOAD_FAILED;
        return FILE_TRANSFER_INVALID_STATE;
    }

    const uint64_t remaining = download->file_size - download->current_offset;

    if(remaining == 0U) {
        *is_complete = true;
        return FILE_TRANSFER_OK;
    }

    uint64_t requested = remaining;

    if(requested > FILE_CHUNK_DATA_SIZE) {
        requested = FILE_CHUNK_DATA_SIZE;
    }

    if(requested > output_capacity) {
        requested = output_capacity;
    }

    size_t       read_size      = 0U;
    const size_t requested_size = (size_t)requested;

    while(read_size < requested_size) {
        const off_t offset = (off_t)(download->current_offset + (uint64_t)read_size);

        const ssize_t result = pread(download->file_fd, output + read_size, requested_size - read_size, offset);

        if(result > 0) {
            read_size += (size_t)result;
            continue;
        }

        if(result < 0 && errno == EINTR) {
            continue;
        }

        if(result == 0) {
            errno = EIO;
        }

        download->status = FILE_DOWNLOAD_FAILED;
        return FILE_TRANSFER_STORAGE_ERROR;
    }

    download->current_offset += (uint64_t)read_size;
    download->last_activity_ms = file_transfer_monotonic_time_ms();

    *output_len  = (uint32_t)read_size;
    *is_complete = download->current_offset == download->file_size;

    return FILE_TRANSFER_OK;
}

FileTransferResult file_download_finish(FileDownloadState* download) {
    if(download == NULL) {
        return FILE_TRANSFER_INVALID_ARGUMENT;
    }

    if(download->status != FILE_DOWNLOAD_READY || download->file_fd < 0) {
        return FILE_TRANSFER_INVALID_STATE;
    }

    if(download->current_offset != download->file_size) {
        return FILE_TRANSFER_INCOMPLETE;
    }

    if(close(download->file_fd) != 0) {
        const int saved_errno = errno;

        download->file_fd = -1;
        file_download_state_reset(download);
        errno = saved_errno;
        return FILE_TRANSFER_STORAGE_ERROR;
    }

    file_download_state_reset(download);

    return FILE_TRANSFER_OK;
}

void file_download_abort(FileDownloadState* download) {
    if(download == NULL) {
        return;
    }

    if(download->file_fd >= 0) {
        (void)close(download->file_fd);
    }

    file_download_state_reset(download);
}

void file_transfer_abort_all(FileStorage* storage, FileTransferState* state) {
    if(state == NULL) {
        return;
    }

    file_upload_abort(storage, &state->upload);
    file_download_abort(&state->download);
}

const char* file_transfer_result_to_string(FileTransferResult result) {
    switch(result) {
        case FILE_TRANSFER_OK: return "ok";
        case FILE_TRANSFER_INVALID_ARGUMENT: return "invalid argument";
        case FILE_TRANSFER_INVALID_STATE: return "invalid transfer state";
        case FILE_TRANSFER_BUSY: return "file transfer is already active";
        case FILE_TRANSFER_FILE_TOO_LARGE: return "file is too large";
        case FILE_TRANSFER_INVALID_CHUNK: return "invalid file chunk";
        case FILE_TRANSFER_INCOMPLETE: return "file transfer is incomplete";
        case FILE_TRANSFER_STORAGE_ERROR: return "file storage error";
        default: return "unknown file transfer error";
    }
}
