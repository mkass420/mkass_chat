#ifndef FILE_H
#define FILE_H

#include "config.h"

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t bytes[FILE_ID_SIZE];
} FileId;

typedef uint64_t FileTransferId;

typedef struct {
    bool           active;
    FileTransferId transfer_id;
    FileId         file_id;
    int            file_fd;
    uint64_t       expected_size;
    uint64_t       received_size;
    uint32_t       running_crc32;
    uint64_t       last_activity_ms;
    char           original_name[FILE_MAX_NAME_LENGTH + 1U];
    char           mime_type[FILE_MAX_MIME_LENGTH + 1U];
} FileUploadState;

typedef struct {
    bool           active;
    FileTransferId transfer_id;
    FileId         file_id;
    int            file_fd;
    uint64_t       file_size;
    uint64_t       current_offset;
    uint32_t       file_crc32;
    uint64_t       last_activity_ms;
} FileDownloadState;

typedef struct {
    FileUploadState   upload;
    FileDownloadState download;
} FileTransferState;

#endif
