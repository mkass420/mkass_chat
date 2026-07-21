#include "test.h"

#include "common/protocol.h"

#include <string.h>

static PacketHeader make_header(MessageType type, uint32_t request_id, uint32_t payload_len) {
    PacketHeader header = {
        .magic            = PROTOCOL_MAGIC,
        .type             = type,
        .flags            = 0U,
        .request_id       = request_id,
        .uncompressed_len = payload_len,
        .payload_len      = payload_len,
        .payload_crc32    = payload_len == 0U ? 0U : 0x12345678U,
    };

    return header;
}

static void test_message_type_metadata(void) {
    const MessageTypeInfo* ping = message_type_get_info(MSG_TYPE_PING_REQUEST);

    TEST_ASSERT(ping != NULL);
    TEST_ASSERT((ping->properties & MESSAGE_PROPERTY_VALID) != 0U);
    TEST_ASSERT((ping->properties & MESSAGE_PROPERTY_REQUEST) != 0U);
    TEST_ASSERT_EQ_INT(PAYLOAD_POLICY_EMPTY, ping->payload_policy);
    TEST_ASSERT_EQ_STRING("PING_REQUEST", ping->name);
    TEST_ASSERT(strlen(ping->description) > 0U);

    TEST_ASSERT(message_type_get_info(MSG_TYPE_UNKNOWN) == NULL);
    TEST_ASSERT(message_type_get_info((MessageType)254) == NULL);
    TEST_ASSERT_EQ_STRING("UNKNOWN", message_type_to_string((MessageType)254));
    TEST_ASSERT(strlen(message_type_get_description((MessageType)254)) > 0U);
}

static void test_message_type_categories(void) {
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
}

static void test_all_valid_types_have_one_category(void) {
    for(unsigned int raw_type = 0U; raw_type <= 255U; ++raw_type) {
        const MessageTypeInfo* info = message_type_get_info((MessageType)raw_type);

        if(info == NULL) {
            continue;
        }

        unsigned int category_count = 0U;
        category_count += (info->properties & MESSAGE_PROPERTY_REQUEST) != 0U ? 1U : 0U;
        category_count += (info->properties & MESSAGE_PROPERTY_RESPONSE) != 0U ? 1U : 0U;
        category_count += (info->properties & MESSAGE_PROPERTY_EVENT) != 0U ? 1U : 0U;

        TEST_ASSERT_EQ_U32(1U, category_count);
    }
}

static void test_header_wire_round_trip(void) {
    const PacketHeader source = {
        .magic            = PROTOCOL_MAGIC,
        .type             = MSG_TYPE_ECHO_REQUEST,
        .flags            = PACKET_FLAG_COMPRESSED,
        .request_id       = 0x10203040U,
        .uncompressed_len = 0x11223344U,
        .payload_len      = 0x01020304U,
        .payload_crc32    = 0xA1B2C3D4U,
    };

    PacketHeaderWire wire = {0};
    PacketHeader restored = {0};

    packet_header_to_wire(&source, &wire);
    packet_header_from_wire(&wire, &restored);

    TEST_ASSERT_EQ_U32(source.magic, restored.magic);
    TEST_ASSERT_EQ_INT(source.type, restored.type);
    TEST_ASSERT_EQ_U32(source.flags, restored.flags);
    TEST_ASSERT_EQ_U32(source.request_id, restored.request_id);
    TEST_ASSERT_EQ_U32(source.uncompressed_len, restored.uncompressed_len);
    TEST_ASSERT_EQ_U32(source.payload_len, restored.payload_len);
    TEST_ASSERT_EQ_U32(source.payload_crc32, restored.payload_crc32);
}

static void test_header_validate_valid_cases(void) {
    PacketHeader ping = make_header(MSG_TYPE_PING_REQUEST, 1U, 0U);
    TEST_ASSERT_EQ_INT(PACKET_HEADER_VALID, packet_header_validate(&ping));

    PacketHeader echo = make_header(MSG_TYPE_ECHO_REQUEST, 2U, 16U);
    TEST_ASSERT_EQ_INT(PACKET_HEADER_VALID, packet_header_validate(&echo));

    PacketHeader event = make_header(MSG_TYPE_TEXT_CREATED_EVENT, 0U, 1U);
    TEST_ASSERT_EQ_INT(PACKET_HEADER_VALID, packet_header_validate(&event));

    PacketHeader compressed = make_header(MSG_TYPE_ECHO_REQUEST, 3U, 8U);
    compressed.flags = PACKET_FLAG_COMPRESSED;
    compressed.uncompressed_len = 32U;
    TEST_ASSERT_EQ_INT(PACKET_HEADER_VALID, packet_header_validate(&compressed));
}

