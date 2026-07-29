#include "test.h"

#include "common/protocol.h"

#include <string.h>

static PacketHeader make_header(MessageType type, uint32_t request_id, uint32_t payload_len) {
    return (PacketHeader){
        .magic            = PROTOCOL_MAGIC,
        .type             = type,
        .flags            = 0U,
        .request_id       = request_id,
        .uncompressed_len = payload_len,
        .payload_len      = payload_len,
        .payload_crc32    = payload_len == 0U ? 0U : 0x12345678U,
    };
}

static bool test_message_type_metadata(void) {
    const MessageTypeInfo* ping = message_type_get_info(MSG_TYPE_PING_REQUEST);

    TEST_ASSERT(ping != NULL);
    TEST_ASSERT((ping->properties & MESSAGE_PROPERTY_VALID) != 0U);
    TEST_ASSERT((ping->properties & MESSAGE_PROPERTY_REQUEST) != 0U);
    TEST_ASSERT(ping->payload_policy == PAYLOAD_POLICY_EMPTY);
    TEST_ASSERT(ping->compression_policy == COMPRESSION_POLICY_NEVER);
    TEST_ASSERT(strcmp(ping->name, "PING_REQUEST") == 0);
    TEST_ASSERT(strlen(ping->description) > 0U);

    const MessageTypeInfo* upload_chunk = message_type_get_info(MSG_TYPE_FILE_UPLOAD_CHUNK_REQUEST);

    TEST_ASSERT(upload_chunk != NULL);
    TEST_ASSERT(upload_chunk->payload_policy == PAYLOAD_POLICY_REQUIRED);
    TEST_ASSERT(upload_chunk->compression_policy == COMPRESSION_POLICY_TRY);

    const MessageTypeInfo* download_chunk = message_type_get_info(MSG_TYPE_FILE_DOWNLOAD_CHUNK_RESPONSE);

    TEST_ASSERT(download_chunk != NULL);
    TEST_ASSERT(download_chunk->payload_policy == PAYLOAD_POLICY_REQUIRED);
    TEST_ASSERT(download_chunk->compression_policy == COMPRESSION_POLICY_TRY);

    TEST_ASSERT(message_type_get_info(MSG_TYPE_UNKNOWN) == NULL);
    TEST_ASSERT(message_type_get_info((MessageType)254) == NULL);
    TEST_ASSERT(strcmp(message_type_to_string((MessageType)254), "UNKNOWN") == 0);
    TEST_ASSERT(strlen(message_type_get_description((MessageType)254)) > 0U);

    return true;
}

static bool test_message_type_categories(void) {
    TEST_ASSERT(message_type_is_valid(MSG_TYPE_PING_REQUEST));
    TEST_ASSERT(message_type_is_request(MSG_TYPE_PING_REQUEST));
    TEST_ASSERT(!message_type_is_response(MSG_TYPE_PING_REQUEST));
    TEST_ASSERT(!message_type_is_event(MSG_TYPE_PING_REQUEST));

    TEST_ASSERT(message_type_is_response(MSG_TYPE_PING_RESPONSE));
    TEST_ASSERT(message_type_is_event(MSG_TYPE_TEXT_CREATED_EVENT));

    TEST_ASSERT(message_type_is_allowed_from_client(MSG_TYPE_ECHO_REQUEST));
    TEST_ASSERT(!message_type_is_allowed_from_client(MSG_TYPE_ECHO_RESPONSE));
    TEST_ASSERT(message_type_is_allowed_from_server(MSG_TYPE_ECHO_RESPONSE));
    TEST_ASSERT(message_type_is_allowed_from_server(MSG_TYPE_TEXT_CREATED_EVENT));
    TEST_ASSERT(!message_type_is_allowed_from_server(MSG_TYPE_ECHO_REQUEST));

    return true;
}

static bool test_all_valid_types_have_one_category(void) {
    for(unsigned int raw_type = 0U; raw_type <= 255U; ++raw_type) {
        const MessageTypeInfo* info = message_type_get_info((MessageType)raw_type);

        if(info == NULL) {
            continue;
        }

        unsigned int category_count = 0U;
        category_count += (info->properties & MESSAGE_PROPERTY_REQUEST) != 0U ? 1U : 0U;
        category_count += (info->properties & MESSAGE_PROPERTY_RESPONSE) != 0U ? 1U : 0U;
        category_count += (info->properties & MESSAGE_PROPERTY_EVENT) != 0U ? 1U : 0U;

        TEST_ASSERT(category_count == 1U);
    }

    return true;
}

