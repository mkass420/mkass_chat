#ifndef ERROR_PROTOCOL_H
#define ERROR_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ERROR_CODE_INVALID_REQUEST = 1,
    ERROR_CODE_NOT_AUTHENTICATED,
    ERROR_CODE_ACCESS_DENIED,
    ERROR_CODE_NOT_FOUND,
    ERROR_CODE_ALREADY_EXISTS,
    ERROR_CODE_BUSY,
    ERROR_CODE_INVALID_STATE,
    ERROR_CODE_TOO_LARGE,
    ERROR_CODE_INVALID_CHUNK,
    ERROR_CODE_INCOMPLETE,
    ERROR_CODE_STORAGE_FAILURE,
    ERROR_CODE_DATABASE_FAILURE,
    ERROR_CODE_NOT_IMPLEMENTED,
    ERROR_CODE_INTERNAL
} ErrorCode;

typedef struct {
    ErrorCode      code;
    const uint8_t* message;
    uint16_t       message_len;
} ErrorResponse;

bool        error_code_is_valid(ErrorCode code);
const char* error_code_to_string(ErrorCode code);

bool error_response_encode(
    const ErrorResponse* response,
    uint8_t*             output,
    uint32_t             output_capacity,
    uint32_t*            output_len
);

bool error_response_decode(const uint8_t* payload, uint32_t payload_len, ErrorResponse* response);

#endif
