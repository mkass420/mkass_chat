#include "test.h"

#include "common/frame.h"

#include <stdlib.h>
#include <string.h>

#include <zlib.h>

static uint32_t test_crc32(const uint8_t* payload, uint32_t payload_len) {
    uLong crc = crc32(0L, Z_NULL, 0);

    if(payload_len != 0U) {
        crc = crc32(crc, payload, (uInt)payload_len);
    }

    return (uint32_t)crc;
}

static size_t build_manual_frame(
    uint8_t* output,
    MessageType type,
    uint8_t flags,
    uint32_t request_id,
    uint32_t uncompressed_len,
    const uint8_t* payload,
    uint32_t payload_len,
    uint32_t payload_crc32
) {
    PacketHeader header = {
        .magic            = PROTOCOL_MAGIC,
        .type             = type,
        .flags            = flags,
        .request_id       = request_id,
        .uncompressed_len = uncompressed_len,
        .payload_len      = payload_len,
        .payload_crc32    = payload_crc32,
    };

    PacketHeaderWire wire;
    packet_header_to_wire(&header, &wire);

    memcpy(output, &wire, sizeof(wire));

    if(payload_len != 0U) {
        memcpy(output + PACKET_HEADER_WIRE_SIZE, payload, payload_len);
    }

    return PACKET_HEADER_WIRE_SIZE + (size_t)payload_len;
}

static void test_frame_build_parse_ping(void) {
    uint8_t buffer[PACKET_HEADER_WIRE_SIZE];
    size_t output_size = 99U;

    TEST_ASSERT_EQ_INT(
        0,
        frame_build_uncompressed(
            MSG_TYPE_PING_REQUEST, 1U, NULL, 0U, buffer, sizeof(buffer), &output_size
        )
    );
    TEST_ASSERT_EQ_SIZE(PACKET_HEADER_WIRE_SIZE, output_size);

    ParsedFrame frame;
    TEST_ASSERT_EQ_INT(FRAME_PARSE_COMPLETE, frame_try_parse(buffer, output_size, &frame));
    TEST_ASSERT_EQ_INT(MSG_TYPE_PING_REQUEST, frame.header.type);
    TEST_ASSERT_EQ_U32(1U, frame.header.request_id);
    TEST_ASSERT_EQ_U32(0U, frame.header.payload_len);
    TEST_ASSERT_EQ_SIZE(PACKET_HEADER_WIRE_SIZE, frame.frame_size);
}

static void test_frame_build_parse_binary_echo(void) {
    static const uint8_t payload[] = {0x00U, 0x01U, 0x7FU, 0x80U, 0xFEU, 0xFFU};
    uint8_t buffer[PACKET_HEADER_WIRE_SIZE + sizeof(payload)];
    size_t output_size = 0U;

    TEST_ASSERT_EQ_INT(
        0,
        frame_build_uncompressed(
            MSG_TYPE_ECHO_REQUEST, 17U, payload, (uint32_t)sizeof(payload), buffer, sizeof(buffer), &output_size
        )
    );

    ParsedFrame frame;
    TEST_ASSERT_EQ_INT(FRAME_PARSE_COMPLETE, frame_try_parse(buffer, output_size, &frame));
    TEST_ASSERT_EQ_INT(MSG_TYPE_ECHO_REQUEST, frame.header.type);
    TEST_ASSERT_EQ_U32(17U, frame.header.request_id);
    TEST_ASSERT_EQ_U32(sizeof(payload), frame.header.payload_len);
    TEST_ASSERT_MEMORY(payload, frame.payload, sizeof(payload));
}

