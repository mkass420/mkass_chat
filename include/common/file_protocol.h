#ifndef FILE_PROTOCOL_H
#define FILE_PROTOCOL_H

#include "common/file_id.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t file_size;

    const uint8_t* file_name;
    uint16_t       file_name_len;
} FileUploadBeginRequest;

typedef struct {
    FileId file_id;
} FileUploadFinishResponse;

typedef struct {
    FileId file_id;
} FileDownloadBeginRequest;

typedef struct {
    uint64_t file_size;

    const uint8_t* file_name;
    uint16_t       file_name_len;
} FileDownloadBeginResponse;

bool file_upload_begin_request_decode(const uint8_t* payload, uint32_t payload_len, FileUploadBeginRequest* request);

bool file_upload_finish_response_encode(
    const FileUploadFinishResponse* response,
    uint8_t*                        output,
    uint32_t                        output_capacity,
    uint32_t*                       output_len
);

bool file_download_begin_request_decode(
    const uint8_t*            payload,
    uint32_t                  payload_len,
    FileDownloadBeginRequest* request
);

bool file_download_begin_response_encode(
    const FileDownloadBeginResponse* response,
    uint8_t*                         output,
    uint32_t                         output_capacity,
    uint32_t*                        output_len
);

#endif
