#include "common/frame.h"

#include <string.h>

#include <zlib.h>

static uint32_t payload_crc32(const uint8_t* payload, uint32_t payload_len) {
    uLong crc = crc32(0L, Z_NULL, 0);

    if(payload_len != 0U) {
        crc = crc32(crc, payload, (uInt)payload_len);
    }

    return (uint32_t)crc;
}

static int frame_write(
    MessageType    type,
    uint32_t       request_id,
    uint8_t        flags,
    uint32_t       uncompressed_len,
    const uint8_t* wire_payload,
    uint32_t       wire_payload_len,
    uint8_t*       output,
    size_t         output_capacity,
    size_t*        output_size
) {
    if(output_size != NULL) {
        *output_size = 0U;
    }

    if(output == NULL || output_size == NULL) {
        return -1;
    }

    if(wire_payload_len != 0U && wire_payload == NULL) {
        return -1;
    }

    const size_t frame_size = PACKET_HEADER_WIRE_SIZE + (size_t)wire_payload_len;

    if(frame_size > output_capacity) {
        return -1;
    }

    const PacketHeader header = {
        .magic            = PROTOCOL_MAGIC,
        .type             = type,
        .flags            = flags,
        .request_id       = request_id,
        .uncompressed_len = uncompressed_len,
        .payload_len      = wire_payload_len,
        .payload_crc32    = payload_crc32(wire_payload, wire_payload_len),
    };

    if(packet_header_validate(&header) != PACKET_HEADER_VALID) {
        return -1;
    }

    PacketHeaderWire wire_header;

    packet_header_to_wire(&header, &wire_header);

    memcpy(output, &wire_header, sizeof(wire_header));

    if(wire_payload_len != 0U) {
        memcpy(output + PACKET_HEADER_WIRE_SIZE, wire_payload, wire_payload_len);
    }

    *output_size = frame_size;

    return 0;
}

FrameParseResult frame_try_parse(const uint8_t* buffer, size_t buffer_size, ParsedFrame* frame) {
    if(buffer == NULL || frame == NULL) {
        return FRAME_PARSE_INVALID_HEADER;
    }

    if(buffer_size < PACKET_HEADER_WIRE_SIZE) {
        return FRAME_PARSE_INCOMPLETE;
    }

    PacketHeaderWire wire_header;

    memcpy(&wire_header, buffer, sizeof(wire_header));

    PacketHeader header;

    packet_header_from_wire(&wire_header, &header);

    if(packet_header_validate(&header) != PACKET_HEADER_VALID) {
        return FRAME_PARSE_INVALID_HEADER;
    }

    const size_t frame_size = PACKET_HEADER_WIRE_SIZE + (size_t)header.payload_len;

    if(buffer_size < frame_size) {
        return FRAME_PARSE_INCOMPLETE;
    }

    const uint8_t* wire_payload = buffer + PACKET_HEADER_WIRE_SIZE;

    const uint32_t actual_crc = payload_crc32(wire_payload, header.payload_len);

    if(actual_crc != header.payload_crc32) {
        return FRAME_PARSE_INVALID_CRC;
    }

    frame->header       = header;
    frame->wire_payload = wire_payload;
    frame->frame_size   = frame_size;

    return FRAME_PARSE_COMPLETE;
}

int frame_build_uncompressed(
    MessageType    type,
    uint32_t       request_id,
    const uint8_t* payload,
    uint32_t       payload_len,
    uint8_t*       output,
    size_t         output_capacity,
    size_t*        output_size
) {
    if(payload_len > PROTOCOL_MAX_PAYLOAD_LENGTH) {
        if(output_size != NULL) {
            *output_size = 0U;
        }

        return -1;
    }
    return frame_write(type, request_id, 0U, payload_len, payload, payload_len, output, output_capacity, output_size);
}