static void test_frame_incomplete_header_and_payload(void) {
    static const uint8_t payload[] = "partial payload";
    uint8_t buffer[PACKET_HEADER_WIRE_SIZE + sizeof(payload) - 1U];
    size_t output_size = 0U;

    TEST_ASSERT_EQ_INT(
        0,
        frame_build_uncompressed(
            MSG_TYPE_ECHO_REQUEST, 2U, payload, (uint32_t)(sizeof(payload) - 1U), buffer, sizeof(buffer), &output_size
        )
    );

    ParsedFrame frame;
    TEST_ASSERT_EQ_INT(FRAME_PARSE_INCOMPLETE, frame_try_parse(buffer, PACKET_HEADER_WIRE_SIZE - 1U, &frame));
    TEST_ASSERT_EQ_INT(FRAME_PARSE_INCOMPLETE, frame_try_parse(buffer, output_size - 1U, &frame));
    TEST_ASSERT_EQ_INT(FRAME_PARSE_COMPLETE, frame_try_parse(buffer, output_size, &frame));
}

static void test_frame_invalid_arguments(void) {
    uint8_t buffer[64];
    size_t output_size = 123U;
    ParsedFrame frame;

    TEST_ASSERT_EQ_INT(FRAME_PARSE_INVALID_HEADER, frame_try_parse(NULL, 0U, &frame));
    TEST_ASSERT_EQ_INT(FRAME_PARSE_INVALID_HEADER, frame_try_parse(buffer, sizeof(buffer), NULL));

    TEST_ASSERT_EQ_INT(-1, frame_build_uncompressed(MSG_TYPE_PING_REQUEST, 1U, NULL, 0U, NULL, 0U, &output_size));
    TEST_ASSERT_EQ_SIZE(0U, output_size);

    output_size = 123U;
    TEST_ASSERT_EQ_INT(-1, frame_build_uncompressed(MSG_TYPE_ECHO_REQUEST, 1U, NULL, 1U, buffer, sizeof(buffer), &output_size));
    TEST_ASSERT_EQ_SIZE(0U, output_size);
}

static void test_frame_small_output_buffer(void) {
    uint8_t buffer[PACKET_HEADER_WIRE_SIZE - 1U];
    size_t output_size = 99U;

    TEST_ASSERT_EQ_INT(
        -1,
        frame_build_uncompressed(
            MSG_TYPE_PING_REQUEST, 1U, NULL, 0U, buffer, sizeof(buffer), &output_size
        )
    );
    TEST_ASSERT_EQ_SIZE(0U, output_size);
}

static void test_frame_invalid_header(void) {
    uint8_t buffer[PACKET_HEADER_WIRE_SIZE];
    size_t output_size = 0U;

    TEST_ASSERT_EQ_INT(
        0,
        frame_build_uncompressed(
            MSG_TYPE_PING_REQUEST, 1U, NULL, 0U, buffer, sizeof(buffer), &output_size
        )
    );

    buffer[0] ^= 0xFFU;

    ParsedFrame frame;
    TEST_ASSERT_EQ_INT(FRAME_PARSE_INVALID_HEADER, frame_try_parse(buffer, output_size, &frame));
}

static void test_frame_invalid_crc(void) {
    static const uint8_t payload[] = "crc payload";
    uint8_t buffer[PACKET_HEADER_WIRE_SIZE + sizeof(payload) - 1U];
    size_t output_size = 0U;

    TEST_ASSERT_EQ_INT(
        0,
        frame_build_uncompressed(
            MSG_TYPE_ECHO_REQUEST, 3U, payload, (uint32_t)(sizeof(payload) - 1U), buffer, sizeof(buffer), &output_size
        )
    );

    buffer[output_size - 1U] ^= 0x01U;

    ParsedFrame frame;
    TEST_ASSERT_EQ_INT(FRAME_PARSE_INVALID_CRC, frame_try_parse(buffer, output_size, &frame));
}

static void test_frame_unsupported_compression(void) {
    static const uint8_t payload[] = {1U, 2U, 3U};
    uint8_t buffer[PACKET_HEADER_WIRE_SIZE + sizeof(payload)];

    const size_t frame_size = build_manual_frame(
        buffer, MSG_TYPE_ECHO_REQUEST, PACKET_FLAG_COMPRESSED, 4U, 12U, payload, (uint32_t)sizeof(payload),
        test_crc32(payload, (uint32_t)sizeof(payload))
    );

    ParsedFrame frame;
    TEST_ASSERT_EQ_INT(FRAME_PARSE_UNSUPPORTED_COMPRESSION, frame_try_parse(buffer, frame_size, &frame));
}

