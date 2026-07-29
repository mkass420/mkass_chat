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
    uint8_t*       output,
    MessageType    type,
    uint8_t        flags,
    uint32_t       request_id,
    uint32_t       uncompressed_len,
    const uint8_t* payload,
    uint32_t       payload_len
) {
    const PacketHeader header = {
        .magic            = PROTOCOL_MAGIC,
        .type             = type,
        .flags            = flags,
        .request_id       = request_id,
        .uncompressed_len = uncompressed_len,
        .payload_len      = payload_len,
        .payload_crc32    = test_crc32(payload, payload_len),
    };

    PacketHeaderWire wire;
    packet_header_to_wire(&header, &wire);
    memcpy(output, &wire, sizeof(wire));

    if(payload_len != 0U) {
        memcpy(output + PACKET_HEADER_WIRE_SIZE, payload, payload_len);
    }

    return PACKET_HEADER_WIRE_SIZE + (size_t)payload_len;
}

static void fill_pseudorandom(uint8_t* data, size_t size) {
    uint32_t state = 0xA341316CU;

    for(size_t i = 0U; i < size; ++i) {
        state ^= state << 13U;
        state ^= state >> 17U;
        state ^= state << 5U;
        data[i] = (uint8_t)state;
    }
}

static bool parse_and_decode(
    FrameCodec*    codec,
    const uint8_t* buffer,
    size_t         buffer_size,
    ParsedFrame*   parsed,
    DecodedFrame*  decoded
) {
    return frame_try_parse(buffer, buffer_size, parsed) == FRAME_PARSE_COMPLETE &&
           frame_decode(codec, parsed, decoded) == FRAME_DECODE_OK;
}

static bool test_frame_build_parse_ping(void) {
    uint8_t buffer[PACKET_HEADER_WIRE_SIZE];
    size_t  output_size = 99U;

    TEST_ASSERT(
        frame_build_uncompressed(MSG_TYPE_PING_REQUEST, 1U, NULL, 0U, buffer, sizeof(buffer), &output_size) == 0
    );
    TEST_ASSERT(output_size == PACKET_HEADER_WIRE_SIZE);

    ParsedFrame parsed;
    TEST_ASSERT(frame_try_parse(buffer, output_size, &parsed) == FRAME_PARSE_COMPLETE);
    TEST_ASSERT(parsed.header.type == MSG_TYPE_PING_REQUEST);
    TEST_ASSERT(parsed.header.request_id == 1U);
    TEST_ASSERT(parsed.header.payload_len == 0U);
    TEST_ASSERT(parsed.header.uncompressed_len == 0U);
    TEST_ASSERT(parsed.frame_size == PACKET_HEADER_WIRE_SIZE);

    FrameCodec   codec;
    DecodedFrame decoded;
    TEST_ASSERT(frame_decode(&codec, &parsed, &decoded) == FRAME_DECODE_OK);
    TEST_ASSERT(decoded.payload == parsed.wire_payload);
    TEST_ASSERT(decoded.payload_len == 0U);

    return true;
}

static bool test_frame_uncompressed_round_trip(void) {
    static const uint8_t payload[] = {0x00U, 0x01U, 0x7FU, 0x80U, 0xFEU, 0xFFU};
    uint8_t              buffer[PACKET_HEADER_WIRE_SIZE + sizeof(payload)];
    size_t               output_size = 0U;

    TEST_ASSERT(
        frame_build_uncompressed(
            MSG_TYPE_ECHO_REQUEST, 17U, payload, (uint32_t)sizeof(payload), buffer, sizeof(buffer), &output_size
        ) == 0
    );

    FrameCodec   codec;
    ParsedFrame  parsed;
    DecodedFrame decoded;

    TEST_ASSERT(parse_and_decode(&codec, buffer, output_size, &parsed, &decoded));
    TEST_ASSERT(parsed.header.flags == 0U);
    TEST_ASSERT(parsed.wire_payload == buffer + PACKET_HEADER_WIRE_SIZE);
    TEST_ASSERT(decoded.header.type == MSG_TYPE_ECHO_REQUEST);
    TEST_ASSERT(decoded.header.request_id == 17U);
    TEST_ASSERT(decoded.payload_len == sizeof(payload));
    TEST_ASSERT(memcmp(decoded.payload, payload, sizeof(payload)) == 0);

    return true;
}

