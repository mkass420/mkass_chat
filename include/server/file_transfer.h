#ifndef FILE_TRANSFER_H
#define FILE_TRANSFER_H

#include "common/file_id.h"
#include "server/file_storage.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    FILE_UPLOAD_IDLE = 0,
    FILE_UPLOAD_READY,
    FILE_UPLOAD_WRITE_PENDING,
    FILE_UPLOAD_SYNC_PENDING,
    FILE_UPLOAD_FAILED
} FileUploadStatus;

typedef enum {
    FILE_DOWNLOAD_IDLE = 0,
    FILE_DOWNLOAD_READY,
    FILE_DOWNLOAD_READ_PENDING,
    FILE_DOWNLOAD_FAILED
} FileDownloadStatus;

typedef struct {
    FileUploadStatus status;
    FileId           file_id;
    int              file_fd;
    uint64_t         expected_size;
    uint64_t         committed_size;
    uint32_t         running_crc32;
    uint64_t         last_activity_ms;
    char             file_name[FILE_MAX_NAME_LENGTH + 1U];
} FileUploadState;

typedef struct {
    FileDownloadStatus status;
    FileId             file_id;
    int                file_fd;
    uint64_t           file_size;
    uint64_t           current_offset;
    uint64_t           last_activity_ms;
    char               file_name[FILE_MAX_NAME_LENGTH + 1U];
} FileDownloadState;

typedef struct {
    FileUploadState   upload;
    FileDownloadState download;
} FileTransferState;

typedef enum {
    FILE_TRANSFER_OK = 0,
    FILE_TRANSFER_INVALID_ARGUMENT,
    FILE_TRANSFER_INVALID_STATE,
    FILE_TRANSFER_BUSY,
    FILE_TRANSFER_FILE_TOO_LARGE,
    FILE_TRANSFER_INVALID_CHUNK,
    FILE_TRANSFER_INCOMPLETE,
    FILE_TRANSFER_STORAGE_ERROR
} FileTransferResult;

void file_transfer_state_init(FileTransferState* state);

bool file_upload_is_active(const FileUploadState* upload);
bool file_download_is_active(const FileDownloadState* download);

FileTransferResult file_upload_begin(
    FileStorage*     storage,
    FileUploadState* upload,
    uint64_t         file_size,
    const uint8_t*   file_name,
    uint16_t         file_name_len
);

FileTransferResult file_upload_write(FileUploadState* upload, const uint8_t* data, uint32_t data_len);

FileTransferResult file_upload_finish(
    FileStorage*     storage,
    FileUploadState* upload,
    FileId*          completed_file_id,
    uint32_t*        completed_crc32
);

void file_upload_abort(FileStorage* storage, FileUploadState* upload);

FileTransferResult file_download_begin(
    FileStorage*       storage,
    FileDownloadState* download,
    const FileId*      file_id,
    const uint8_t*     file_name,
    uint16_t           file_name_len
);

FileTransferResult file_download_read(
    FileDownloadState* download,
    uint8_t*           output,
    uint32_t           output_capacity,
    uint32_t*          output_len,
    bool*              is_complete
);

FileTransferResult file_download_finish(FileDownloadState* download);

void file_download_abort(FileDownloadState* download);

void file_transfer_abort_all(FileStorage* storage, FileTransferState* state);

const char* file_transfer_result_to_string(FileTransferResult result);

#endif