static void test_frame_multiple_frames(void) {
    static const uint8_t payload[] = "second";
    uint8_t buffer[2U * PACKET_HEADER_WIRE_SIZE + sizeof(payload) - 1U];
    size_t first_size = 0U;
    size_t second_size = 0U;

    TEST_ASSERT_EQ_INT(
        0,
        frame_build_uncompressed(
            MSG_TYPE_PING_REQUEST, 10U, NULL, 0U, buffer, sizeof(buffer), &first_size
        )
    );
    TEST_ASSERT_EQ_INT(
        0,
        frame_build_uncompressed(
            MSG_TYPE_ECHO_REQUEST, 11U, payload, (uint32_t)(sizeof(payload) - 1U), buffer + first_size,
            sizeof(buffer) - first_size, &second_size
        )
    );

    ParsedFrame first;
    ParsedFrame second;

    TEST_ASSERT_EQ_INT(FRAME_PARSE_COMPLETE, frame_try_parse(buffer, first_size + second_size, &first));
    TEST_ASSERT_EQ_SIZE(first_size, first.frame_size);
    TEST_ASSERT_EQ_INT(MSG_TYPE_PING_REQUEST, first.header.type);

    TEST_ASSERT_EQ_INT(
        FRAME_PARSE_COMPLETE, frame_try_parse(buffer + first.frame_size, first_size + second_size - first.frame_size, &second)
    );
    TEST_ASSERT_EQ_INT(MSG_TYPE_ECHO_REQUEST, second.header.type);
    TEST_ASSERT_MEMORY(payload, second.payload, sizeof(payload) - 1U);
}

static void test_frame_maximum_payload(void) {
    uint8_t* payload = malloc(PROTOCOL_MAX_PAYLOAD_LENGTH);
    uint8_t* buffer = malloc(PACKET_HEADER_WIRE_SIZE + PROTOCOL_MAX_PAYLOAD_LENGTH);

    TEST_ASSERT(payload != NULL);
    TEST_ASSERT(buffer != NULL);

    for(size_t i = 0U; i < PROTOCOL_MAX_PAYLOAD_LENGTH; ++i) {
        payload[i] = (uint8_t)(i % 251U);
    }

    size_t output_size = 0U;
    TEST_ASSERT_EQ_INT(
        0,
        frame_build_uncompressed(
            MSG_TYPE_ECHO_REQUEST, 12U, payload, PROTOCOL_MAX_PAYLOAD_LENGTH, buffer,
            PACKET_HEADER_WIRE_SIZE + PROTOCOL_MAX_PAYLOAD_LENGTH, &output_size
        )
    );

    ParsedFrame frame;
    TEST_ASSERT_EQ_INT(FRAME_PARSE_COMPLETE, frame_try_parse(buffer, output_size, &frame));
    TEST_ASSERT_EQ_U32(PROTOCOL_MAX_PAYLOAD_LENGTH, frame.header.payload_len);
    TEST_ASSERT_MEMORY(payload, frame.payload, PROTOCOL_MAX_PAYLOAD_LENGTH);

    free(buffer);
    free(payload);
}

void register_frame_tests(TestSuite* suite) {
    TEST_ADD(suite, test_frame_build_parse_ping);
    TEST_ADD(suite, test_frame_build_parse_binary_echo);
    TEST_ADD(suite, test_frame_incomplete_header_and_payload);
    TEST_ADD(suite, test_frame_invalid_arguments);
    TEST_ADD(suite, test_frame_small_output_buffer);
    TEST_ADD(suite, test_frame_invalid_header);
    TEST_ADD(suite, test_frame_invalid_crc);
    TEST_ADD(suite, test_frame_unsupported_compression);
    TEST_ADD(suite, test_frame_multiple_frames);
    TEST_ADD(suite, test_frame_maximum_payload);
}