static bool test_frame_try_policy_small_payload_stays_uncompressed(void) {
    static const uint8_t payload[] = "small echo";
    uint8_t              buffer[PACKET_HEADER_WIRE_SIZE + sizeof(payload)];
    FrameCodec           codec;
    size_t               output_size = 0U;

    TEST_ASSERT(
        frame_build(
            &codec, MSG_TYPE_ECHO_REQUEST, 1U, payload, (uint32_t)(sizeof(payload) - 1U), buffer, sizeof(buffer),
            &output_size
        ) == 0
    );

    ParsedFrame parsed;
    TEST_ASSERT(frame_try_parse(buffer, output_size, &parsed) == FRAME_PARSE_COMPLETE);
    TEST_ASSERT((parsed.header.flags & PACKET_FLAG_COMPRESSED) == 0U);

    return true;
}

static bool test_frame_compressed_round_trip(void) {
    const size_t payload_size = 4096U;
    uint8_t*     payload      = malloc(payload_size);
    uint8_t*     buffer       = malloc(PACKET_HEADER_WIRE_SIZE + PROTOCOL_MAX_PAYLOAD_LENGTH);

    TEST_ASSERT(payload != NULL);
    TEST_ASSERT(buffer != NULL);

    memset(payload, 'A', payload_size);

    FrameCodec codec;
    size_t     output_size  = 0U;
    const int  build_result = frame_build(
        &codec, MSG_TYPE_ECHO_REQUEST, 7U, payload, (uint32_t)payload_size, buffer,
        PACKET_HEADER_WIRE_SIZE + PROTOCOL_MAX_PAYLOAD_LENGTH, &output_size
    );

    TEST_ASSERT(build_result == 0);

    ParsedFrame  parsed;
    DecodedFrame decoded;
    TEST_ASSERT(parse_and_decode(&codec, buffer, output_size, &parsed, &decoded));
    TEST_ASSERT((parsed.header.flags & PACKET_FLAG_COMPRESSED) != 0U);
    TEST_ASSERT(parsed.header.payload_len < parsed.header.uncompressed_len);
    TEST_ASSERT(decoded.payload_len == payload_size);
    TEST_ASSERT(memcmp(decoded.payload, payload, payload_size) == 0);

    free(buffer);
    free(payload);

    return true;
}

static bool test_frame_incompressible_falls_back(void) {
    const size_t payload_size = 4096U;
    uint8_t*     payload      = malloc(payload_size);
    uint8_t*     buffer       = malloc(PACKET_HEADER_WIRE_SIZE + payload_size);

    TEST_ASSERT(payload != NULL);
    TEST_ASSERT(buffer != NULL);

    fill_pseudorandom(payload, payload_size);

    FrameCodec codec;
    size_t     output_size = 0U;
    TEST_ASSERT(
        frame_build(
            &codec, MSG_TYPE_ECHO_REQUEST, 8U, payload, (uint32_t)payload_size, buffer,
            PACKET_HEADER_WIRE_SIZE + payload_size, &output_size
        ) == 0
    );

    ParsedFrame  parsed;
    DecodedFrame decoded;
    TEST_ASSERT(parse_and_decode(&codec, buffer, output_size, &parsed, &decoded));
    TEST_ASSERT((parsed.header.flags & PACKET_FLAG_COMPRESSED) == 0U);
    TEST_ASSERT(decoded.payload_len == payload_size);
    TEST_ASSERT(memcmp(decoded.payload, payload, payload_size) == 0);

    free(buffer);
    free(payload);

    return true;
}

