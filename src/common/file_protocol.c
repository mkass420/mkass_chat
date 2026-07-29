#include "common/file_protocol.h"

#include "common/binary.h"
#include "config.h"

#include <string.h>
static bool file_name_is_valid(const uint8_t* file_name, uint16_t file_name_len) {
    if(file_name == NULL || file_name_len == 0U || file_name_len > FILE_MAX_NAME_LENGTH) {
        return false;
    }

    // Имя передаётся без завершающего нулевого байта
    return memchr(file_name, '\0', file_name_len) == NULL;
}

bool file_upload_begin_request_decode(const uint8_t* payload, uint32_t payload_len, FileUploadBeginRequest* request) {
    if(payload == NULL || request == NULL) {
        return false;
    }

    *request = (FileUploadBeginRequest){0};

    if(payload_len < FILE_UPLOAD_BEGIN_REQUEST_PREFIX_SIZE) {
        return false;
    }

    BinaryReader reader;

    binary_reader_init(&reader, payload, payload_len);

    uint16_t file_name_len = 0U;

    if(!binary_read_u64(&reader, &request->file_size) || !binary_read_u16(&reader, &file_name_len)) {
        return false;
    }

    if(request->file_size > FILE_MAX_SIZE) {
        return false;
    }

    if(file_name_len == 0U || file_name_len > FILE_MAX_NAME_LENGTH) {
        return false;
    }

    if(binary_reader_remaining(&reader) != (size_t)file_name_len) {
        return false;
    }

    if(!binary_read_view(&reader, &request->file_name, file_name_len)) {
        return false;
    }

    if(!binary_reader_is_finished(&reader)) {
        return false;
    }

    if(!file_name_is_valid(request->file_name, file_name_len)) {
        return false;
    }

    request->file_name_len = file_name_len;

    return true;
}

bool file_upload_finish_response_encode(
    const FileUploadFinishResponse* response,
    uint8_t*                        output,
    uint32_t                        output_capacity,
    uint32_t*                       output_len
) {
    if(output_len != NULL) {
        *output_len = 0U;
    }

    if(response == NULL || output == NULL || output_len == NULL) {
        return false;
    }

    if(output_capacity < FILE_UPLOAD_FINISH_RESPONSE_SIZE) {
        return false;
    }

    BinaryWriter writer;

    binary_writer_init(&writer, output, output_capacity);

    if(!binary_write_bytes(&writer, response->file_id.bytes, FILE_ID_SIZE)) {
        return false;
    }

    *output_len = (uint32_t)binary_writer_size(&writer);

    return true;
}

bool file_download_begin_request_decode(
    const uint8_t*            payload,
    uint32_t                  payload_len,
    FileDownloadBeginRequest* request
) {
    if(payload == NULL || request == NULL) {
        return false;
    }

    *request = (FileDownloadBeginRequest){0};

    if(payload_len != FILE_DOWNLOAD_BEGIN_REQUEST_SIZE) {
        return false;
    }

    BinaryReader reader;

    binary_reader_init(&reader, payload, payload_len);

    if(!binary_read_bytes(&reader, request->file_id.bytes, FILE_ID_SIZE)) {
        return false;
    }

    return binary_reader_is_finished(&reader);
}

bool file_download_begin_response_encode(
    const FileDownloadBeginResponse* response,
    uint8_t*                         output,
    uint32_t                         output_capacity,
    uint32_t*                        output_len
) {
    if(output_len != NULL) {
        *output_len = 0U;
    }

    if(response == NULL || output == NULL || output_len == NULL) {
        return false;
    }

    if(response->file_size > FILE_MAX_SIZE) {
        return false;
    }

    if(!file_name_is_valid(response->file_name, response->file_name_len)) {
        return false;
    }

    const size_t required_size = FILE_DOWNLOAD_BEGIN_RESPONSE_PREFIX_SIZE + (size_t)response->file_name_len;

    if(required_size > (size_t)output_capacity) {
        return false;
    }

    BinaryWriter writer;

    binary_writer_init(&writer, output, output_capacity);

    if(!binary_write_u64(&writer, response->file_size) || !binary_write_u16(&writer, response->file_name_len) ||
        !binary_write_bytes(&writer, response->file_name, response->file_name_len)) {
        return false;
    }

    *output_len = (uint32_t)binary_writer_size(&writer);

    return true;
}
