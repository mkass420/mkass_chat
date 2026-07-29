#include "server/transport.h"

#include "common/frame.h"
#include "common/protocol.h"

#include <assert.h>
#include <errno.h>
#include <string.h>

#include <sys/socket.h>

static_assert(
    TRANSPORT_READ_BUFFER_SIZE >= PACKET_HEADER_WIRE_SIZE + PROTOCOL_MAX_PAYLOAD_LENGTH,
    "Transport read buffer must fit one maximum frame"
);

static_assert(
    TRANSPORT_WRITE_BUFFER_SIZE >= PACKET_HEADER_WIRE_SIZE + PROTOCOL_MAX_PAYLOAD_LENGTH,
    "Transport write buffer must fit one maximum frame"
);

static void transport_assert(const TransportSession* transport) {
    assert(transport != NULL);

    assert(transport->read_bytes <= sizeof(transport->read_buffer));

    assert(transport->write_offset <= transport->write_bytes);

    assert(transport->write_bytes <= sizeof(transport->write_buffer));
}

void transport_reset(TransportSession* transport) {
    assert(transport != NULL);

    transport->socket_fd = -1;

    transport->read_bytes = 0U;

    transport->write_offset = 0U;
    transport->write_bytes  = 0U;
}

void transport_init(TransportSession* transport, int socket_fd) {
    assert(transport != NULL);
    assert(socket_fd >= 0);

    transport_reset(transport);

    transport->socket_fd = socket_fd;
}

bool transport_is_active(const TransportSession* transport) { return transport != NULL && transport->socket_fd >= 0; }

bool transport_has_pending_write(const TransportSession* transport) {
    return transport != NULL && transport->write_offset < transport->write_bytes;
}

static void transport_compact_write_buffer(TransportSession* transport) {
    transport_assert(transport);

    if(transport->write_offset == 0U) {
        return;
    }

    const size_t remaining = transport->write_bytes - transport->write_offset;

    if(remaining != 0U) {
        memmove(transport->write_buffer, transport->write_buffer + transport->write_offset, remaining);
    }

    transport->write_offset = 0U;
    transport->write_bytes  = remaining;
}

TransportIoResult transport_queue_frame(
    TransportSession* transport,
    FrameCodec*       codec,
    MessageType       type,
    uint32_t          request_id,
    const uint8_t*    payload,
    uint32_t          payload_len
) {
    assert(transport_is_active(transport));

    transport_assert(transport);

    if(codec == NULL) {
        return TRANSPORT_IO_INVALID_ARGUMENT;
    }

    if(payload_len != 0U && payload == NULL) {
        return TRANSPORT_IO_INVALID_ARGUMENT;
    }

    if(payload_len > PROTOCOL_MAX_UNCOMPRESSED_LENGTH) {
        return TRANSPORT_IO_INVALID_ARGUMENT;
    }

    size_t frame_size = 0U;

    const int build_result = frame_build(
        codec, type, request_id, payload, payload_len, codec->frame_buffer, sizeof(codec->frame_buffer), &frame_size
    );

    if(build_result != 0 || frame_size == 0U || frame_size > sizeof(codec->frame_buffer)) {
        return TRANSPORT_IO_FRAME_BUILD_ERROR;
    }

    transport_compact_write_buffer(transport);

    const size_t available_size = sizeof(transport->write_buffer) - transport->write_bytes;

    if(frame_size > available_size) {
        return TRANSPORT_IO_BUFFER_FULL;
    }

    memcpy(transport->write_buffer + transport->write_bytes, codec->frame_buffer, frame_size);

    transport->write_bytes += frame_size;

    transport_assert(transport);

    return TRANSPORT_IO_OK;
}

static void transport_consume_input(TransportSession* transport, size_t consumed_size) {
    transport_assert(transport);

    assert(consumed_size <= transport->read_bytes);

    const size_t remaining = transport->read_bytes - consumed_size;

    if(remaining != 0U) {
        memmove(transport->read_buffer, transport->read_buffer + consumed_size, remaining);
    }

    transport->read_bytes = remaining;

    transport_assert(transport);
}