static bool test_frame_large_logical_payload_must_compress(void) {
    const size_t payload_size = 128U * 1024U;
    uint8_t*     payload      = malloc(payload_size);
    uint8_t*     buffer       = malloc(PACKET_HEADER_WIRE_SIZE + PROTOCOL_MAX_PAYLOAD_LENGTH);

    TEST_ASSERT(payload != NULL);
    TEST_ASSERT(buffer != NULL);

    memset(payload, 0x5AU, payload_size);

    FrameCodec codec;
    size_t     output_size = 0U;
    TEST_ASSERT(
        frame_build(
            &codec, MSG_TYPE_ECHO_REQUEST, 9U, payload, (uint32_t)payload_size, buffer,
            PACKET_HEADER_WIRE_SIZE + PROTOCOL_MAX_PAYLOAD_LENGTH, &output_size
        ) == 0
    );

    ParsedFrame  parsed;
    DecodedFrame decoded;
    TEST_ASSERT(parse_and_decode(&codec, buffer, output_size, &parsed, &decoded));
    TEST_ASSERT((parsed.header.flags & PACKET_FLAG_COMPRESSED) != 0U);
    TEST_ASSERT(parsed.header.uncompressed_len == payload_size);
    TEST_ASSERT(parsed.header.payload_len <= PROTOCOL_MAX_PAYLOAD_LENGTH);
    TEST_ASSERT(decoded.payload_len == payload_size);
    TEST_ASSERT(memcmp(decoded.payload, payload, payload_size) == 0);

    free(buffer);
    free(payload);

    return true;
}

static bool test_frame_incomplete_header_and_payload(void) {
    static const uint8_t payload[] = "partial payload";
    uint8_t              buffer[PACKET_HEADER_WIRE_SIZE + sizeof(payload) - 1U];
    size_t               output_size = 0U;

    TEST_ASSERT(
        frame_build_uncompressed(
            MSG_TYPE_ECHO_REQUEST, 2U, payload, (uint32_t)(sizeof(payload) - 1U), buffer, sizeof(buffer), &output_size
        ) == 0
    );

    ParsedFrame parsed;
    TEST_ASSERT(frame_try_parse(buffer, PACKET_HEADER_WIRE_SIZE - 1U, &parsed) == FRAME_PARSE_INCOMPLETE);
    TEST_ASSERT(frame_try_parse(buffer, output_size - 1U, &parsed) == FRAME_PARSE_INCOMPLETE);
    TEST_ASSERT(frame_try_parse(buffer, output_size, &parsed) == FRAME_PARSE_COMPLETE);

    return true;
}

static bool test_frame_invalid_arguments(void) {
    uint8_t      buffer[64]  = {0};
    size_t       output_size = 123U;
    ParsedFrame  parsed;
    DecodedFrame decoded;
    FrameCodec   codec;

    TEST_ASSERT(frame_try_parse(NULL, 0U, &parsed) == FRAME_PARSE_INVALID_HEADER);
    TEST_ASSERT(frame_try_parse(buffer, sizeof(buffer), NULL) == FRAME_PARSE_INVALID_HEADER);

    TEST_ASSERT(frame_build_uncompressed(MSG_TYPE_PING_REQUEST, 1U, NULL, 0U, NULL, 0U, &output_size) == -1);
    TEST_ASSERT(output_size == 0U);

    output_size = 123U;
    TEST_ASSERT(
        frame_build_uncompressed(MSG_TYPE_ECHO_REQUEST, 1U, NULL, 1U, buffer, sizeof(buffer), &output_size) == -1
    );
    TEST_ASSERT(output_size == 0U);

    TEST_ASSERT(frame_decode(NULL, &parsed, &decoded) == FRAME_DECODE_INVALID_ARGUMENT);
    TEST_ASSERT(frame_decode(&codec, NULL, &decoded) == FRAME_DECODE_INVALID_ARGUMENT);
    TEST_ASSERT(frame_decode(&codec, &parsed, NULL) == FRAME_DECODE_INVALID_ARGUMENT);

    return true;
}

static bool test_frame_small_output_buffer(void) {
    uint8_t buffer[PACKET_HEADER_WIRE_SIZE - 1U];
    size_t  output_size = 99U;

    TEST_ASSERT(
        frame_build_uncompressed(MSG_TYPE_PING_REQUEST, 1U, NULL, 0U, buffer, sizeof(buffer), &output_size) == -1
    );
    TEST_ASSERT(output_size == 0U);

    return true;
}

