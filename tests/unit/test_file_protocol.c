#include "test.h"

#include "common/binary.h"
#include "common/file_protocol.h"
#include "config.h"

#include <string.h>

static uint32_t build_upload_begin_payload(
    uint8_t*       output,
    size_t         capacity,
    uint64_t       file_size,
    const uint8_t* file_name,
    uint16_t       file_name_len
) {
    BinaryWriter writer;
    binary_writer_init(&writer, output, capacity);

    if(!binary_write_u64(&writer, file_size) || !binary_write_u16(&writer, file_name_len) ||
        !binary_write_bytes(&writer, file_name, file_name_len)) {
        return 0U;
    }

    return (uint32_t)binary_writer_size(&writer);
}

static bool test_file_upload_begin_decode(void) {
    static const uint8_t name[]      = "report.bin";
    uint8_t              payload[64] = {0};
    const uint32_t       payload_len =
        build_upload_begin_payload(payload, sizeof(payload), UINT64_C(123456789), name, (uint16_t)(sizeof(name) - 1U));

    TEST_ASSERT(payload_len != 0U);

    FileUploadBeginRequest request;
    TEST_ASSERT(file_upload_begin_request_decode(payload, payload_len, &request));
    TEST_ASSERT(request.file_size == UINT64_C(123456789));
    TEST_ASSERT(request.file_name_len == sizeof(name) - 1U);
    TEST_ASSERT(request.file_name == payload + FILE_UPLOAD_BEGIN_REQUEST_PREFIX_SIZE);
    TEST_ASSERT(memcmp(request.file_name, name, sizeof(name) - 1U) == 0);

    return true;
}

static bool test_file_upload_begin_decode_empty_file(void) {
    static const uint8_t name[]      = "empty.txt";
    uint8_t              payload[64] = {0};
    const uint32_t       payload_len =
        build_upload_begin_payload(payload, sizeof(payload), 0U, name, (uint16_t)(sizeof(name) - 1U));

    FileUploadBeginRequest request;
    TEST_ASSERT(file_upload_begin_request_decode(payload, payload_len, &request));
    TEST_ASSERT(request.file_size == 0U);

    return true;
}

static bool test_file_upload_begin_rejects_invalid_payloads(void) {
    FileUploadBeginRequest request;
    uint8_t                payload[FILE_UPLOAD_BEGIN_REQUEST_PREFIX_SIZE + FILE_MAX_NAME_LENGTH + 2U] = {0};

    TEST_ASSERT(!file_upload_begin_request_decode(NULL, 0U, &request));
    TEST_ASSERT(!file_upload_begin_request_decode(payload, sizeof(payload), NULL));
    TEST_ASSERT(!file_upload_begin_request_decode(payload, FILE_UPLOAD_BEGIN_REQUEST_PREFIX_SIZE - 1U, &request));

    static const uint8_t name[] = "name.bin";
    uint32_t             payload_len =
        build_upload_begin_payload(payload, sizeof(payload), FILE_MAX_SIZE + 1U, name, (uint16_t)(sizeof(name) - 1U));
    TEST_ASSERT(!file_upload_begin_request_decode(payload, payload_len, &request));

    payload_len = build_upload_begin_payload(payload, sizeof(payload), 1U, NULL, 0U);
    TEST_ASSERT(payload_len == FILE_UPLOAD_BEGIN_REQUEST_PREFIX_SIZE);
    TEST_ASSERT(!file_upload_begin_request_decode(payload, payload_len, &request));

    static const uint8_t name_with_nul[] = {'a', '\0', 'b'};
    payload_len =
        build_upload_begin_payload(payload, sizeof(payload), 1U, name_with_nul, (uint16_t)sizeof(name_with_nul));
    TEST_ASSERT(!file_upload_begin_request_decode(payload, payload_len, &request));

    memset(payload, 'x', sizeof(payload));
    BinaryWriter writer;
    binary_writer_init(&writer, payload, sizeof(payload));
    TEST_ASSERT(binary_write_u64(&writer, 1U));
    TEST_ASSERT(binary_write_u16(&writer, (uint16_t)(FILE_MAX_NAME_LENGTH + 1U)));
    TEST_ASSERT(
        binary_write_bytes(&writer, payload + FILE_UPLOAD_BEGIN_REQUEST_PREFIX_SIZE, FILE_MAX_NAME_LENGTH + 1U)
    );
    TEST_ASSERT(!file_upload_begin_request_decode(payload, (uint32_t)binary_writer_size(&writer), &request));

    payload_len = build_upload_begin_payload(payload, sizeof(payload), 1U, name, (uint16_t)(sizeof(name) - 1U));
    payload[payload_len] = 0xAAU;
    TEST_ASSERT(!file_upload_begin_request_decode(payload, payload_len + 1U, &request));

    return true;
}