static TransportIoResult transport_process_input(
    TransportSession*     transport,
    FrameCodec*           codec,
    TransportFrameHandler frame_handler,
    void*                 handler_context
) {
    assert(transport != NULL);
    assert(codec != NULL);
    assert(frame_handler != NULL);

    transport_assert(transport);

    for(;;) {
        ParsedFrame parsed_frame;

        const FrameParseResult parse_result =
            frame_try_parse(transport->read_buffer, transport->read_bytes, &parsed_frame);

        // Ждём оставшуюся часть frame.
        if(parse_result == FRAME_PARSE_INCOMPLETE) {
            return TRANSPORT_IO_OK;
        }

        if(parse_result != FRAME_PARSE_COMPLETE) {
            return TRANSPORT_IO_PROTOCOL_ERROR;
        }

        if(parsed_frame.frame_size == 0U || parsed_frame.frame_size > transport->read_bytes) {
            return TRANSPORT_IO_PROTOCOL_ERROR;
        }

        DecodedFrame decoded_frame;

        const FrameDecodeResult decode_result = frame_decode(codec, &parsed_frame, &decoded_frame);

        if(decode_result != FRAME_DECODE_OK) {
            return TRANSPORT_IO_PROTOCOL_ERROR;
        }

        // Payload действует только до следующего decode или сдвига буфера.
        const TransportIoResult handler_result = frame_handler(handler_context, &decoded_frame);

        if(handler_result != TRANSPORT_IO_OK) {
            return handler_result;
        }

        transport_consume_input(transport, parsed_frame.frame_size);
    }
}

TransportIoResult transport_handle_read(
    TransportSession*     transport,
    FrameCodec*           codec,
    TransportFrameHandler frame_handler,
    void*                 handler_context
) {
    assert(transport != NULL);
    assert(codec != NULL);
    assert(frame_handler != NULL);
    assert(transport_is_active(transport));

    transport_assert(transport);

    for(;;) {
        // Сначала обрабатываем уже накопленные данные.
        const TransportIoResult process_result =
            transport_process_input(transport, codec, frame_handler, handler_context);

        if(process_result != TRANSPORT_IO_OK) {
            return process_result;
        }

        const size_t available_size = sizeof(transport->read_buffer) - transport->read_bytes;

        if(available_size == 0U) {
            return TRANSPORT_IO_BUFFER_FULL;
        }

        const ssize_t received =
            recv(transport->socket_fd, transport->read_buffer + transport->read_bytes, available_size, 0);

        if(received > 0) {
            transport->read_bytes += (size_t)received;

            transport_assert(transport);

            continue;
        }

        if(received == 0) {
            return TRANSPORT_IO_PEER_CLOSED;
        }

        if(errno == EINTR) {
            continue;
        }

        if(errno == EAGAIN || errno == EWOULDBLOCK) {
            return TRANSPORT_IO_OK;
        }

        return TRANSPORT_IO_SYSTEM_ERROR;
    }
}

TransportIoResult transport_handle_write(TransportSession* transport) {
    assert(transport != NULL);
    assert(transport_is_active(transport));

    transport_assert(transport);

    while(transport_has_pending_write(transport)) {
        const uint8_t* data = transport->write_buffer + transport->write_offset;

        const size_t remaining = transport->write_bytes - transport->write_offset;

        const ssize_t sent = send(transport->socket_fd, data, remaining, MSG_NOSIGNAL);

        if(sent > 0) {
            transport->write_offset += (size_t)sent;

            transport_assert(transport);

            continue;
        }

        if(sent == 0) {
            return TRANSPORT_IO_SYSTEM_ERROR;
        }

        if(errno == EINTR) {
            continue;
        }

        if(errno == EAGAIN || errno == EWOULDBLOCK) {
            return TRANSPORT_IO_OK;
        }

        return TRANSPORT_IO_SYSTEM_ERROR;
    }

    transport->write_offset = 0U;
    transport->write_bytes  = 0U;

    transport_assert(transport);

    return TRANSPORT_IO_OK;
}