static bool test_frame_invalid_header_and_crc(void) {
    static const uint8_t payload[] = "crc payload";
    uint8_t              buffer[PACKET_HEADER_WIRE_SIZE + sizeof(payload) - 1U];
    size_t               output_size = 0U;

    TEST_ASSERT(
        frame_build_uncompressed(
            MSG_TYPE_ECHO_REQUEST, 3U, payload, (uint32_t)(sizeof(payload) - 1U), buffer, sizeof(buffer), &output_size
        ) == 0
    );

    ParsedFrame parsed;

    buffer[0] ^= 0xFFU;
    TEST_ASSERT(frame_try_parse(buffer, output_size, &parsed) == FRAME_PARSE_INVALID_HEADER);
    buffer[0] ^= 0xFFU;

    buffer[output_size - 1U] ^= 0x01U;
    TEST_ASSERT(frame_try_parse(buffer, output_size, &parsed) == FRAME_PARSE_INVALID_CRC);

    return true;
}

static bool test_frame_decode_corrupt_stream(void) {
    static const uint8_t junk[] = {1U, 2U, 3U, 4U, 5U};
    uint8_t              buffer[PACKET_HEADER_WIRE_SIZE + sizeof(junk)];
    const size_t         frame_size = build_manual_frame(
        buffer, MSG_TYPE_ECHO_REQUEST, PACKET_FLAG_COMPRESSED, 4U, 64U, junk, (uint32_t)sizeof(junk)
    );

    ParsedFrame parsed;
    TEST_ASSERT(frame_try_parse(buffer, frame_size, &parsed) == FRAME_PARSE_COMPLETE);

    FrameCodec   codec;
    DecodedFrame decoded;
    TEST_ASSERT(frame_decode(&codec, &parsed, &decoded) == FRAME_DECODE_INVALID_STREAM);

    return true;
}

static bool test_frame_decode_size_mismatch(void) {
    static const uint8_t payload[] = "compressed payload compressed payload";
    uint8_t              compressed[128];
    uLongf               compressed_len = sizeof(compressed);

    TEST_ASSERT(
        compress2(compressed, &compressed_len, payload, (uLong)(sizeof(payload) - 1U), Z_DEFAULT_COMPRESSION) == Z_OK
    );

    uint8_t      buffer[PACKET_HEADER_WIRE_SIZE + sizeof(compressed)];
    const size_t frame_size = build_manual_frame(
        buffer, MSG_TYPE_ECHO_REQUEST, PACKET_FLAG_COMPRESSED, 5U, (uint32_t)sizeof(payload), compressed,
        (uint32_t)compressed_len
    );

    ParsedFrame parsed;
    TEST_ASSERT(frame_try_parse(buffer, frame_size, &parsed) == FRAME_PARSE_COMPLETE);

    FrameCodec   codec;
    DecodedFrame decoded;
    TEST_ASSERT(frame_decode(&codec, &parsed, &decoded) == FRAME_DECODE_INVALID_STREAM);

    return true;
}

static bool test_frame_decode_rejects_trailing_compressed_data(void) {
    static const uint8_t payload[] = "trailing data test trailing data test";
    uint8_t              compressed[128];
    uLongf               compressed_len = sizeof(compressed) - 1U;

    TEST_ASSERT(
        compress2(compressed, &compressed_len, payload, (uLong)(sizeof(payload) - 1U), Z_DEFAULT_COMPRESSION) == Z_OK
    );

    compressed[compressed_len] = 0xAAU;
    ++compressed_len;

    uint8_t      buffer[PACKET_HEADER_WIRE_SIZE + sizeof(compressed)];
    const size_t frame_size = build_manual_frame(
        buffer, MSG_TYPE_ECHO_REQUEST, PACKET_FLAG_COMPRESSED, 6U, (uint32_t)(sizeof(payload) - 1U), compressed,
        (uint32_t)compressed_len
    );

    ParsedFrame parsed;
    TEST_ASSERT(frame_try_parse(buffer, frame_size, &parsed) == FRAME_PARSE_COMPLETE);

    FrameCodec   codec;
    DecodedFrame decoded;
    TEST_ASSERT(frame_decode(&codec, &parsed, &decoded) == FRAME_DECODE_INVALID_STREAM);

    return true;
}

