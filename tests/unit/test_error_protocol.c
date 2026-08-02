#include "test.h"

#include "common/error_protocol.h"
#include "config.h"

#include <string.h>

static bool test_error_response_round_trip(void) {
    static const uint8_t message[] = "File not found";

    const ErrorResponse source = {
        .code        = ERROR_CODE_NOT_FOUND,
        .message     = message,
        .message_len = (uint16_t)(sizeof(message) - 1U),
    };

    uint8_t  payload[ERROR_RESPONSE_MAX_SIZE];
    uint32_t payload_len = 0U;

    TEST_ASSERT(error_response_encode(&source, payload, sizeof(payload), &payload_len));
    TEST_ASSERT(payload_len == ERROR_RESPONSE_PREFIX_SIZE + sizeof(message) - 1U);

    ErrorResponse decoded;
    TEST_ASSERT(error_response_decode(payload, payload_len, &decoded));
    TEST_ASSERT(decoded.code == source.code);
    TEST_ASSERT(decoded.message_len == source.message_len);
    TEST_ASSERT(memcmp(decoded.message, source.message, source.message_len) == 0);

    return true;
}

static bool test_error_response_empty_message(void) {
    const ErrorResponse source = {
        .code        = ERROR_CODE_INTERNAL,
        .message     = NULL,
        .message_len = 0U,
    };

    uint8_t  payload[ERROR_RESPONSE_PREFIX_SIZE];
    uint32_t payload_len = 0U;

    TEST_ASSERT(error_response_encode(&source, payload, sizeof(payload), &payload_len));
    TEST_ASSERT(payload_len == ERROR_RESPONSE_PREFIX_SIZE);

    ErrorResponse decoded;
    TEST_ASSERT(error_response_decode(payload, payload_len, &decoded));
    TEST_ASSERT(decoded.code == ERROR_CODE_INTERNAL);
    TEST_ASSERT(decoded.message == NULL);
    TEST_ASSERT(decoded.message_len == 0U);

    return true;
}

static bool test_error_response_rejects_invalid_code(void) {
    const uint8_t payload[] = {
        0x00U,
        0x00U,
        0x00U,
        0x00U,
    };

    ErrorResponse decoded;
    TEST_ASSERT(!error_response_decode(payload, sizeof(payload), &decoded));

    return true;
}

static bool test_error_response_rejects_wrong_message_length(void) {
    const uint8_t payload[] = {
        0x00U,
        (uint8_t)ERROR_CODE_NOT_FOUND,
        0x00U,
        0x03U,
        'x',
        'y',
    };

    ErrorResponse decoded;
    TEST_ASSERT(!error_response_decode(payload, sizeof(payload), &decoded));

    return true;
}

static bool test_error_response_rejects_trailing_bytes(void) {
    const uint8_t payload[] = {
        0x00U,
        (uint8_t)ERROR_CODE_BUSY,
        0x00U,
        0x01U,
        'x',
        'y',
    };

    ErrorResponse decoded;
    TEST_ASSERT(!error_response_decode(payload, sizeof(payload), &decoded));

    return true;
}

static bool test_error_response_rejects_small_output(void) {
    static const uint8_t message[] = "busy";

    const ErrorResponse source = {
        .code        = ERROR_CODE_BUSY,
        .message     = message,
        .message_len = (uint16_t)(sizeof(message) - 1U),
    };

    uint8_t  payload[ERROR_RESPONSE_PREFIX_SIZE];
    uint32_t payload_len = 123U;

    TEST_ASSERT(!error_response_encode(&source, payload, sizeof(payload), &payload_len));
    TEST_ASSERT(payload_len == 0U);

    return true;
}

void register_error_protocol_tests(TestSuite* suite) {
    TEST_ADD(suite, test_error_response_round_trip);
    TEST_ADD(suite, test_error_response_empty_message);
    TEST_ADD(suite, test_error_response_rejects_invalid_code);
    TEST_ADD(suite, test_error_response_rejects_wrong_message_length);
    TEST_ADD(suite, test_error_response_rejects_trailing_bytes);
    TEST_ADD(suite, test_error_response_rejects_small_output);
}