static void test_header_validate_basic_errors(void) {
    TEST_ASSERT_EQ_INT(PACKET_HEADER_INVALID_ARGUMENT, packet_header_validate(NULL));

    PacketHeader header = make_header(MSG_TYPE_ECHO_REQUEST, 1U, 1U);
    header.magic = 0U;
    TEST_ASSERT_EQ_INT(PACKET_HEADER_INVALID_MAGIC, packet_header_validate(&header));

    header = make_header((MessageType)254, 1U, 1U);
    TEST_ASSERT_EQ_INT(PACKET_HEADER_INVALID_TYPE, packet_header_validate(&header));

    header = make_header(MSG_TYPE_ECHO_REQUEST, 1U, 1U);
    header.flags = 0x80U;
    TEST_ASSERT_EQ_INT(PACKET_HEADER_INVALID_FLAGS, packet_header_validate(&header));
}

static void test_header_validate_request_ids(void) {
    PacketHeader request = make_header(MSG_TYPE_ECHO_REQUEST, 0U, 1U);
    TEST_ASSERT_EQ_INT(PACKET_HEADER_INVALID_REQUEST_ID, packet_header_validate(&request));

    PacketHeader response = make_header(MSG_TYPE_ECHO_RESPONSE, 0U, 1U);
    TEST_ASSERT_EQ_INT(PACKET_HEADER_INVALID_REQUEST_ID, packet_header_validate(&response));

    PacketHeader event = make_header(MSG_TYPE_TEXT_CREATED_EVENT, 7U, 1U);
    TEST_ASSERT_EQ_INT(PACKET_HEADER_INVALID_REQUEST_ID, packet_header_validate(&event));
}

static void test_header_validate_limits(void) {
    PacketHeader header = make_header(MSG_TYPE_ECHO_REQUEST, 1U, PROTOCOL_MAX_PAYLOAD_LENGTH);
    TEST_ASSERT_EQ_INT(PACKET_HEADER_VALID, packet_header_validate(&header));

    header.payload_len = PROTOCOL_MAX_PAYLOAD_LENGTH + 1U;
    header.uncompressed_len = header.payload_len;
    TEST_ASSERT_EQ_INT(PACKET_HEADER_INVALID_PAYLOAD_LENGTH, packet_header_validate(&header));

    header = make_header(MSG_TYPE_ECHO_REQUEST, 1U, 1U);
    header.flags = PACKET_FLAG_COMPRESSED;
    header.uncompressed_len = PROTOCOL_MAX_UNCOMPRESSED_LENGTH + 1U;
    TEST_ASSERT_EQ_INT(PACKET_HEADER_INVALID_UNCOMPRESSED_LENGTH, packet_header_validate(&header));
}

static void test_header_validate_payload_policy(void) {
    PacketHeader ping = make_header(MSG_TYPE_PING_REQUEST, 1U, 1U);
    TEST_ASSERT_EQ_INT(PACKET_HEADER_INVALID_PAYLOAD_POLICY, packet_header_validate(&ping));

    PacketHeader registration = make_header(MSG_TYPE_REGISTER_REQUEST, 1U, 0U);
    TEST_ASSERT_EQ_INT(PACKET_HEADER_INVALID_PAYLOAD_POLICY, packet_header_validate(&registration));
}

static void test_header_validate_length_consistency(void) {
    PacketHeader header = make_header(MSG_TYPE_ECHO_REQUEST, 1U, 8U);
    header.uncompressed_len = 9U;
    TEST_ASSERT_EQ_INT(PACKET_HEADER_INVALID_UNCOMPRESSED_LENGTH, packet_header_validate(&header));

    header = make_header(MSG_TYPE_PING_REQUEST, 1U, 0U);
    header.uncompressed_len = 1U;
    TEST_ASSERT_EQ_INT(PACKET_HEADER_INVALID_UNCOMPRESSED_LENGTH, packet_header_validate(&header));

    header = make_header(MSG_TYPE_PING_REQUEST, 1U, 0U);
    header.payload_crc32 = 1U;
    TEST_ASSERT_EQ_INT(PACKET_HEADER_INVALID_CRC, packet_header_validate(&header));

    header = make_header(MSG_TYPE_PING_REQUEST, 1U, 0U);
    header.flags = PACKET_FLAG_COMPRESSED;
    TEST_ASSERT_EQ_INT(PACKET_HEADER_INVALID_FLAGS, packet_header_validate(&header));
}

static void test_validation_result_strings(void) {
    TEST_ASSERT_EQ_STRING("valid header", packet_header_validation_result_to_string(PACKET_HEADER_VALID));
    TEST_ASSERT_EQ_STRING(
        "invalid payload CRC", packet_header_validation_result_to_string(PACKET_HEADER_INVALID_CRC)
    );
    TEST_ASSERT_EQ_STRING("unknown header validation result", packet_header_validation_result_to_string((PacketHeaderValidationResult)99));
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
    TEST_ADD(suite, test_header_validate_length_consistency);
    TEST_ADD(suite, test_validation_result_strings);
}
