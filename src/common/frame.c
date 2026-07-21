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

    if((header.flags & PACKET_FLAG_COMPRESSED) != 0U) {
        return FRAME_PARSE_UNSUPPORTED_COMPRESSION;
    }

    const size_t frame_size = PACKET_HEADER_WIRE_SIZE + (size_t)header.payload_len;

    if(buffer_size < frame_size) {
        return FRAME_PARSE_INCOMPLETE;
    }

    const uint8_t* payload = buffer + PACKET_HEADER_WIRE_SIZE;
    const uint32_t actual_crc = payload_crc32(payload, header.payload_len);

    if(actual_crc != header.payload_crc32) {
        return FRAME_PARSE_INVALID_CRC;
    }

    frame->header     = header;
    frame->payload    = payload;
    frame->frame_size = frame_size;

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
    if(output_size != NULL) {
        *output_size = 0U;
    }

    if(output == NULL || output_size == NULL) {
        return -1;
    }

    if(payload_len != 0U && payload == NULL) {
        return -1;
    }

    const size_t frame_size = PACKET_HEADER_WIRE_SIZE + (size_t)payload_len;

    if(frame_size > output_capacity) {
        return -1;
    }

    PacketHeader header = {
        .magic            = PROTOCOL_MAGIC,
        .type             = type,
        .flags            = 0U,
        .request_id       = request_id,
        .uncompressed_len = payload_len,
        .payload_len      = payload_len,
        .payload_crc32    = payload_crc32(payload, payload_len),
    };

    if(packet_header_validate(&header) != PACKET_HEADER_VALID) {
        return -1;
    }

    PacketHeaderWire wire_header;
    packet_header_to_wire(&header, &wire_header);

    memcpy(output, &wire_header, sizeof(wire_header));

    if(payload_len != 0U) {
        memcpy(output + PACKET_HEADER_WIRE_SIZE, payload, payload_len);
    }

    *output_size = frame_size;
    return 0;
}