FrameDecodeResult frame_decode(FrameCodec* codec, const ParsedFrame* parsed, DecodedFrame* decoded) {
    if(codec == NULL || parsed == NULL || decoded == NULL) {
        return FRAME_DECODE_INVALID_ARGUMENT;
    }

    decoded->payload     = NULL;
    decoded->payload_len = 0U;

    if(packet_header_validate(&parsed->header) != PACKET_HEADER_VALID) {
        return FRAME_DECODE_INVALID_SIZE;
    }

    const size_t expected_frame_size = PACKET_HEADER_WIRE_SIZE + (size_t)parsed->header.payload_len;

    if(parsed->frame_size != expected_frame_size) {
        return FRAME_DECODE_INVALID_SIZE;
    }

    if(parsed->header.payload_len != 0U && parsed->wire_payload == NULL) {
        return FRAME_DECODE_INVALID_ARGUMENT;
    }

    decoded->header = parsed->header;

    if((parsed->header.flags & PACKET_FLAG_COMPRESSED) == 0U) {
        decoded->payload = parsed->wire_payload;

        decoded->payload_len = parsed->header.uncompressed_len;

        return FRAME_DECODE_OK;
    }

    z_stream stream = {0};

    stream.next_in   = (Bytef*)parsed->wire_payload;
    stream.avail_in  = (uInt)parsed->header.payload_len;
    stream.next_out  = codec->decompress_buffer;
    stream.avail_out = (uInt)(parsed->header.uncompressed_len + 1U);

    if(inflateInit(&stream) != Z_OK) {
        return FRAME_DECODE_INVALID_STREAM;
    }

    const int  inflate_result  = inflate(&stream, Z_FINISH);
    const bool stream_is_valid = inflate_result == Z_STREAM_END &&
                                 stream.total_in == (uLong)parsed->header.payload_len &&
                                 stream.total_out == (uLong)parsed->header.uncompressed_len;

    (void)inflateEnd(&stream);

    if(!stream_is_valid) {
        return FRAME_DECODE_INVALID_STREAM;
    }

    decoded->payload     = codec->decompress_buffer;
    decoded->payload_len = parsed->header.uncompressed_len;

    return FRAME_DECODE_OK;
}

int frame_build(
    FrameCodec*    codec,
    MessageType    type,
    uint32_t       request_id,
    const uint8_t* payload,
    uint32_t       payload_len,
    uint8_t*       output,
    size_t         output_capacity,
    size_t*        output_size
) {
    if(output_size != NULL) {
        *output_size = 0U;
    }

    if(codec == NULL || output == NULL || output_size == NULL) {
        return -1;
    }

    if(payload_len != 0U && payload == NULL) {
        return -1;
    }

    if(payload_len > PROTOCOL_MAX_UNCOMPRESSED_LENGTH) {
        return -1;
    }

    const MessageTypeInfo* info = message_type_get_info(type);

    if(info == NULL) {
        return -1;
    }

    if(info->compression_policy == COMPRESSION_POLICY_NEVER ||
        (info->compression_policy == COMPRESSION_POLICY_TRY && payload_len < COMPRESSION_MIN_INPUT_SIZE)) {
        return frame_build_uncompressed(type, request_id, payload, payload_len, output, output_capacity, output_size);
    }

    if(payload_len == 0U) {
        return -1;
    }

    uLongf compressed_len = (uLongf)sizeof(codec->compress_buffer);

    const int compression_result =
        compress2(codec->compress_buffer, &compressed_len, payload, (uLong)payload_len, Z_DEFAULT_COMPRESSION);

    if(compression_result == Z_OK) {
        const uint32_t compressed_size = (uint32_t)compressed_len;
        const bool     required        = info->compression_policy == COMPRESSION_POLICY_REQUIRE;
        const bool     needed_to_fit   = payload_len > PROTOCOL_MAX_PAYLOAD_LENGTH;
        const bool     saves_enough =
            compressed_size < payload_len && payload_len - compressed_size >= COMPRESSION_MIN_SAVING;

        if(required || needed_to_fit || saves_enough) {
            return frame_write(
                type, request_id, PACKET_FLAG_COMPRESSED, payload_len, codec->compress_buffer, compressed_size, output,
                output_capacity, output_size
            );
        }
    }

    if(info->compression_policy == COMPRESSION_POLICY_REQUIRE || payload_len > PROTOCOL_MAX_PAYLOAD_LENGTH) {
        return -1;
    }

    return frame_build_uncompressed(type, request_id, payload, payload_len, output, output_capacity, output_size);
}
