#ifndef FRAME_H
#define FRAME_H

#include "common/protocol.h"

#include <stddef.h>
#include <stdint.h>

typedef enum {
    FRAME_PARSE_COMPLETE = 0,
    FRAME_PARSE_INCOMPLETE,
    FRAME_PARSE_INVALID_HEADER,
    FRAME_PARSE_INVALID_CRC
} FrameParseResult;

typedef enum {
    FRAME_DECODE_OK = 0,
    FRAME_DECODE_INVALID_ARGUMENT,
    FRAME_DECODE_INVALID_SIZE,
    FRAME_DECODE_INVALID_STREAM
} FrameDecodeResult;

typedef struct {
    PacketHeader   header;
    const uint8_t* wire_payload; // Указатель внутрь входного буфера.
    size_t         frame_size;
} ParsedFrame;

typedef struct {
    PacketHeader   header;
    const uint8_t* payload; // Логический payload после распаковки.
    uint32_t       payload_len;
} DecodedFrame;

typedef struct {
    uint8_t frame_buffer[PACKET_HEADER_WIRE_SIZE + PROTOCOL_MAX_PAYLOAD_LENGTH];
    uint8_t compress_buffer[PROTOCOL_MAX_PAYLOAD_LENGTH];
    uint8_t decompress_buffer[PROTOCOL_MAX_UNCOMPRESSED_LENGTH + 1U];
} FrameCodec;

FrameParseResult frame_try_parse(const uint8_t* buffer, size_t buffer_size, ParsedFrame* frame);

int frame_build_uncompressed(
    MessageType    type,
    uint32_t       request_id,
    const uint8_t* payload,
    uint32_t       payload_len,
    uint8_t*       output,
    size_t         output_capacity,
    size_t*        output_size
);

FrameDecodeResult frame_decode(FrameCodec* codec, const ParsedFrame* parsed, DecodedFrame* decoded);

int frame_build(
    FrameCodec*    codec,
    MessageType    type,
    uint32_t       request_id,
    const uint8_t* payload,
    uint32_t       payload_len,
    uint8_t*       output,
    size_t         output_capacity,
    size_t*        output_size
);

#endif