static bool test_frame_multiple_frames(void) {
    static const uint8_t payload[] = "second";
    uint8_t              buffer[2U * PACKET_HEADER_WIRE_SIZE + sizeof(payload) - 1U];
    size_t               first_size  = 0U;
    size_t               second_size = 0U;

    TEST_ASSERT(
        frame_build_uncompressed(MSG_TYPE_PING_REQUEST, 10U, NULL, 0U, buffer, sizeof(buffer), &first_size) == 0
    );

    TEST_ASSERT(
        frame_build_uncompressed(
            MSG_TYPE_ECHO_REQUEST, 11U, payload, (uint32_t)(sizeof(payload) - 1U), buffer + first_size,
            sizeof(buffer) - first_size, &second_size
        ) == 0
    );

    ParsedFrame first;
    ParsedFrame second;

    TEST_ASSERT(frame_try_parse(buffer, first_size + second_size, &first) == FRAME_PARSE_COMPLETE);
    TEST_ASSERT(first.frame_size == first_size);
    TEST_ASSERT(first.header.type == MSG_TYPE_PING_REQUEST);

    TEST_ASSERT(
        frame_try_parse(buffer + first.frame_size, first_size + second_size - first.frame_size, &second) ==
        FRAME_PARSE_COMPLETE
    );
    TEST_ASSERT(second.header.type == MSG_TYPE_ECHO_REQUEST);
    TEST_ASSERT(memcmp(second.wire_payload, payload, sizeof(payload) - 1U) == 0);

    return true;
}

static bool test_frame_maximum_wire_payload(void) {
    uint8_t* payload = malloc(PROTOCOL_MAX_PAYLOAD_LENGTH);
    uint8_t* buffer  = malloc(PACKET_HEADER_WIRE_SIZE + PROTOCOL_MAX_PAYLOAD_LENGTH);

    TEST_ASSERT(payload != NULL);
    TEST_ASSERT(buffer != NULL);

    fill_pseudorandom(payload, PROTOCOL_MAX_PAYLOAD_LENGTH);

    size_t output_size = 0U;
    TEST_ASSERT(
        frame_build_uncompressed(
            MSG_TYPE_ECHO_REQUEST, 12U, payload, PROTOCOL_MAX_PAYLOAD_LENGTH, buffer,
            PACKET_HEADER_WIRE_SIZE + PROTOCOL_MAX_PAYLOAD_LENGTH, &output_size
        ) == 0
    );

    FrameCodec   codec;
    ParsedFrame  parsed;
    DecodedFrame decoded;
    TEST_ASSERT(parse_and_decode(&codec, buffer, output_size, &parsed, &decoded));
    TEST_ASSERT(parsed.header.payload_len == PROTOCOL_MAX_PAYLOAD_LENGTH);
    TEST_ASSERT(decoded.payload_len == PROTOCOL_MAX_PAYLOAD_LENGTH);
    TEST_ASSERT(memcmp(decoded.payload, payload, PROTOCOL_MAX_PAYLOAD_LENGTH) == 0);

    free(buffer);
    free(payload);

    return true;
}

void register_frame_tests(TestSuite* suite) {
    TEST_ADD(suite, test_frame_build_parse_ping);
    TEST_ADD(suite, test_frame_uncompressed_round_trip);
    TEST_ADD(suite, test_frame_try_policy_small_payload_stays_uncompressed);
    TEST_ADD(suite, test_frame_compressed_round_trip);
    TEST_ADD(suite, test_frame_incompressible_falls_back);
    TEST_ADD(suite, test_frame_large_logical_payload_must_compress);
    TEST_ADD(suite, test_frame_incomplete_header_and_payload);
    TEST_ADD(suite, test_frame_invalid_arguments);
    TEST_ADD(suite, test_frame_small_output_buffer);
    TEST_ADD(suite, test_frame_invalid_header_and_crc);
    TEST_ADD(suite, test_frame_decode_corrupt_stream);
    TEST_ADD(suite, test_frame_decode_size_mismatch);
    TEST_ADD(suite, test_frame_decode_rejects_trailing_compressed_data);
    TEST_ADD(suite, test_frame_multiple_frames);
    TEST_ADD(suite, test_frame_maximum_wire_payload);
}