static bool test_header_wire_round_trip(void) {
    const PacketHeader source = {
        .magic            = PROTOCOL_MAGIC,
        .type             = MSG_TYPE_ECHO_REQUEST,
        .flags            = PACKET_FLAG_COMPRESSED,
        .request_id       = 0x10203040U,
        .uncompressed_len = 0x11223344U,
        .payload_len      = 0x01020304U,
        .payload_crc32    = 0xA1B2C3D4U,
    };

    PacketHeaderWire wire     = {0};
    PacketHeader     restored = {0};

    packet_header_to_wire(&source, &wire);
    packet_header_from_wire(&wire, &restored);

    TEST_ASSERT(restored.magic == source.magic);
    TEST_ASSERT(restored.type == source.type);
    TEST_ASSERT(restored.flags == source.flags);
    TEST_ASSERT(restored.request_id == source.request_id);
    TEST_ASSERT(restored.uncompressed_len == source.uncompressed_len);
    TEST_ASSERT(restored.payload_len == source.payload_len);
    TEST_ASSERT(restored.payload_crc32 == source.payload_crc32);

    return true;
}

static bool test_header_validate_valid_cases(void) {
    PacketHeader ping = make_header(MSG_TYPE_PING_REQUEST, 1U, 0U);
    TEST_ASSERT(packet_header_validate(&ping) == PACKET_HEADER_VALID);

    PacketHeader echo = make_header(MSG_TYPE_ECHO_REQUEST, 2U, 16U);
    TEST_ASSERT(packet_header_validate(&echo) == PACKET_HEADER_VALID);

    PacketHeader event = make_header(MSG_TYPE_TEXT_CREATED_EVENT, 0U, 1U);
    TEST_ASSERT(packet_header_validate(&event) == PACKET_HEADER_VALID);

    PacketHeader compressed     = make_header(MSG_TYPE_ECHO_REQUEST, 3U, 8U);
    compressed.flags            = PACKET_FLAG_COMPRESSED;
    compressed.uncompressed_len = 32U;
    TEST_ASSERT(packet_header_validate(&compressed) == PACKET_HEADER_VALID);

    PacketHeader upload_chunk = make_header(MSG_TYPE_FILE_UPLOAD_CHUNK_REQUEST, 4U, FILE_CHUNK_DATA_SIZE);
    TEST_ASSERT(packet_header_validate(&upload_chunk) == PACKET_HEADER_VALID);

    PacketHeader upload_begin = make_header(MSG_TYPE_FILE_UPLOAD_BEGIN_REQUEST, 5U, 12U);
    TEST_ASSERT(packet_header_validate(&upload_begin) == PACKET_HEADER_VALID);

    return true;
}

static bool test_header_validate_basic_errors(void) {
    TEST_ASSERT(packet_header_validate(NULL) == PACKET_HEADER_INVALID_ARGUMENT);

    PacketHeader header = make_header(MSG_TYPE_ECHO_REQUEST, 1U, 1U);
    header.magic        = 0U;
    TEST_ASSERT(packet_header_validate(&header) == PACKET_HEADER_INVALID_MAGIC);

    header = make_header((MessageType)254, 1U, 1U);
    TEST_ASSERT(packet_header_validate(&header) == PACKET_HEADER_INVALID_TYPE);

    header       = make_header(MSG_TYPE_ECHO_REQUEST, 1U, 1U);
    header.flags = 0x80U;
    TEST_ASSERT(packet_header_validate(&header) == PACKET_HEADER_INVALID_FLAGS);

    return true;
}

static bool test_header_validate_request_ids(void) {
    PacketHeader request = make_header(MSG_TYPE_ECHO_REQUEST, 0U, 1U);
    TEST_ASSERT(packet_header_validate(&request) == PACKET_HEADER_INVALID_REQUEST_ID);

    PacketHeader response = make_header(MSG_TYPE_ECHO_RESPONSE, 0U, 1U);
    TEST_ASSERT(packet_header_validate(&response) == PACKET_HEADER_INVALID_REQUEST_ID);

    PacketHeader event = make_header(MSG_TYPE_TEXT_CREATED_EVENT, 7U, 1U);
    TEST_ASSERT(packet_header_validate(&event) == PACKET_HEADER_INVALID_REQUEST_ID);

    return true;
}

static bool test_header_validate_limits(void) {
    PacketHeader header = make_header(MSG_TYPE_ECHO_REQUEST, 1U, PROTOCOL_MAX_PAYLOAD_LENGTH);
    TEST_ASSERT(packet_header_validate(&header) == PACKET_HEADER_VALID);

    header.payload_len      = PROTOCOL_MAX_PAYLOAD_LENGTH + 1U;
    header.uncompressed_len = header.payload_len;
    TEST_ASSERT(packet_header_validate(&header) == PACKET_HEADER_INVALID_PAYLOAD_LENGTH);

    header                  = make_header(MSG_TYPE_ECHO_REQUEST, 1U, 1U);
    header.flags            = PACKET_FLAG_COMPRESSED;
    header.uncompressed_len = PROTOCOL_MAX_UNCOMPRESSED_LENGTH + 1U;
    TEST_ASSERT(packet_header_validate(&header) == PACKET_HEADER_INVALID_UNCOMPRESSED_LENGTH);

    return true;
}

