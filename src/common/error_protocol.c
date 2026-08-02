#include "common/error_protocol.h"

#include "common/binary.h"
#include "config.h"

#include <stddef.h>

bool error_code_is_valid(ErrorCode code) {
    switch(code) {
        case ERROR_CODE_INVALID_REQUEST:
        case ERROR_CODE_NOT_AUTHENTICATED:
        case ERROR_CODE_ACCESS_DENIED:
        case ERROR_CODE_NOT_FOUND:
        case ERROR_CODE_ALREADY_EXISTS:
        case ERROR_CODE_BUSY:
        case ERROR_CODE_INVALID_STATE:
        case ERROR_CODE_TOO_LARGE:
        case ERROR_CODE_INVALID_CHUNK:
        case ERROR_CODE_INCOMPLETE:
        case ERROR_CODE_STORAGE_FAILURE:
        case ERROR_CODE_DATABASE_FAILURE:
        case ERROR_CODE_NOT_IMPLEMENTED:
        case ERROR_CODE_INTERNAL: return true;

        default: return false;
    }
}

const char* error_code_to_string(ErrorCode code) {
    switch(code) {
        case ERROR_CODE_INVALID_REQUEST: return "invalid request";
        case ERROR_CODE_NOT_AUTHENTICATED: return "not authenticated";
        case ERROR_CODE_ACCESS_DENIED: return "access denied";
        case ERROR_CODE_NOT_FOUND: return "not found";
        case ERROR_CODE_ALREADY_EXISTS: return "already exists";
        case ERROR_CODE_BUSY: return "busy";
        case ERROR_CODE_INVALID_STATE: return "invalid state";
        case ERROR_CODE_TOO_LARGE: return "too large";
        case ERROR_CODE_INVALID_CHUNK: return "invalid chunk";
        case ERROR_CODE_INCOMPLETE: return "incomplete";
        case ERROR_CODE_STORAGE_FAILURE: return "storage failure";
        case ERROR_CODE_DATABASE_FAILURE: return "database failure";
        case ERROR_CODE_NOT_IMPLEMENTED: return "not implemented";
        case ERROR_CODE_INTERNAL: return "internal error";
        default: return "unknown error";
    }
}

bool error_response_encode(
    const ErrorResponse* response,
    uint8_t*             output,
    uint32_t             output_capacity,
    uint32_t*            output_len
) {
    if(output_len != NULL) {
        *output_len = 0U;
    }

    if(response == NULL || output == NULL || output_len == NULL || !error_code_is_valid(response->code) ||
        response->message_len > ERROR_MESSAGE_MAX_LENGTH ||
        (response->message_len != 0U && response->message == NULL)) {
        return false;
    }

    const uint32_t required_size = ERROR_RESPONSE_PREFIX_SIZE + (uint32_t)response->message_len;

    if(required_size > output_capacity) {
        return false;
    }

    BinaryWriter writer;
    binary_writer_init(&writer, output, output_capacity);

    if(!binary_write_u16(&writer, (uint16_t)response->code) || !binary_write_u16(&writer, response->message_len) ||
        !binary_write_bytes(&writer, response->message, response->message_len)) {
        return false;
    }

    *output_len = (uint32_t)binary_writer_size(&writer);

    return true;
}

bool error_response_decode(const uint8_t* payload, uint32_t payload_len, ErrorResponse* response) {
    if(response != NULL) {
        *response = (ErrorResponse){0};
    }

    if(payload == NULL || response == NULL || payload_len < ERROR_RESPONSE_PREFIX_SIZE) {
        return false;
    }

    BinaryReader reader;
    binary_reader_init(&reader, payload, payload_len);

    uint16_t raw_code    = 0U;
    uint16_t message_len = 0U;

    if(!binary_read_u16(&reader, &raw_code) || !binary_read_u16(&reader, &message_len)) {
        return false;
    }

    const ErrorCode code = (ErrorCode)raw_code;

    if(!error_code_is_valid(code) || message_len > ERROR_MESSAGE_MAX_LENGTH ||
        binary_reader_remaining(&reader) != (size_t)message_len) {
        return false;
    }

    const uint8_t* message = NULL;

    if(!binary_read_view(&reader, &message, message_len) || !binary_reader_is_finished(&reader)) {
        return false;
    }

    response->code        = code;
    response->message     = message;
    response->message_len = message_len;

    return true;
}