static bool test_file_upload_finish_response_encode(void) {
    FileUploadFinishResponse response = {0};

    for(size_t i = 0U; i < FILE_ID_SIZE; ++i) {
        response.file_id.bytes[i] = (uint8_t)(i + 1U);
    }

    uint8_t  output[FILE_ID_SIZE] = {0};
    uint32_t output_len           = 99U;

    TEST_ASSERT(file_upload_finish_response_encode(&response, output, sizeof(output), &output_len));
    TEST_ASSERT(output_len == FILE_ID_SIZE);
    TEST_ASSERT(memcmp(output, response.file_id.bytes, FILE_ID_SIZE) == 0);

    output_len = 99U;
    TEST_ASSERT(!file_upload_finish_response_encode(&response, output, FILE_ID_SIZE - 1U, &output_len));
    TEST_ASSERT(output_len == 0U);

    output_len = 99U;
    TEST_ASSERT(!file_upload_finish_response_encode(NULL, output, sizeof(output), &output_len));
    TEST_ASSERT(output_len == 0U);

    return true;
}

static bool test_file_download_begin_request_decode(void) {
    uint8_t payload[FILE_ID_SIZE];

    for(size_t i = 0U; i < sizeof(payload); ++i) {
        payload[i] = (uint8_t)(0xF0U + i);
    }

    FileDownloadBeginRequest request;
    TEST_ASSERT(file_download_begin_request_decode(payload, sizeof(payload), &request));
    TEST_ASSERT(memcmp(request.file_id.bytes, payload, sizeof(payload)) == 0);

    TEST_ASSERT(!file_download_begin_request_decode(payload, sizeof(payload) - 1U, &request));
    TEST_ASSERT(!file_download_begin_request_decode(payload, sizeof(payload) + 1U, &request));
    TEST_ASSERT(!file_download_begin_request_decode(NULL, sizeof(payload), &request));
    TEST_ASSERT(!file_download_begin_request_decode(payload, sizeof(payload), NULL));

    return true;
}

static bool test_file_download_begin_response_encode(void) {
    static const uint8_t            name[]   = "archive.tar";
    const FileDownloadBeginResponse response = {
        .file_size     = UINT64_C(123456789),
        .file_name     = name,
        .file_name_len = (uint16_t)(sizeof(name) - 1U),
    };

    uint8_t  output[64] = {0};
    uint32_t output_len = 0U;

    TEST_ASSERT(file_download_begin_response_encode(&response, output, sizeof(output), &output_len));
    TEST_ASSERT(output_len == FILE_DOWNLOAD_BEGIN_RESPONSE_PREFIX_SIZE + sizeof(name) - 1U);

    BinaryReader reader;
    binary_reader_init(&reader, output, output_len);

    uint64_t       file_size     = 0U;
    uint16_t       file_name_len = 0U;
    const uint8_t* file_name     = NULL;

    TEST_ASSERT(binary_read_u64(&reader, &file_size));
    TEST_ASSERT(binary_read_u16(&reader, &file_name_len));
    TEST_ASSERT(binary_read_view(&reader, &file_name, file_name_len));
    TEST_ASSERT(binary_reader_is_finished(&reader));
    TEST_ASSERT(file_size == response.file_size);
    TEST_ASSERT(file_name_len == response.file_name_len);
    TEST_ASSERT(memcmp(file_name, name, sizeof(name) - 1U) == 0);

    return true;
}

static bool test_file_download_begin_response_rejects_invalid_values(void) {
    static const uint8_t valid_name[]   = "file.bin";
    static const uint8_t invalid_name[] = {'a', '\0', 'b'};
    uint8_t              output[64]     = {0};
    uint32_t             output_len     = 99U;

    FileDownloadBeginResponse response = {
        .file_size     = FILE_MAX_SIZE + 1U,
        .file_name     = valid_name,
        .file_name_len = (uint16_t)(sizeof(valid_name) - 1U),
    };

    TEST_ASSERT(!file_download_begin_response_encode(&response, output, sizeof(output), &output_len));
    TEST_ASSERT(output_len == 0U);

    response.file_size     = 1U;
    response.file_name     = invalid_name;
    response.file_name_len = (uint16_t)sizeof(invalid_name);
    TEST_ASSERT(!file_download_begin_response_encode(&response, output, sizeof(output), &output_len));

    response.file_name     = valid_name;
    response.file_name_len = (uint16_t)(sizeof(valid_name) - 1U);
    TEST_ASSERT(!file_download_begin_response_encode(
        &response, output, FILE_DOWNLOAD_BEGIN_RESPONSE_PREFIX_SIZE + response.file_name_len - 1U, &output_len
    ));

    return true;
}

void register_file_protocol_tests(TestSuite* suite) {
    TEST_ADD(suite, test_file_upload_begin_decode);
    TEST_ADD(suite, test_file_upload_begin_decode_empty_file);
    TEST_ADD(suite, test_file_upload_begin_rejects_invalid_payloads);
    TEST_ADD(suite, test_file_upload_finish_response_encode);
    TEST_ADD(suite, test_file_download_begin_request_decode);
    TEST_ADD(suite, test_file_download_begin_response_encode);
    TEST_ADD(suite, test_file_download_begin_response_rejects_invalid_values);
}