static bool test_header_validate_payload_policy(void) {
    PacketHeader ping = make_header(MSG_TYPE_PING_REQUEST, 1U, 1U);
    TEST_ASSERT(packet_header_validate(&ping) == PACKET_HEADER_INVALID_PAYLOAD_POLICY);

    PacketHeader registration = make_header(MSG_TYPE_REGISTER_REQUEST, 1U, 0U);
    TEST_ASSERT(packet_header_validate(&registration) == PACKET_HEADER_INVALID_PAYLOAD_POLICY);

    PacketHeader upload_chunk = make_header(MSG_TYPE_FILE_UPLOAD_CHUNK_REQUEST, 1U, 0U);
    TEST_ASSERT(packet_header_validate(&upload_chunk) == PACKET_HEADER_INVALID_PAYLOAD_POLICY);

    PacketHeader upload_finish = make_header(MSG_TYPE_FILE_UPLOAD_FINISH_REQUEST, 1U, 1U);
    TEST_ASSERT(packet_header_validate(&upload_finish) == PACKET_HEADER_INVALID_PAYLOAD_POLICY);

    return true;
}

static bool test_header_validate_compression_policy(void) {
    PacketHeader ping = make_header(MSG_TYPE_PING_REQUEST, 1U, 0U);
    ping.flags        = PACKET_FLAG_COMPRESSED;
    TEST_ASSERT(packet_header_validate(&ping) == PACKET_HEADER_INVALID_COMPRESSION_POLICY);

    PacketHeader upload_begin = make_header(MSG_TYPE_FILE_UPLOAD_BEGIN_REQUEST, 2U, 16U);
    upload_begin.flags        = PACKET_FLAG_COMPRESSED;
    upload_begin.payload_len  = 8U;
    TEST_ASSERT(packet_header_validate(&upload_begin) == PACKET_HEADER_INVALID_COMPRESSION_POLICY);

    PacketHeader echo     = make_header(MSG_TYPE_ECHO_REQUEST, 3U, 8U);
    echo.flags            = PACKET_FLAG_COMPRESSED;
    echo.uncompressed_len = 32U;
    TEST_ASSERT(packet_header_validate(&echo) == PACKET_HEADER_VALID);

    return true;
}

static bool test_header_validate_length_consistency(void) {
    PacketHeader header     = make_header(MSG_TYPE_ECHO_REQUEST, 1U, 8U);
    header.uncompressed_len = 9U;
    TEST_ASSERT(packet_header_validate(&header) == PACKET_HEADER_INVALID_UNCOMPRESSED_LENGTH);

    header               = make_header(MSG_TYPE_PING_REQUEST, 1U, 0U);
    header.payload_crc32 = 1U;
    TEST_ASSERT(packet_header_validate(&header) == PACKET_HEADER_INVALID_CRC);

    header       = make_header(MSG_TYPE_ECHO_REQUEST, 1U, 0U);
    header.flags = PACKET_FLAG_COMPRESSED;
    TEST_ASSERT(packet_header_validate(&header) == PACKET_HEADER_INVALID_FLAGS);

    return true;
}

static bool test_validation_result_strings(void) {
    TEST_ASSERT(strcmp(packet_header_validation_result_to_string(PACKET_HEADER_VALID), "valid header") == 0);
    TEST_ASSERT(
        strcmp(packet_header_validation_result_to_string(PACKET_HEADER_INVALID_CRC), "invalid payload CRC") == 0
    );
    TEST_ASSERT(
        strcmp(
            packet_header_validation_result_to_string(PACKET_HEADER_INVALID_COMPRESSION_POLICY),
            "compression does not match message policy"
        ) == 0
    );
    TEST_ASSERT(
        strcmp(
            packet_header_validation_result_to_string((PacketHeaderValidationResult)99),
            "unknown header validation result"
        ) == 0
    );

    return true;
}

void register_protocol_tests(TestSuite* suite) {
    TEST_ADD(suite, test_message_type_metadata);
    TEST_ADD(suite, test_message_type_categories);
    TEST_ADD(suite, test_all_valid_types_have_one_category);
    TEST_ADD(suite, test_header_wire_round_trip);
    TEST_ADD(suite, test_header_validate_valid_cases);
    TEST_ADD(suite, test_header_validate_basic_errors);
    TEST_ADD(suite, test_header_validate_request_ids);
    TEST_ADD(suite, test_header_validate_limits);
    TEST_ADD(suite, test_header_validate_payload_policy);
    TEST_ADD(suite, test_header_validate_compression_policy);
    TEST_ADD(suite, test_header_validate_length_consistency);
    TEST_ADD(suite, test_validation_result_strings);
}
