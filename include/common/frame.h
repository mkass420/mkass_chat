#ifndef FRAME_H
#define FRAME_H

#include "common/protocol.h"

#include <stdint.h>
#include <stddef.h>

typedef enum {
    FRAME_PARSE_COMPLETE = 0,
    FRAME_PARSE_INCOMPLETE,
    FRAME_PARSE_INVALID_HEADER,
    FRAME_PARSE_INVALID_CRC,
    FRAME_PARSE_UNSUPPORTED_COMPRESSION
} FrameParseResult;

typedef struct {
    PacketHeader   header;
    const uint8_t* payload; // Указатель внутрь входного буфера, недействителен после сдвига буфера.
    size_t         frame_size;
} ParsedFrame;

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

#endif
